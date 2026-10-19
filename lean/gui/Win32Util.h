// SharpSFV (lean C++ rewrite) - small Win32 helpers used by the GUI.
// Port of src/gui/Win32Util.* (clipboard, file dialogs, message boxes, Explorer, fonts).
// Strings cross this boundary as UTF-8 Str.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "../base/Str.h"
#include "../base/Text.h"
#include "../base/Vec.h"

namespace sfv::gui {

inline text::WStr W(StrView s) { return text::WStr(s); }
inline Str N(const wchar_t* z) { return text::Narrow(z); }

inline int Scale(int v, UINT dpi) { return MulDiv(v, static_cast<int>(dpi), 96); }
UINT DpiOf(HWND hwnd);

// Message font for a DPI ("Segoe UI 9"), optionally bold/strike-out, or a named face/size.
HFONT CreateUiFont(UINT dpi, bool bold = false, bool strike = false);
HFONT CreateNamedFont(UINT dpi, const wchar_t* face, int pointSize);

// Width in pixels of `s` drawn with `font` (TextRenderer.MeasureText).
int MeasureTextWidth(HWND hwnd, HFONT font, const text::WStr& s);

Str GetWindowTextUtf8(HWND hwnd);
void SetWindowTextUtf8(HWND hwnd, StrView s);

// Copies a NUL-terminated wide string into a fixed buffer, truncating (wcsncpy_s _TRUNCATE).
void CopyTruncated(wchar_t* dst, usize capacity, const wchar_t* src);
// Case-insensitive comparison of two wide strings (ordinal, like _wcsicmp for class names).
bool EqualsIgnoreCaseW(const wchar_t* a, const wchar_t* b);

// Clipboard (CF_UNICODETEXT).
Opt<Str> GetClipboardText(HWND owner);
bool SetClipboardText(HWND owner, StrView s);

// WinForms-style filter: "Desc (*.a)|*.a|All Files (*.*)|*.*". filterIndex is 1-based.
Opt<Str> OpenFileDialog(HWND owner, StrView filter, StrView initialDir);
struct SaveResult {
    Str path;
    int filterIndex = 1;
};
Opt<SaveResult> SaveFileDialog(HWND owner, StrView filter, StrView fileName, StrView initialDir, int filterIndex = 1,
                               StrView title = {});
Opt<Str> PickFolder(HWND owner, StrView title);

int MsgBox(HWND owner, StrView text, StrView caption, UINT flags);

// Opens Explorer with the file selected (explorer.exe /select,"path").
void ShowInExplorer(StrView path);
void OpenUrl(StrView url);

// Starts a new instance of this program (no arguments), for Application.Restart().
bool RestartSelf();

Str ExpandEnv(StrView s); // ExpandEnvironmentStrings
Str ModulePath();

} // namespace sfv::gui
