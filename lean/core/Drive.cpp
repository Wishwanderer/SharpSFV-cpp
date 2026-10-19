// SharpSFV (lean C++ rewrite) - volume identification and HDD vs SSD detection (see Drive.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Drive.h"

#include "../base/FileSystem.h"
#include "../base/Path.h"
#include "../base/Text.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#endif

namespace sfv::drive {

namespace {

bool IsSsdByName(StrView name) {
    const Str n = text::FoldCase(name);
    static const char* const kKeys[] = {"SSD", "NVME", "FLASH", "M.2", "SAMSUNG"};
    for (const char* k : kKeys)
        if (n.Find(k) != kNpos) return true;
    return false;
}

// Everything the probe learned. -1 = unknown.
struct ProbeResult {
    bool volumeOpened = false;
    int volumeSeekPenalty = -1;
    Str volumeBus, volumeModel;
    int diskNumber = -1;
    bool physicalOpened = false;
    int physicalSeekPenalty = -1;
    Str friendlyName;
    Str error;
};

Detection Result(bool hdd, StrView how, bool failed = false) {
    Detection d;
    d.isHdd = hdd, d.failed = failed, d.method = how;
    return d;
}

// The decision rules, shared by Detect and GetDebugInfo so they can never disagree.
Detection Decide(const VolumeInfo& v, const ProbeResult& p) {
    if (v.network) return Result(true, "network drive");
    if (p.volumeSeekPenalty == 1) return Result(true, "volume reports a seek penalty");
    if (p.volumeSeekPenalty == 0) return Result(false, "volume reports no seek penalty");
    if (p.physicalSeekPenalty == 1) return Result(true, "physical disk reports a seek penalty");
    if (p.physicalSeekPenalty == 0) return Result(false, "physical disk reports no seek penalty");
    if (!p.friendlyName.Empty()) {
        const bool ssd = IsSsdByName(p.friendlyName);
        return Result(!ssd, "model name '" + p.friendlyName + (ssd ? "' suggests an SSD" : "' has no SSD keyword"));
    }
    return Result(true, "detection failed (defaulted to HDD)", true);
}

#ifdef _WIN32

// Local definitions (identical layout to winioctl.h).
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
    explicit Handle(StrView p)
        : h(CreateFileW(text::WStr(p).C(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr)) {}
    ~Handle() {
        if (Valid()) CloseHandle(h);
    }
    bool Valid() const { return h != INVALID_HANDLE_VALUE; }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

int GetSeekPenalty(HANDLE h) {
    PropertyQuery q{};
    q.PropertyId = kStorageDeviceSeekPenaltyProperty;
    SeekPenaltyDescriptor d{};
    DWORD ret = 0;
    if (DeviceIoControl(h, kIoctlStorageQueryProperty, &q, sizeof q, &d, sizeof d, &ret, nullptr) && ret >= sizeof d)
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
void DescribeDevice(HANDLE h, Str& bus, Str& model) {
    PropertyQuery q{};
    q.PropertyId = kStorageDeviceProperty;
    alignas(8) BYTE buf[1024] = {};
    DWORD ret = 0;
    if (!DeviceIoControl(h, kIoctlStorageQueryProperty, &q, sizeof q, buf, sizeof buf, &ret, nullptr) || ret < 28) return;
    DWORD busType, vendorOff, productOff;
    memcpy(&busType, buf + 24, 4);
    memcpy(&vendorOff, buf + 8, 4);
    memcpy(&productOff, buf + 12, 4);
    bus = BusName(busType);
    auto str = [&](DWORD off) -> Str {
        if (off == 0 || off >= ret) return {};
        const char* s = reinterpret_cast<const char*>(buf + off);
        usize n = 0;
        while (n < ret - off && s[n]) ++n;
        return Str(text::Trim(StrView(s, n)));
    };
    const Str vendor = str(vendorOff), product = str(productOff);
    model = vendor.Empty() ? product : (product.Empty() ? vendor : vendor + " " + product);
}

int GetPhysicalDriveNumber(HANDLE h) {
    // VOLUME_DISK_EXTENTS { DWORD NumberOfDiskExtents; DISK_EXTENT Extents[]; } where
    // DISK_EXTENT { DWORD DiskNumber; LARGE_INTEGER StartingOffset, ExtentLength; } (8-aligned).
    alignas(8) BYTE buf[8 + 24 * 8] = {};
    DWORD ret = 0;
    if (!DeviceIoControl(h, kIoctlVolumeGetDiskExtents, nullptr, 0, buf, sizeof buf, &ret, nullptr)) return -1;
    DWORD n, disk;
    memcpy(&n, buf, 4);
    memcpy(&disk, buf + 8, 4);
    return n > 0 ? static_cast<int>(disk) : -1;
}

// The registry functions live in advapi32.dll, loaded only while a drive is being probed.
struct Registry {
    using OpenFn = LONG(WINAPI*)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
    using QueryFn = LONG(WINAPI*)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
    using CloseFn = LONG(WINAPI*)(HKEY);
    HMODULE dll = LoadLibraryW(L"advapi32.dll");
    OpenFn open = dll ? reinterpret_cast<OpenFn>(reinterpret_cast<void*>(GetProcAddress(dll, "RegOpenKeyExW"))) : nullptr;
    QueryFn query = dll ? reinterpret_cast<QueryFn>(reinterpret_cast<void*>(GetProcAddress(dll, "RegQueryValueExW"))) : nullptr;
    CloseFn close = dll ? reinterpret_cast<CloseFn>(reinterpret_cast<void*>(GetProcAddress(dll, "RegCloseKey"))) : nullptr;
    ~Registry() {
        if (dll) FreeLibrary(dll);
    }

    Str ReadString(StrView subKey, StrView value) const {
        if (!open || !query || !close) return {};
        HKEY key;
        if (open(HKEY_LOCAL_MACHINE, text::WStr(subKey).C(), 0, KEY_READ, &key) != ERROR_SUCCESS) return {};
        const text::WStr wvalue(value);
        DWORD type = 0, size = 0;
        Str result;
        if (query(key, wvalue.C(), nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ) && size > 0) {
            text::WStr buf;
            buf.Resize(size / sizeof(wchar_t) + 1);
            if (query(key, wvalue.C(), nullptr, &type, reinterpret_cast<LPBYTE>(buf.Data()), &size) == ERROR_SUCCESS)
                result = text::Narrow(buf.C());
        }
        close(key);
        return result;
    }
};

Str GetFriendlyNameFromRegistry(int diskNumber) {
    const Registry reg;
    const Str deviceId = reg.ReadString("SYSTEM\\CurrentControlSet\\Services\\disk\\Enum", ToStr(diskNumber));
    if (deviceId.Empty()) return {};
    const Str key = "SYSTEM\\CurrentControlSet\\Enum\\" + deviceId;
    Str name = reg.ReadString(key, "FriendlyName");
    if (name.Empty()) name = reg.ReadString(key, "DeviceDesc");
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

VolumeInfo IdentifyVolumeImpl(StrView p) {
    VolumeInfo v;
    v.probePath = path::GetFullPath(p);
    if (v.probePath.Empty()) return v;

    const text::WStr wfull(v.probePath);
    wchar_t mpBuf[MAX_PATH + 1] = {};
    Str mp;
    if (GetVolumePathNameW(wfull.C(), mpBuf, MAX_PATH)) {
        mp = text::Narrow(mpBuf);
    } else {
        mp = path::GetPathRoot(v.probePath);
        if (!mp.Empty() && mp.Back() != '\\') mp += '\\';
    }
    if (mp.Empty()) return v;
    v.mountPoint = mp;
    const text::WStr wmp(mp);

    const UINT type = GetDriveTypeW(wmp.C());
    v.driveType = DriveTypeName(type);
    const bool unc = mp.Size() > 2 && mp[0] == '\\' && mp[1] == '\\' && mp[2] != '?' && mp[2] != '.';
    if (type == DRIVE_REMOTE || unc) {
        v.network = true;
        v.driveType = "Network";
        while (v.mountPoint.Size() > 3 && path::IsSep(v.mountPoint.Back())) v.mountPoint.PopBack();
        return v;
    }

    wchar_t name[64] = {};
    if (GetVolumeNameForVolumeMountPointW(wmp.C(), name, 64)) { // \\?\Volume{GUID}\ //
        const Str n = text::Narrow(name);
        const usize open = n.Find('{'), close = n.Find('}');
        if (open != kNpos && close != kNpos && close > open) {
            v.guid = n.Sub(open, close - open + 1);
            v.devicePath = "\\\\.\\Volume" + v.guid;
        }
    }
    if (v.devicePath.Empty() && v.mountPoint.Size() >= 2 && v.mountPoint[1] == ':')
        v.devicePath = "\\\\.\\" + Str(v.mountPoint.Sub(0, 2));

    wchar_t label[MAX_PATH + 1] = {}, fsName[MAX_PATH + 1] = {};
    DWORD serial = 0;
    if (GetVolumeInformationW(wmp.C(), label, MAX_PATH + 1, &serial, nullptr, nullptr, fsName, MAX_PATH + 1)) {
        if (serial != 0) { // "%04X-%04X"
            static const char kHex[] = "0123456789ABCDEF";
            Str sn;
            for (int i = 0, shift = 28; i < 9; ++i) {
                if (i == 4) {
                    sn += '-';
                    continue;
                }
                sn += kHex[(serial >> shift) & 0xF];
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
    if (v.devicePath.Empty()) {
        r.error = "No device path for this volume.";
        return r;
    }
    Handle hVol(v.devicePath);
    if (!hVol.Valid()) {
        r.error = "Could not open " + v.devicePath + " (error " + ToStr(static_cast<i64>(GetLastError())) + ").";
        return r;
    }
    r.volumeOpened = true;
    r.volumeSeekPenalty = GetSeekPenalty(hVol.h);
    DescribeDevice(hVol.h, r.volumeBus, r.volumeModel);

    r.diskNumber = GetPhysicalDriveNumber(hVol.h);
    if (r.diskNumber >= 0) {
        Handle hPhys("\\\\.\\PhysicalDrive" + ToStr(r.diskNumber));
        if (hPhys.Valid()) {
            r.physicalOpened = true;
            r.physicalSeekPenalty = GetSeekPenalty(hPhys.h);
        }
        r.friendlyName = GetFriendlyNameFromRegistry(r.diskNumber);
    }
    if (r.friendlyName.Empty()) r.friendlyName = r.volumeModel;
    return r;
}

#else // POSIX: /proc/self/mountinfo + /sys/dev/block/<maj>:<min>/queue/rotational

Str Unescape(StrView s) { // mountinfo escapes space, tab, \n and \ as \ooo
    Str r;
    for (usize i = 0; i < s.Size(); ++i) {
        if (s[i] == '\\' && i + 3 < s.Size()) {
            const char a = s[i + 1], b = s[i + 2], c = s[i + 3];
            if (a >= '0' && a <= '7' && b >= '0' && b <= '7' && c >= '0' && c <= '7') {
                r += static_cast<char>((a - '0') * 64 + (b - '0') * 8 + (c - '0'));
                i += 3;
                continue;
            }
        }
        r += s[i];
    }
    return r;
}

Str RealPath(const Str& p) {
    char buf[PATH_MAX];
    return realpath(p.CStr(), buf) ? Str(buf) : Str();
}

Vec<Str> Fields(StrView line) { // whitespace-separated, like istream >> string
    Vec<Str> out;
    usize i = 0;
    while (i < line.Size()) {
        while (i < line.Size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        const usize start = i;
        while (i < line.Size() && line[i] != ' ' && line[i] != '\t') ++i;
        if (i > start) out.Push(Str(line.Sub(start, i - start)));
    }
    return out;
}

Str Hex8(u32 v) {
    static const char kHex[] = "0123456789ABCDEF";
    Str s;
    for (int shift = 28; shift >= 0; shift -= 4) s += kHex[(v >> shift) & 0xF];
    return s;
}

VolumeInfo IdentifyVolumeImpl(StrView p) {
    VolumeInfo v;
    v.probePath = path::GetFullPath(p);
    if (v.probePath.Empty()) return v;
    struct stat st;
    if (stat(v.probePath.CStr(), &st) != 0) return v;
    const Str majmin = ToStr(static_cast<i64>(major(st.st_dev))) + ":" + ToStr(static_cast<i64>(minor(st.st_dev)));

    Str source, content;
    fs::ReadAllBytes("/proc/self/mountinfo", content);
    usize best = 0;
    for (const Str& line : text::SplitLines(content)) {
        const Vec<Str> f = Fields(line);
        if (f.Size() < 5 || f[2] != majmin) continue;
        const Str mount = Unescape(f[4]);
        if (!(v.probePath == mount || v.probePath.StartsWith(mount == "/" ? mount : mount + "/"))) continue;
        if (mount.Size() < best && !v.mountPoint.Empty()) continue;
        best = mount.Size();
        v.mountPoint = mount;
        usize k = 5;
        while (k < f.Size() && f[k] != "-") ++k;
        v.fileSystem = k + 1 < f.Size() ? f[k + 1] : Str();
        source = k + 2 < f.Size() ? Unescape(f[k + 2]) : Str();
    }
    if (!source.Empty() && source[0] == '/') {
        const Str dev = RealPath(source);
        if (DIR* d = opendir("/dev/disk/by-uuid")) {
            while (dirent* e = readdir(d)) {
                if (e->d_name[0] == '.') continue;
                if (!dev.Empty() && RealPath(Str("/dev/disk/by-uuid/") + e->d_name) == dev) {
                    v.guid = e->d_name;
                    break;
                }
            }
            closedir(d);
        }
    }
    struct statfs sfs;
    if (statfs(v.probePath.CStr(), &sfs) == 0) {
        const auto t = static_cast<unsigned long>(sfs.f_type);
        // NFS, SMB, SMB2, CIFS, FUSE (sshfs etc.)
        v.network = t == 0x6969 || t == 0x517B || t == 0xFE534D42 || t == 0xFF534D42 || t == 0x65735546;
        int fsid[2];
        memcpy(fsid, &sfs.f_fsid, sizeof fsid);
        if (fsid[0] || fsid[1]) v.serial = Hex8(static_cast<u32>(fsid[0])) + "-" + Hex8(static_cast<u32>(fsid[1]));
    }
    v.driveType = v.network ? "Network" : "Fixed";
    v.devicePath = "/sys/dev/block/" + majmin;
    return v;
}

ProbeResult Probe(const VolumeInfo& v) {
    ProbeResult r;
    if (v.network || v.devicePath.Empty()) return r;
    const Str files[] = {v.devicePath + "/queue/rotational", v.devicePath + "/../queue/rotational"};
    for (const Str& f : files) {
        Str c;
        if (fs::ReadAllBytes(f, c) && !text::Trim(c).Empty()) {
            r.volumeOpened = true;
            r.volumeSeekPenalty = text::Trim(c)[0] == '1' ? 1 : 0;
            break;
        }
    }
    if (!r.volumeOpened) r.error = "No rotational flag in " + v.devicePath + ".";
    return r;
}

#endif

} // namespace

VolumeInfo IdentifyVolume(StrView p) { return IdentifyVolumeImpl(p); }

Detection Detect(const VolumeInfo& v) {
    if (v.probePath.Empty()) return Result(true, "path could not be resolved (defaulted to HDD)", true);
    return Decide(v, Probe(v));
}

Str GetDebugInfo(StrView p) {
    const VolumeInfo v = IdentifyVolume(p);
    const ProbeResult r = v.probePath.Empty() ? ProbeResult{} : Probe(v);
    const Detection d = v.probePath.Empty() ? Detect(v) : Decide(v, r);
    auto pen = [](int x) { return x < 0 ? Str("n/a") : ToStr(x); };
    auto orNA = [](const Str& s) { return s.Empty() ? Str("n/a") : s; };
    Str s;
    auto line = [&](StrView l) {
        s += l;
        s += "\r\n";
    };
    line("Probing path: " + Str(p));
    line("Mount point: " + orNA(v.mountPoint) + "   Drive type: " + v.driveType);
    line("Volume GUID: " + orNA(v.guid) + "   Serial: " + orNA(v.serial) + "   File system: " + orNA(v.fileSystem) +
         "   Label: '" + v.label + "'");
    line("Device: " + orNA(v.devicePath));
    if (r.volumeOpened)
        line("[Volume IOCTL] Bus: " + orNA(r.volumeBus) + ", Model: '" + r.volumeModel + "', SeekPenalty: " +
             pen(r.volumeSeekPenalty));
    if (r.diskNumber >= 0) {
        line("Mapped to PhysicalDisk" + ToStr(r.diskNumber));
        line(r.physicalOpened ? "[Physical IOCTL] SeekPenalty: " + pen(r.physicalSeekPenalty)
                              : Str("[Physical IOCTL] Access Denied (Requires Admin)"));
    } else if (r.volumeOpened) {
        line("Could not map to a physical disk (typical for encrypted/virtual volumes).");
    }
    if (!r.friendlyName.Empty()) line("[Registry/Model] Name: '" + r.friendlyName + "'");
    if (!r.error.Empty()) line("Note: " + r.error);
    line("");
    line(Str(">>> DETECTION: ") + (d.isHdd ? "HDD Mode (Sequential)" : "SSD Mode (Parallel)") + " - " + d.method);
    return s;
}

} // namespace sfv::drive
