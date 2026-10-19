// SharpSFV (C++ port) - small Win32 helpers used by the GUI.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Win32Util.h"

#include "PathUtil.h"
#include "Text.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace sfv::gui {

std::wstring W(std::string_view s) { return text::Widen(s); }
std::string N(std::wstring_view s) { return text::Narrow(s); }

UINT DpiOf(HWND hwnd) {
    const UINT d = hwnd ? GetDpiForWindow(hwnd) : 0;
    if (d) return d;
    HDC dc = GetDC(nullptr);
    const UINT sys = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSY));
    ReleaseDC(nullptr, dc);
    return sys ? sys : 96;
}

namespace {
LOGFONTW MessageFont(UINT dpi) {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof ncm;
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, dpi)) return ncm.lfMessageFont;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    LOGFONTW lf = ncm.lfMessageFont;
    lf.lfHeight = MulDiv(lf.lfHeight, static_cast<int>(dpi), static_cast<int>(DpiOf(nullptr)));
    return lf;
}
} // namespace

HFONT CreateUiFont(UINT dpi, bool bold, bool strike) {
    LOGFONTW lf = MessageFont(dpi);
    if (bold) lf.lfWeight = FW_BOLD;
    lf.lfStrikeOut = strike ? TRUE : FALSE;
    return CreateFontIndirectW(&lf);
}

HFONT CreateNamedFont(UINT dpi, const wchar_t* face, int pointSize) {
    LOGFONTW lf = MessageFont(dpi);
    lf.lfHeight = -MulDiv(pointSize, static_cast<int>(dpi), 72);
    lf.lfWeight = FW_NORMAL;
    wcsncpy_s(lf.lfFaceName, face, LF_FACESIZE - 1);
    return CreateFontIndirectW(&lf);
}

int MeasureTextWidth(HWND hwnd, HFONT font, const std::wstring& s) {
    HDC dc = GetDC(hwnd);
    HGDIOBJ old = SelectObject(dc, font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &sz);
    SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    return sz.cx;
}

std::string GetWindowTextUtf8(HWND hwnd) {
    const int n = GetWindowTextLengthW(hwnd);
    std::wstring w(static_cast<std::size_t>(n) + 1, L'\0');
    GetWindowTextW(hwnd, w.data(), n + 1);
    w.resize(static_cast<std::size_t>(n));
    return N(w);
}

void SetWindowTextUtf8(HWND hwnd, std::string_view s) { SetWindowTextW(hwnd, W(s).c_str()); }

std::optional<std::string> GetClipboardText(HWND owner) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(owner)) return std::nullopt;
    std::optional<std::string> result;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            result = N(p);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return result;
}

bool SetClipboardText(HWND owner, std::string_view s) {
    const std::wstring w = W(s);
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    bool ok = false;
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t))) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(h);
            ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
        }
        if (!ok) GlobalFree(h);
    }
    CloseClipboard();
    return ok;
}

namespace {
std::wstring FilterString(std::string_view filter) {
    std::wstring f = W(filter);
    for (wchar_t& c : f)
        if (c == L'|') c = L'\0';
    f.push_back(L'\0');
    f.push_back(L'\0');
    return f;
}
// Extension (without dot) of the first pattern of the filter's first entry: "*.sfv" -> "sfv".
std::wstring FirstExtension(std::string_view filter) {
    const auto parts = text::Split(filter, "|", false);
    if (parts.size() < 2) return {};
    const std::string& pat = parts[1];
    const auto dot = pat.find("*.");
    if (dot == std::string::npos) return {};
    std::string ext = pat.substr(dot + 2);
    ext = ext.substr(0, ext.find(';'));
    return ext == "*" ? std::wstring() : W(ext);
}
} // namespace

std::optional<std::string> OpenFileDialog(HWND owner, std::string_view filter, const std::string& initialDir) {
    std::wstring buf(32768, L'\0');
    const std::wstring f = FilterString(filter), dir = W(initialDir);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = f.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = static_cast<DWORD>(buf.size());
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return std::nullopt;
    return N(buf.c_str());
}

std::optional<SaveResult> SaveFileDialog(HWND owner, std::string_view filter, const std::string& fileName,
                                         const std::string& initialDir, int filterIndex, const std::string& title) {
    std::wstring buf(32768, L'\0');
    const std::wstring name = W(fileName);
    wcsncpy_s(buf.data(), buf.size(), name.c_str(), _TRUNCATE);
    const std::wstring f = FilterString(filter), dir = W(initialDir), ext = FirstExtension(filter), t = W(title);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = f.c_str();
    ofn.nFilterIndex = static_cast<DWORD>(filterIndex);
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = static_cast<DWORD>(buf.size());
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrDefExt = ext.empty() ? nullptr : ext.c_str(); // the dialog uses the chosen filter's extension
    ofn.lpstrTitle = t.empty() ? nullptr : t.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return std::nullopt;
    return SaveResult{N(buf.c_str()), static_cast<int>(ofn.nFilterIndex)};
}

// CLSID_FileOpenDialog, defined locally: the SDK's copy lives in MinGW's 48 KB libuuid table.
static const CLSID kClsidFileOpenDialog = {0xDC1C5A9C, 0xE88A, 0x4DDE, {0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7}};

std::optional<std::string> PickFolder(HWND owner, const std::string& title) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(kClsidFileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return std::nullopt;
    std::optional<std::string> result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(W(title).c_str());
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                result = N(p);
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

int MsgBox(HWND owner, std::string_view textUtf8, std::string_view caption, UINT flags) {
    return MessageBoxW(owner, W(textUtf8).c_str(), W(caption).c_str(), flags);
}

void ShowInExplorer(const std::string& p) {
    const std::wstring wp = W(p);
    if (PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(wp.c_str())) {
        const HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
        if (SUCCEEDED(hr)) return;
    }
    const std::wstring args = L"/select,\"" + wp + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void OpenUrl(const std::string& url) { ShellExecuteW(nullptr, L"open", W(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

std::string ModulePath() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            return N(buf);
        }
        buf.resize(buf.size() * 2);
    }
}

bool RestartSelf() {
    std::wstring cmd = L"\"" + W(ModulePath()) + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

std::string ExpandEnv(std::string_view s) {
    const std::wstring in = W(s);
    const DWORD n = ExpandEnvironmentStringsW(in.c_str(), nullptr, 0);
    if (n == 0) return std::string(s);
    std::wstring out(n, L'\0');
    ExpandEnvironmentStringsW(in.c_str(), out.data(), n);
    return N(out.c_str());
}

} // namespace sfv::gui
