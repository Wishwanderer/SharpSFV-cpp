// SharpSFV (C++ port) - volume identification and HDD vs SSD detection.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
// Modified 2026-10-03: volume identification; detection reports its method.
// Modified 2026-10-03: serial and device names formatted without printf.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "DriveDetector.h"

#include "PathUtil.h"
#include "Text.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#else
#include <climits>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <unistd.h>
#endif

namespace sfv::drive {

namespace {

bool IsSsdByName(const std::string& name) {
    const std::string n = text::FoldCase(name);
    for (const char* k : {"SSD", "NVME", "FLASH", "M.2", "SAMSUNG"})
        if (n.find(k) != std::string::npos) return true;
    return false;
}

// Everything the probe learned. -1 = unknown.
struct ProbeResult {
    bool volumeOpened = false;
    int volumeSeekPenalty = -1;
    std::string volumeBus, volumeModel;
    int diskNumber = -1;
    bool physicalOpened = false;
    int physicalSeekPenalty = -1;
    std::string friendlyName;
    std::string error;
};

// The decision rules, shared by Detect and GetDebugInfo so they can never disagree.
Detection Decide(const VolumeInfo& v, const ProbeResult& p) {
    auto result = [](bool hdd, std::string how, bool failed = false) { return Detection{hdd, failed, std::move(how)}; };
    if (v.network) return result(true, "network drive");
    if (p.volumeSeekPenalty == 1) return result(true, "volume reports a seek penalty");
    if (p.volumeSeekPenalty == 0) return result(false, "volume reports no seek penalty");
    if (p.physicalSeekPenalty == 1) return result(true, "physical disk reports a seek penalty");
    if (p.physicalSeekPenalty == 0) return result(false, "physical disk reports no seek penalty");
    if (!p.friendlyName.empty()) {
        const bool ssd = IsSsdByName(p.friendlyName);
        return result(!ssd, "model name '" + p.friendlyName + (ssd ? "' suggests an SSD" : "' has no SSD keyword"));
    }
    return result(true, "detection failed (defaulted to HDD)", true);
}

#ifdef _WIN32

// Local definitions (identical layout to winioctl.h) so the build does not depend on the
// SDK/MinGW header version.
struct PropertyQuery {
    DWORD PropertyId;
    DWORD QueryType;
    BYTE AdditionalParameters[1];
};
struct SeekPenaltyDescriptor {
    DWORD Version;
    DWORD Size;
    BOOLEAN IncursSeekPenalty;
};
constexpr DWORD kStorageDeviceProperty = 0;
constexpr DWORD kStorageDeviceSeekPenaltyProperty = 7;
constexpr DWORD kIoctlStorageQueryProperty = 0x002D1400;
constexpr DWORD kIoctlVolumeGetDiskExtents = 0x00560000;

struct Handle {
    HANDLE h;
    explicit Handle(const std::wstring& p)
        : h(CreateFileW(p.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr)) {}
    ~Handle() {
        if (valid()) CloseHandle(h);
    }
    bool valid() const { return h != INVALID_HANDLE_VALUE; }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

int GetSeekPenalty(HANDLE h) {
    PropertyQuery q{};
    q.PropertyId = kStorageDeviceSeekPenaltyProperty;
    SeekPenaltyDescriptor d{};
    DWORD ret = 0;
    if (DeviceIoControl(h, kIoctlStorageQueryProperty, &q, sizeof q, &d, sizeof d, &ret, nullptr) &&
        ret >= sizeof d)
        return d.IncursSeekPenalty ? 1 : 0;
    return -1;
}

const char* BusName(DWORD bus) {
    switch (bus) {
        case 1: return "SCSI";
        case 2: return "ATAPI";
        case 3: return "ATA";
        case 7: return "USB";
        case 8: return "RAID";
        case 10: return "SAS";
        case 11: return "SATA";
        case 12: return "SD";
        case 13: return "MMC";
        case 14: return "Virtual";
        case 15: return "FileBackedVirtual";
        case 16: return "Spaces";
        case 17: return "NVMe";
        case 18: return "SCM";
        case 19: return "UFS";
        default: return "Unknown";
    }
}

// STORAGE_DEVICE_DESCRIPTOR: BusType at offset 24, Vendor/Product id string offsets at 8/12.
void DescribeDevice(HANDLE h, std::string& bus, std::string& model) {
    PropertyQuery q{};
    q.PropertyId = kStorageDeviceProperty;
    alignas(8) BYTE buf[1024] = {};
    DWORD ret = 0;
    if (!DeviceIoControl(h, kIoctlStorageQueryProperty, &q, sizeof q, buf, sizeof buf, &ret, nullptr) || ret < 28)
        return;
    DWORD busType, vendorOff, productOff;
    std::memcpy(&busType, buf + 24, 4);
    std::memcpy(&vendorOff, buf + 8, 4);
    std::memcpy(&productOff, buf + 12, 4);
    bus = BusName(busType);
    auto str = [&](DWORD off) -> std::string {
        if (off == 0 || off >= ret) return {};
        const char* s = reinterpret_cast<const char*>(buf + off);
        return std::string(text::Trim(std::string(s, strnlen(s, ret - off))));
    };
    const std::string vendor = str(vendorOff), product = str(productOff);
    model = vendor.empty() ? product : (product.empty() ? vendor : vendor + " " + product);
}

int GetPhysicalDriveNumber(HANDLE h) {
    // VOLUME_DISK_EXTENTS { DWORD NumberOfDiskExtents; DISK_EXTENT Extents[]; } where
    // DISK_EXTENT { DWORD DiskNumber; LARGE_INTEGER StartingOffset, ExtentLength; } (8-aligned).
    alignas(8) BYTE buf[8 + 24 * 8] = {};
    DWORD ret = 0;
    if (!DeviceIoControl(h, kIoctlVolumeGetDiskExtents, nullptr, 0, buf, sizeof buf, &ret, nullptr)) return -1;
    DWORD n, disk;
    std::memcpy(&n, buf, 4);
    std::memcpy(&disk, buf + 8, 4);
    return n > 0 ? static_cast<int>(disk) : -1;
}

std::string ReadRegString(HKEY root, const std::wstring& subKey, const std::wstring& value) {
    HKEY key;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return {};
    DWORD type = 0, size = 0;
    std::string result;
    if (RegQueryValueExW(key, value.c_str(), nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && size > 0) {
        std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
        if (RegQueryValueExW(key, value.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &size) ==
            ERROR_SUCCESS)
            result = text::Narrow(buf.c_str());
    }
    RegCloseKey(key);
    return result;
}

std::string GetFriendlyNameFromRegistry(int diskNumber) {
    const std::string deviceId = ReadRegString(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\disk\\Enum",
                                               text::Widen(std::to_string(diskNumber)));
    if (deviceId.empty()) return {};
    const std::wstring key = L"SYSTEM\\CurrentControlSet\\Enum\\" + text::Widen(deviceId);
    std::string name = ReadRegString(HKEY_LOCAL_MACHINE, key, L"FriendlyName");
    if (name.empty()) name = ReadRegString(HKEY_LOCAL_MACHINE, key, L"DeviceDesc");
    return name;
}

const char* DriveTypeName(UINT t) {
    switch (t) {
        case DRIVE_NO_ROOT_DIR: return "NoRootDirectory";
        case DRIVE_REMOVABLE: return "Removable";
        case DRIVE_FIXED: return "Fixed";
        case DRIVE_REMOTE: return "Network";
        case DRIVE_CDROM: return "CDRom";
        case DRIVE_RAMDISK: return "Ram";
        default: return "Unknown";
    }
}

VolumeInfo IdentifyVolumeImpl(const std::string& p) {
    VolumeInfo v;
    v.probePath = path::GetFullPath(p);
    if (v.probePath.empty()) return v;

    const std::wstring wfull = text::Widen(v.probePath);
    std::wstring mp(MAX_PATH + 1, L'\0');
    if (GetVolumePathNameW(wfull.c_str(), mp.data(), MAX_PATH)) {
        mp.resize(wcslen(mp.c_str()));
    } else {
        mp = text::Widen(path::GetPathRoot(v.probePath));
        if (!mp.empty() && mp.back() != L'\\') mp.push_back(L'\\');
    }
    if (mp.empty()) return v;
    v.mountPoint = text::Narrow(mp);

    const UINT type = GetDriveTypeW(mp.c_str());
    v.driveType = DriveTypeName(type);
    const bool unc = mp.size() > 2 && mp[0] == L'\\' && mp[1] == L'\\' && mp[2] != L'?' && mp[2] != L'.';
    if (type == DRIVE_REMOTE || unc) {
        v.network = true;
        v.driveType = "Network";
        while (v.mountPoint.size() > 3 && path::IsSep(v.mountPoint.back())) v.mountPoint.pop_back();
        return v;
    }

    wchar_t name[64] = {};
    if (GetVolumeNameForVolumeMountPointW(mp.c_str(), name, 64)) { // \\?\Volume{GUID}\ //
        const std::wstring n = name;
        const auto open = n.find(L'{'), close = n.find(L'}');
        if (open != std::wstring::npos && close != std::wstring::npos && close > open) {
            v.guid = text::Narrow(n.substr(open, close - open + 1));
            v.devicePath = "\\\\.\\Volume" + v.guid;
        }
    }
    if (v.devicePath.empty() && v.mountPoint.size() >= 2 && v.mountPoint[1] == ':')
        v.devicePath = "\\\\.\\" + v.mountPoint.substr(0, 2);

    wchar_t label[MAX_PATH + 1] = {}, fsName[MAX_PATH + 1] = {};
    DWORD serial = 0;
    if (GetVolumeInformationW(mp.c_str(), label, MAX_PATH + 1, &serial, nullptr, nullptr, fsName, MAX_PATH + 1)) {
        if (serial != 0) { // "%04X-%04X", without pulling in printf
            static constexpr char kHex[] = "0123456789ABCDEF";
            std::string sn(9, '-');
            for (int i = 0, shift = 28; i < 9; ++i) {
                if (i == 4) continue;
                sn[static_cast<std::size_t>(i)] = kHex[(serial >> shift) & 0xF];
                shift -= 4;
            }
            v.serial = sn;
        }
        v.label = text::Narrow(label);
        v.fileSystem = text::Narrow(fsName);
    }
    return v;
}

ProbeResult Probe(const VolumeInfo& v) {
    ProbeResult r;
    if (v.network) return r;
    if (v.devicePath.empty()) {
        r.error = "No device path for this volume.";
        return r;
    }
    Handle hVol(text::Widen(v.devicePath));
    if (!hVol.valid()) {
        r.error = "Could not open " + v.devicePath + " (error " + std::to_string(GetLastError()) + ").";
        return r;
    }
    r.volumeOpened = true;
    r.volumeSeekPenalty = GetSeekPenalty(hVol.h);
    DescribeDevice(hVol.h, r.volumeBus, r.volumeModel);

    r.diskNumber = GetPhysicalDriveNumber(hVol.h);
    if (r.diskNumber >= 0) {
        Handle hPhys(L"\\\\.\\PhysicalDrive" + text::Widen(std::to_string(r.diskNumber)));
        if (hPhys.valid()) {
            r.physicalOpened = true;
            r.physicalSeekPenalty = GetSeekPenalty(hPhys.h);
        }
        r.friendlyName = GetFriendlyNameFromRegistry(r.diskNumber);
    }
    if (r.friendlyName.empty()) r.friendlyName = r.volumeModel;
    return r;
}

#else // POSIX: /proc/self/mountinfo + /sys/dev/block/<maj>:<min>/queue/rotational

std::string Unescape(const std::string& s) { // mountinfo escapes space, tab, \n and \ as \ooo
    std::string r;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 3 < s.size()) {
            const std::string oct = s.substr(i + 1, 3);
            if (oct.find_first_not_of("01234567") == std::string::npos) {
                r.push_back(static_cast<char>(std::stoi(oct, nullptr, 8)));
                i += 3;
                continue;
            }
        }
        r.push_back(s[i]);
    }
    return r;
}

std::string RealPath(const std::string& p) {
    char buf[PATH_MAX];
    return realpath(p.c_str(), buf) ? std::string(buf) : std::string();
}

VolumeInfo IdentifyVolumeImpl(const std::string& p) {
    VolumeInfo v;
    v.probePath = path::GetFullPath(p);
    if (v.probePath.empty()) return v;
    struct stat st;
    if (stat(v.probePath.c_str(), &st) != 0) return v;
    const std::string majmin = std::to_string(major(st.st_dev)) + ":" + std::to_string(minor(st.st_dev));

    std::string source;
    std::ifstream in("/proc/self/mountinfo");
    std::string line;
    std::size_t best = 0;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string id, parent, mm, root, mount;
        ls >> id >> parent >> mm >> root >> mount;
        if (mm != majmin) continue;
        mount = Unescape(mount);
        if (!(v.probePath == mount || v.probePath.starts_with(mount == "/" ? mount : mount + "/"))) continue;
        if (mount.size() < best && !v.mountPoint.empty()) continue;
        best = mount.size();
        v.mountPoint = mount;
        std::string tok;
        while (ls >> tok && tok != "-") {
        }
        ls >> v.fileSystem >> source;
        source = Unescape(source);
    }
    if (!source.empty() && source[0] == '/') {
        const std::string dev = RealPath(source);
        if (DIR* d = opendir("/dev/disk/by-uuid")) {
            while (dirent* e = readdir(d)) {
                if (e->d_name[0] == '.') continue;
                if (!dev.empty() && RealPath(std::string("/dev/disk/by-uuid/") + e->d_name) == dev) {
                    v.guid = e->d_name;
                    break;
                }
            }
            closedir(d);
        }
    }
    struct statfs sfs;
    if (statfs(v.probePath.c_str(), &sfs) == 0) {
        const auto t = static_cast<unsigned long>(sfs.f_type);
        // NFS, SMB, SMB2, CIFS, FUSE (sshfs etc.)
        v.network = t == 0x6969 || t == 0x517B || t == 0xFE534D42 || t == 0xFF534D42 || t == 0x65735546;
        int fsid[2];
        std::memcpy(fsid, &sfs.f_fsid, sizeof fsid);
        char buf[24];
        std::snprintf(buf, sizeof buf, "%08X-%08X", static_cast<unsigned>(fsid[0]), static_cast<unsigned>(fsid[1]));
        if (fsid[0] || fsid[1]) v.serial = buf;
    }
    v.driveType = v.network ? "Network" : "Fixed";
    v.devicePath = "/sys/dev/block/" + majmin;
    return v;
}

ProbeResult Probe(const VolumeInfo& v) {
    ProbeResult r;
    if (v.network || v.devicePath.empty()) return r;
    for (const std::string& f : {v.devicePath + "/queue/rotational", v.devicePath + "/../queue/rotational"}) {
        std::ifstream in(f);
        char c = 0;
        if (in >> c) {
            r.volumeOpened = true;
            r.volumeSeekPenalty = c == '1' ? 1 : 0;
            break;
        }
    }
    if (!r.volumeOpened) r.error = "No rotational flag in " + v.devicePath + ".";
    return r;
}

#endif

} // namespace

VolumeInfo IdentifyVolume(const std::string& p) { return IdentifyVolumeImpl(p); }

Detection Detect(const VolumeInfo& v) {
    if (v.probePath.empty()) return Detection{true, true, "path could not be resolved (defaulted to HDD)"};
    return Decide(v, Probe(v));
}

bool IsRotational(const std::string& p) { return Detect(IdentifyVolume(p)).isHdd; }

std::string GetDebugInfo(const std::string& p) {
    const VolumeInfo v = IdentifyVolume(p);
    const ProbeResult r = v.probePath.empty() ? ProbeResult{} : Probe(v);
    const Detection d = v.probePath.empty() ? Detect(v) : Decide(v, r);
    auto pen = [](int x) { return x < 0 ? std::string("n/a") : std::to_string(x); };
    auto orNA = [](const std::string& s) { return s.empty() ? std::string("n/a") : s; };
    std::string s;
    auto line = [&](const std::string& l) { s += l + "\r\n"; };
    line("Probing path: " + p);
    line("Mount point: " + orNA(v.mountPoint) + "   Drive type: " + v.driveType);
    line("Volume GUID: " + orNA(v.guid) + "   Serial: " + orNA(v.serial) + "   File system: " + orNA(v.fileSystem) +
         "   Label: '" + v.label + "'");
    line("Device: " + orNA(v.devicePath));
    if (r.volumeOpened) {
        line("[Volume IOCTL] Bus: " + orNA(r.volumeBus) + ", Model: '" + r.volumeModel +
             "', SeekPenalty: " + pen(r.volumeSeekPenalty));
    }
    if (r.diskNumber >= 0) {
        line("Mapped to PhysicalDisk" + std::to_string(r.diskNumber));
        line(r.physicalOpened ? "[Physical IOCTL] SeekPenalty: " + pen(r.physicalSeekPenalty)
                              : std::string("[Physical IOCTL] Access Denied (Requires Admin)"));
    } else if (r.volumeOpened) {
        line("Could not map to a physical disk (typical for encrypted/virtual volumes).");
    }
    if (!r.friendlyName.empty()) line("[Registry/Model] Name: '" + r.friendlyName + "'");
    if (!r.error.empty()) line("Note: " + r.error);
    line("");
    line(std::string(">>> DETECTION: ") + (d.isHdd ? "HDD Mode (Sequential)" : "SSD Mode (Parallel)") + " - " + d.method);
    return s;
}

} // namespace sfv::drive
