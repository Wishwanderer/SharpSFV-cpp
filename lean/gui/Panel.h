// SharpSFV (lean C++ rewrite) - colored container window (the WinForms Panel the original
// layout is built from). Port of src/gui/Panel.*: children get the panel's background, labels
// can have their own text color, and WM_COMMAND / WM_NOTIFY go on to the panel's parent.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "Win32Util.h"

namespace sfv::gui {

HWND CreatePanel(HWND parent, COLORREF background);
void SetPanelBackground(HWND panel, COLORREF background);
// Text color for a static/check box/read-only edit on the panel.
void SetChildTextColor(HWND panel, HWND child, COLORREF color);

} // namespace sfv::gui
