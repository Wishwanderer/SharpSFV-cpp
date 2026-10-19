// SharpSFV (lean C++ rewrite) - modal dialogs. Port of src/gui/Dialogs.*:
//   InputDialog   <- SimpleInputDialog.cs (rename)
//   TextDialog    <- ShowDiskDebugInfo (Form1.Actions.cs)
//   AboutDialog   <- ShowCredits (Form1.Actions.cs), plus the GPL notice
//   DrivesDialog  <- view and change DriveType.ini (per-drive SSD/HDD)
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "../core/Settings.h"
#include "Win32Util.h"

namespace sfv::gui {

Opt<Str> InputDialog(HWND owner, StrView caption, StrView prompt, StrView value);
void TextDialog(HWND owner, StrView caption, StrView text);
void AboutDialog(HWND owner);
// `settings` holds the data-storage consent (the dialog can ask for it).
void DrivesDialog(HWND owner, AppSettings& settings);

} // namespace sfv::gui
