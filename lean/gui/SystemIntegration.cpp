// SharpSFV (lean C++ rewrite) - shell integration, taskbar progress, command-line install
// (see SystemIntegration.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "SystemIntegration.h"

#include "../base/FileSystem.h"
#include "../base/Path.h"
#include "../core/Scripts.h"

#include <shlobj.h>

namespace sfv::gui {

namespace {

constexpr const char* kClasses = "Software\\Classes\\";

bool RegSet(HKEY root, StrView key, const wchar_t* name, StrView value, DWORD type = REG_SZ) {
    HKEY h;
    if (RegCreateKeyExW(root, W(key).C(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) != ERROR_SUCCESS)
        return false;
    const text::WStr v = W(value);
    const LONG r = RegSetValueExW(h, name, 0, type, reinterpret_cast<const BYTE*>(v.C()),
                                  static_cast<DWORD>((v.Size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

struct RegString {
    Str value;
    DWORD type = REG_SZ;
};

Opt<RegString> RegGet(HKEY root, StrView key, const wchar_t* name) {
    HKEY h;
    if (RegOpenKeyExW(root, W(key).C(), 0, KEY_QUERY_VALUE, &h) != ERROR_SUCCESS) return {};
    DWORD type = 0, size = 0;
    Opt<RegString> out;
    if (RegQueryValueExW(h, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        text::WStr buf;
        buf.Resize(size / sizeof(wchar_t) + 1);
        if (RegQueryValueExW(h, name, nullptr, &type, reinterpret_cast<BYTE*>(buf.Data()), &size) == ERROR_SUCCESS) {
            RegString r;
            r.value = N(buf.C());
            r.type = type;
            out = r;
        }
    }
    RegCloseKey(h);
    return out;
}

void NotifyAssociationsChanged() { SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); }

void BroadcastEnvironmentChange() {
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"), SMTO_ABORTIFHUNG,
                        3000, &result);
}

Str LastErrorText(StrView what) { return Str(what) + " (error " + ToStr(static_cast<i64>(GetLastError())) + ")"; }

Str Expand(StrView s) { return ExpandEnv(s); }

bool SameFile(StrView a, StrView b) { return text::EqualsIgnoreCase(path::GetFullPath(a), path::GetFullPath(b)); }

} // namespace

// ------------------------------------------------------------------ Explorer

bool RegisterShell(Str* error) {
    const Str exe = ModulePath();
    const Str verify = "\"" + exe + "\" \"%1\"";
    const Str create = "\"" + exe + "\" -create \"%1\"";
    bool ok = true;
    static const char* const kTargets[] = {"Directory\\shell\\SharpSFV", "*\\shell\\SharpSFV"};
    for (const char* target : kTargets) {
        const Str key = Str(kClasses) + target;
        ok &= RegSet(HKEY_CURRENT_USER, key, nullptr, "SharpSFV: Create Checksum");
        ok &= RegSet(HKEY_CURRENT_USER, key, L"Icon", exe);
        ok &= RegSet(HKEY_CURRENT_USER, key, L"MultiSelectModel", "Player");
        ok &= RegSet(HKEY_CURRENT_USER, key + "\\command", nullptr, create);
    }
    ok &= RegSet(HKEY_CURRENT_USER, Str(kClasses) + ".sfv", nullptr, "SharpSFV.File");
    ok &= RegSet(HKEY_CURRENT_USER, Str(kClasses) + "SharpSFV.File", nullptr, "SFV Checksum File");
    ok &= RegSet(HKEY_CURRENT_USER, Str(kClasses) + "SharpSFV.File\\shell\\open\\command", nullptr, verify);
    ok &= RegSet(HKEY_CURRENT_USER, Str(kClasses) + "SharpSFV.File\\DefaultIcon", nullptr, exe + ",0");
    NotifyAssociationsChanged();
    if (!ok && error) *error = "Some registry keys could not be written.";
    return ok;
}

bool UnregisterShell(Str* error) {
    static const char* const kKeys[] = {"Directory\\shell\\SharpSFV", "*\\shell\\SharpSFV", "SharpSFV.File"};
    for (const char* key : kKeys) {
        const LONG r = RegDeleteTreeW(HKEY_CURRENT_USER, W(Str(kClasses) + key).C());
        if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND) {
            if (error) *error = Str("Could not delete HKCU\\Software\\Classes\\") + key;
            return false;
        }
    }
    // The original left .sfv pointing at the deleted SharpSFV.File; clear it if it is ours.
    if (Opt<RegString> v = RegGet(HKEY_CURRENT_USER, Str(kClasses) + ".sfv", nullptr); v && v->value == "SharpSFV.File") {
        HKEY h;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, W(Str(kClasses) + ".sfv").C(), 0, KEY_SET_VALUE, &h) == ERROR_SUCCESS) {
            RegDeleteValueW(h, nullptr);
            RegCloseKey(h);
        }
    }
    NotifyAssociationsChanged();
    return true;
}

// ------------------------------------------------------------------ Command-line install

Str InstallDirectory() {
    const Str base = fs::LocalAppDataDirectory();
    return base.Empty() ? Str() : path::Combine(base, "SharpSFV");
}

bool IsInstalledForCommandLine() {
    const Str dir = InstallDirectory();
    if (dir.Empty() || !fs::FileExists(path::Combine(dir, "SharpSFV.exe"))) return false;
    const Opt<RegString> p = RegGet(HKEY_CURRENT_USER, "Environment", L"Path");
    return p && PathListContains(p->value, dir, Expand);
}

bool InstallForCommandLine(Vec<Str>* copied, Str* error) {
    const Str dir = InstallDirectory();
    if (dir.Empty() || !fs::CreateDirectories(dir)) {
        if (error) *error = "Could not create " + dir;
        return false;
    }
    const Str self = ModulePath();
    const Str selfDir = path::GetDirectoryName(self).ValueOr(Str());
    static const char* const kNames[] = {"SharpSFV.exe", "SharpSFV-cli.exe"};
    for (const char* name : kNames) {
        const Str src = Str(name) == "SharpSFV.exe" ? self : path::Combine(selfDir, name);
        const Str dst = path::Combine(dir, name);
        if (!fs::FileExists(src) || SameFile(src, dst)) continue;
        if (!CopyFileW(W(src).C(), W(dst).C(), FALSE)) {
            if (error) *error = LastErrorText("Could not copy " + src);
            return false;
        }
        if (copied) copied->Push(dst);
    }
    // The installed copy keeps its settings next to itself; start it with this copy's settings
    // (including the local data storage decision) unless it already has its own.
    const Str iniSrc = path::Combine(selfDir, "SharpSFV.ini"), iniDst = path::Combine(dir, "SharpSFV.ini");
    if (fs::FileExists(iniSrc) && !fs::FileExists(iniDst) && !SameFile(iniSrc, iniDst) &&
        CopyFileW(W(iniSrc).C(), W(iniDst).C(), TRUE) && copied)
        copied->Push(iniDst);
    const Opt<RegString> current = RegGet(HKEY_CURRENT_USER, "Environment", L"Path");
    const Str list = current ? current->value : Str();
    const Str updated = PathListAdd(list, dir, Expand);
    if (updated != list) {
        const DWORD type = current ? current->type : REG_EXPAND_SZ; // keep REG_EXPAND_SZ as it was
        if (!RegSet(HKEY_CURRENT_USER, "Environment", L"Path", updated, type)) {
            if (error) *error = "Could not update the user PATH.";
            return false;
        }
        BroadcastEnvironmentChange();
    }
    return true;
}

bool UninstallForCommandLine(Vec<Str>* removed, Str* error) {
    const Str dir = InstallDirectory();
    if (dir.Empty()) return true;
    if (const Opt<RegString> current = RegGet(HKEY_CURRENT_USER, "Environment", L"Path")) {
        const Str& list = current->value;
        const Str updated = PathListRemove(list, dir, Expand);
        if (updated != list) {
            // Nothing left (typically: the value was created by Install): remove it entirely.
            const bool ok = updated.Empty() ? RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Environment", L"Path") == ERROR_SUCCESS
                                            : RegSet(HKEY_CURRENT_USER, "Environment", L"Path", updated, current->type);
            if (!ok) {
                if (error) *error = "Could not update the user PATH.";
                return false;
            }
            BroadcastEnvironmentChange();
        }
    }
    const Str self = ModulePath();
    static const char* const kNames[] = {"SharpSFV.exe", "SharpSFV-cli.exe", "SharpSFV.ini"};
    for (const char* name : kNames) {
        const Str f = path::Combine(dir, name);
        if (!fs::FileExists(f)) continue;
        if (SameFile(f, self)) {
            if (error) *error = "This copy is running from " + dir + "; close it and delete SharpSFV.exe there.";
            continue;
        }
        if (fs::RemoveFile(f) && removed) removed->Push(f);
    }
    fs::RemoveDirectoryIfEmpty(dir);
    return true;
}

// ------------------------------------------------------------------ Taskbar

// CLSID_TaskbarList, defined locally (the SDK's copy lives in MinGW's large libuuid).
static const CLSID kClsidTaskbarList = {0x56FDF344, 0xFD6D, 0x11D0, {0x95, 0x8A, 0x00, 0x60, 0x97, 0xC9, 0xA0, 0x90}};

UINT TaskbarProgress::ButtonCreatedMessage() {
    static UINT msg = 0;
    if (!msg) msg = RegisterWindowMessageW(L"TaskbarButtonCreated");
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
