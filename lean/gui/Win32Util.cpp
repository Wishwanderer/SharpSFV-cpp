// SharpSFV (lean C++ rewrite) - small Win32 helpers used by the GUI (see Win32Util.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Win32Util.h"

#include "../base/Path.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace sfv::gui {

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

void CopyTruncated(wchar_t* dst, usize capacity, const wchar_t* src) {
    if (capacity == 0) return;
    usize i = 0;
    for (; i + 1 < capacity && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

bool EqualsIgnoreCaseW(const wchar_t* a, const wchar_t* b) { return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL; }

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
    CopyTruncated(lf.lfFaceName, LF_FACESIZE, face);
    return CreateFontIndirectW(&lf);
}

int MeasureTextWidth(HWND hwnd, HFONT font, const text::WStr& s) {
    HDC dc = GetDC(hwnd);
    HGDIOBJ old = SelectObject(dc, font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.C(), static_cast<int>(s.Size()), &sz);
    SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    return sz.cx;
}

Str GetWindowTextUtf8(HWND hwnd) {
    const int n = GetWindowTextLengthW(hwnd);
    text::WStr w;
    w.Resize(static_cast<usize>(n));
    GetWindowTextW(hwnd, w.Data(), n + 1);
    return text::Narrow(w.C());
}

void SetWindowTextUtf8(HWND hwnd, StrView s) { SetWindowTextW(hwnd, W(s).C()); }

Opt<Str> GetClipboardText(HWND owner) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(owner)) return {};
    Opt<Str> result;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            result = N(p);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return result;
}

bool SetClipboardText(HWND owner, StrView s) {
    const text::WStr w = W(s);
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    bool ok = false;
    const usize bytes = (w.Size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, w.C(), bytes);
            GlobalUnlock(h);
            ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
        }
        if (!ok) GlobalFree(h);
    }
    CloseClipboard();
    return ok;
}

namespace {
// "a|b|c" -> "a\0b\0c\0\0"
text::WStr FilterString(StrView filter) {
    text::WStr f = W(filter);
    for (usize i = 0; i < f.Size(); ++i)
        if (f.Data()[i] == L'|') f.Data()[i] = L'\0';
    f.Append(L'\0');
    return f; // WStr adds the final terminator
}
// Extension (without dot) of the first pattern of the filter's first entry: "*.sfv" -> "sfv".
Str FirstExtension(StrView filter) {
    const Vec<Str> parts = text::Split(filter, "|", false);
    if (parts.Size() < 2) return {};
    const Str& pat = parts[1];
    const usize dot = pat.Find("*.");
    if (dot == kNpos) return {};
    Str ext(pat.Sub(dot + 2));
    const usize semi = ext.Find(';');
    if (semi != kNpos) ext = Str(ext.Sub(0, semi));
    return ext == "*" ? Str() : ext;
}
} // namespace

Opt<Str> OpenFileDialog(HWND owner, StrView filter, StrView initialDir) {
    text::WStr buf;
    buf.Resize(32767);
    const text::WStr f = FilterString(filter), dir = W(initialDir);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = f.C();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = buf.Data();
    ofn.nMaxFile = 32768;
    ofn.lpstrInitialDir = dir.Size() == 0 ? nullptr : dir.C();
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return {};
    return N(buf.C());
}

Opt<SaveResult> SaveFileDialog(HWND owner, StrView filter, StrView fileName, StrView initialDir, int filterIndex,
                               StrView title) {
    text::WStr buf;
    buf.Resize(32767);
    CopyTruncated(buf.Data(), 32768, W(fileName).C());
    const text::WStr f = FilterString(filter), dir = W(initialDir), ext = W(FirstExtension(filter)), t = W(title);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = f.C();
    ofn.nFilterIndex = static_cast<DWORD>(filterIndex);
    ofn.lpstrFile = buf.Data();
    ofn.nMaxFile = 32768;
    ofn.lpstrInitialDir = dir.Size() == 0 ? nullptr : dir.C();
    ofn.lpstrDefExt = ext.Size() == 0 ? nullptr : ext.C(); // the dialog uses the chosen filter's extension
    ofn.lpstrTitle = t.Size() == 0 ? nullptr : t.C();
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return {};
    SaveResult r;
    r.path = N(buf.C());
    r.filterIndex = static_cast<int>(ofn.nFilterIndex);
    return r;
}

// CLSID_FileOpenDialog, defined locally (the SDK's copy lives in MinGW's large libuuid).
static const CLSID kClsidFileOpenDialog = {0xDC1C5A9C, 0xE88A, 0x4DDE, {0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7}};

Opt<Str> PickFolder(HWND owner, StrView title) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(kClsidFileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return {};
    Opt<Str> result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(W(title).C());
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

int MsgBox(HWND owner, StrView textUtf8, StrView caption, UINT flags) {
    return MessageBoxW(owner, W(textUtf8).C(), W(caption).C(), flags);
}

void ShowInExplorer(StrView p) {
    const text::WStr wp = W(p);
    if (PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(wp.C())) {
        const HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
        if (SUCCEEDED(hr)) return;
    }
    const text::WStr args = W(Str("/select,\"") + p + "\"");
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.C(), nullptr, SW_SHOWNORMAL);
}

void OpenUrl(StrView url) { ShellExecuteW(nullptr, L"open", W(url).C(), nullptr, nullptr, SW_SHOWNORMAL); }

Str ModulePath() {
    text::WStr buf;
    buf.Resize(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.Data(), static_cast<DWORD>(buf.Size() + 1));
        if (n == 0) return {};
        if (n < buf.Size()) return text::Narrow(buf.C(), n);
        buf.Resize(buf.Size() * 2);
    }
}

bool RestartSelf() {
    text::WStr cmd = W("\"" + ModulePath() + "\"");
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.Data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

Str ExpandEnv(StrView s) {
    const text::WStr in = W(s);
    const DWORD n = ExpandEnvironmentStringsW(in.C(), nullptr, 0);
    if (n == 0) return Str(s);
    text::WStr out;
    out.Resize(n);
    ExpandEnvironmentStringsW(in.C(), out.Data(), n);
    return N(out.C());
}

} // namespace sfv::gui
