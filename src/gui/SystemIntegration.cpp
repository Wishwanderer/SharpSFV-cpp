// SharpSFV (C++ port) - Windows shell integration, taskbar progress, command-line install.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "SystemIntegration.h"

#include "EnvPath.h"
#include "FileSystem.h"
#include "PathUtil.h"
#include "Text.h"

#include <shlobj.h>

#include <optional>

namespace sfv::gui {

namespace {

const std::wstring kClasses = L"Software\\Classes\\";

bool RegSet(HKEY root, const std::wstring& key, const wchar_t* name, const std::wstring& value, DWORD type = REG_SZ) {
    HKEY h;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) != ERROR_SUCCESS)
        return false;
    const LONG r = RegSetValueExW(h, name, 0, type, reinterpret_cast<const BYTE*>(value.c_str()),
                                  static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

struct RegString {
    std::wstring value;
    DWORD type = REG_SZ;
};

std::optional<RegString> RegGet(HKEY root, const std::wstring& key, const wchar_t* name) {
    HKEY h;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_QUERY_VALUE, &h) != ERROR_SUCCESS) return std::nullopt;
    DWORD type = 0, size = 0;
    std::optional<RegString> out;
    if (RegQueryValueExW(h, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ)) {
        std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
        if (RegQueryValueExW(h, name, nullptr, &type, reinterpret_cast<BYTE*>(buf.data()), &size) == ERROR_SUCCESS)
            out = RegString{buf.c_str(), type};
    }
    RegCloseKey(h);
    return out;
}

void NotifyAssociationsChanged() { SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); }

void BroadcastEnvironmentChange() {
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"),
                        SMTO_ABORTIFHUNG, 3000, &result);
}

std::string LastErrorText(const char* what) { return std::string(what) + " (error " + std::to_string(GetLastError()) + ")"; }

} // namespace

// ------------------------------------------------------------------ Explorer

bool RegisterShell(std::string* error) {
    const std::wstring exe = W(ModulePath());
    const std::wstring verify = L"\"" + exe + L"\" \"%1\"";
    const std::wstring create = L"\"" + exe + L"\" -create \"%1\"";
    bool ok = true;
    for (const wchar_t* target : {L"Directory\\shell\\SharpSFV", L"*\\shell\\SharpSFV"}) {
        const std::wstring key = kClasses + target;
        ok &= RegSet(HKEY_CURRENT_USER, key, nullptr, L"SharpSFV: Create Checksum");
        ok &= RegSet(HKEY_CURRENT_USER, key, L"Icon", exe);
        ok &= RegSet(HKEY_CURRENT_USER, key, L"MultiSelectModel", L"Player");
        ok &= RegSet(HKEY_CURRENT_USER, key + L"\\command", nullptr, create);
    }
    ok &= RegSet(HKEY_CURRENT_USER, kClasses + L".sfv", nullptr, L"SharpSFV.File");
    ok &= RegSet(HKEY_CURRENT_USER, kClasses + L"SharpSFV.File", nullptr, L"SFV Checksum File");
    ok &= RegSet(HKEY_CURRENT_USER, kClasses + L"SharpSFV.File\\shell\\open\\command", nullptr, verify);
    ok &= RegSet(HKEY_CURRENT_USER, kClasses + L"SharpSFV.File\\DefaultIcon", nullptr, exe + L",0");
    NotifyAssociationsChanged();
    if (!ok && error) *error = "Some registry keys could not be written.";
    return ok;
}

bool UnregisterShell(std::string* error) {
    for (const wchar_t* key : {L"Directory\\shell\\SharpSFV", L"*\\shell\\SharpSFV", L"SharpSFV.File"}) {
        const LONG r = RegDeleteTreeW(HKEY_CURRENT_USER, (kClasses + key).c_str());
        if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND) {
            if (error) *error = "Could not delete HKCU\\Software\\Classes\\" + N(key);
            return false;
        }
    }
    // The original left .sfv pointing at the deleted SharpSFV.File; clear it if it is ours.
    if (auto v = RegGet(HKEY_CURRENT_USER, kClasses + L".sfv", nullptr); v && v->value == L"SharpSFV.File") {
        HKEY h;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, (kClasses + L".sfv").c_str(), 0, KEY_SET_VALUE, &h) == ERROR_SUCCESS) {
            RegDeleteValueW(h, nullptr);
            RegCloseKey(h);
        }
    }
    NotifyAssociationsChanged();
    return true;
}

// ------------------------------------------------------------------ Command-line install

std::string InstallDirectory() {
    const std::string base = fs::LocalAppDataDirectory();
    return base.empty() ? std::string() : path::Combine(base, "SharpSFV");
}

namespace {
const ExpandFn kExpand = [](std::string_view s) { return ExpandEnv(s); };

bool SameFile(const std::string& a, const std::string& b) {
    return text::EqualsIgnoreCase(path::GetFullPath(a), path::GetFullPath(b));
}
} // namespace

bool IsInstalledForCommandLine() {
    const std::string dir = InstallDirectory();
    if (dir.empty() || !fs::FileExists(path::Combine(dir, "SharpSFV.exe"))) return false;
    const auto p = RegGet(HKEY_CURRENT_USER, L"Environment", L"Path");
    return p && PathListContains(N(p->value), dir, kExpand);
}

bool InstallForCommandLine(std::vector<std::string>* copied, std::string* error) {
    const std::string dir = InstallDirectory();
    if (dir.empty() || !fs::CreateDirectories(dir)) {
        if (error) *error = "Could not create " + dir;
        return false;
    }
    const std::string self = ModulePath();
    const std::string selfDir = path::GetDirectoryName(self).value_or("");
    for (const char* name : {"SharpSFV.exe", "SharpSFV-cli.exe"}) {
        const std::string src = std::string(name) == "SharpSFV.exe" ? self : path::Combine(selfDir, name);
        const std::string dst = path::Combine(dir, name);
        if (!fs::FileExists(src) || SameFile(src, dst)) continue;
        if (!CopyFileW(W(src).c_str(), W(dst).c_str(), FALSE)) {
            if (error) *error = LastErrorText(("Could not copy " + src).c_str());
            return false;
        }
        if (copied) copied->push_back(dst);
    }
    // The installed copy keeps its settings next to itself; start it with this copy's settings
    // (including the local data storage decision) unless it already has its own.
    const std::string iniSrc = path::Combine(selfDir, "SharpSFV.ini"), iniDst = path::Combine(dir, "SharpSFV.ini");
    if (fs::FileExists(iniSrc) && !fs::FileExists(iniDst) && !SameFile(iniSrc, iniDst) &&
        CopyFileW(W(iniSrc).c_str(), W(iniDst).c_str(), TRUE) && copied)
        copied->push_back(iniDst);
    const auto current = RegGet(HKEY_CURRENT_USER, L"Environment", L"Path");
    const std::string list = current ? N(current->value) : std::string();
    const std::string updated = PathListAdd(list, dir, kExpand);
    if (updated != list) {
        const DWORD type = current ? current->type : REG_EXPAND_SZ; // keep REG_EXPAND_SZ as it was
        if (!RegSet(HKEY_CURRENT_USER, L"Environment", L"Path", W(updated), type)) {
            if (error) *error = "Could not update the user PATH.";
            return false;
        }
        BroadcastEnvironmentChange();
    }
    return true;
}

bool UninstallForCommandLine(std::vector<std::string>* removed, std::string* error) {
    const std::string dir = InstallDirectory();
    if (dir.empty()) return true;
    if (const auto current = RegGet(HKEY_CURRENT_USER, L"Environment", L"Path")) {
        const std::string list = N(current->value);
        const std::string updated = PathListRemove(list, dir, kExpand);
        if (updated != list) {
            // Nothing left (typically: the value was created by Install): remove it entirely.
            const bool ok = updated.empty()
                                ? RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Environment", L"Path") == ERROR_SUCCESS
                                : RegSet(HKEY_CURRENT_USER, L"Environment", L"Path", W(updated), current->type);
            if (!ok) {
                if (error) *error = "Could not update the user PATH.";
                return false;
            }
            BroadcastEnvironmentChange();
        }
    }
    const std::string self = ModulePath();
    for (const char* name : {"SharpSFV.exe", "SharpSFV-cli.exe", "SharpSFV.ini"}) {
        const std::string f = path::Combine(dir, name);
        if (!fs::FileExists(f)) continue;
        if (SameFile(f, self)) {
            if (error) *error = "This copy is running from " + dir + "; close it and delete SharpSFV.exe there.";
            continue;
        }
        if (fs::RemoveFile(f) && removed) removed->push_back(f);
    }
    fs::RemoveDirectoryIfEmpty(dir);
    return true;
}

// ------------------------------------------------------------------ Taskbar

// CLSID_TaskbarList, defined locally: the SDK's copy lives in MinGW's 48 KB libuuid table.
static const CLSID kClsidTaskbarList = {0x56FDF344, 0xFD6D, 0x11D0, {0x95, 0x8A, 0x00, 0x60, 0x97, 0xC9, 0xA0, 0x90}};

UINT TaskbarProgress::ButtonCreatedMessage() {
    static const UINT msg = RegisterWindowMessageW(L"TaskbarButtonCreated");
    return msg;
}

TaskbarProgress::~TaskbarProgress() {
    if (list_) list_->Release();
}

void TaskbarProgress::Attach(HWND hwnd) {
    hwnd_ = hwnd;
    if (list_) return;
    if (FAILED(CoCreateInstance(kClsidTaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&list_)))) {
        list_ = nullptr;
        return;
    }
    if (FAILED(list_->HrInit())) {
        list_->Release();
        list_ = nullptr;
    }
}

void TaskbarProgress::SetValue(ULONGLONG done, ULONGLONG total) {
    if (list_) list_->SetProgressValue(hwnd_, done, total);
}

void TaskbarProgress::SetState(TBPFLAG state) {
    if (list_) list_->SetProgressState(hwnd_, state);
}

} // namespace sfv::gui
