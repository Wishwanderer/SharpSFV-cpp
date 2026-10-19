// SharpSFV (C++ port) - small Win32 helpers used by the GUI.
// Replaces the WinForms conveniences the original relied on (Clipboard, OpenFileDialog,
// SaveFileDialog, MessageBox, Process.Start("explorer.exe /select"), Application.Restart).
// Strings cross this boundary as UTF-8 std::string.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sfv::gui {

std::wstring W(std::string_view s);
std::string N(std::wstring_view s);

inline int Scale(int v, UINT dpi) { return MulDiv(v, static_cast<int>(dpi), 96); }
UINT DpiOf(HWND hwnd);

// Message font for a DPI ("Segoe UI 9"), optionally bold/strike-out, or a named face/size.
HFONT CreateUiFont(UINT dpi, bool bold = false, bool strike = false);
HFONT CreateNamedFont(UINT dpi, const wchar_t* face, int pointSize);

// Width in pixels of `s` drawn with `font` (TextRenderer.MeasureText).
int MeasureTextWidth(HWND hwnd, HFONT font, const std::wstring& s);

std::string GetWindowTextUtf8(HWND hwnd);
void SetWindowTextUtf8(HWND hwnd, std::string_view s);

// Clipboard (CF_UNICODETEXT).
std::optional<std::string> GetClipboardText(HWND owner);
bool SetClipboardText(HWND owner, std::string_view s);

// WinForms-style filter: "Desc (*.a)|*.a|All Files (*.*)|*.*". filterIndex is 1-based.
std::optional<std::string> OpenFileDialog(HWND owner, std::string_view filter, const std::string& initialDir);
struct SaveResult {
    std::string path;
    int filterIndex = 1;
};
std::optional<SaveResult> SaveFileDialog(HWND owner, std::string_view filter, const std::string& fileName,
                                         const std::string& initialDir, int filterIndex = 1,
                                         const std::string& title = {});
std::optional<std::string> PickFolder(HWND owner, const std::string& title);

int MsgBox(HWND owner, std::string_view text, std::string_view caption, UINT flags);

// Opens Explorer with the file selected (explorer.exe /select,"path").
void ShowInExplorer(const std::string& path);
void OpenUrl(const std::string& url);

// Starts a new instance of this program (no arguments), for Application.Restart().
bool RestartSelf();

// ExpandEnvironmentStrings.
std::string ExpandEnv(std::string_view s);
std::string ModulePath();

} // namespace sfv::gui
