// SharpSFV (C++ port) - modal dialogs.
//   InputDialog   <- SimpleInputDialog.cs (rename)
//   TextDialog    <- ShowDiskDebugInfo (Form1.Actions.cs)
//   AboutDialog   <- ShowCredits (Form1.Actions.cs), plus the GPL notice
//   DrivesDialog  <- new: view and change DriveType.ini (per-drive SSD/HDD)
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "AppSettings.h"
#include "Win32Util.h"

#include <optional>
#include <string>

namespace sfv::gui {

std::optional<std::string> InputDialog(HWND owner, const std::string& caption, const std::string& prompt,
                                       const std::string& value);
void TextDialog(HWND owner, const std::string& caption, const std::string& text);
void AboutDialog(HWND owner);
// `settings` holds the data-storage consent (the dialog can ask for it).
void DrivesDialog(HWND owner, AppSettings& settings);

} // namespace sfv::gui
