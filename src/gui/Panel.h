// SharpSFV (C++ port) - colored container window (the WinForms Panel the original layout
// is built from). Children placed on a panel get its background; labels can have their own
// text color (the green/red/yellow OK/BAD/MISSING counters). WM_COMMAND and WM_NOTIFY from
// children are forwarded to the panel's parent, so the main window handles them as usual.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
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
