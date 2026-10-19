// SharpSFV (lean C++ rewrite) - Windows shell integration, taskbar progress, command-line
// install. Port of src/gui/SystemIntegration.*: the Explorer context menu (HKCU), the
// taskbar button's progress (ITaskbarList3), and the explicit, reversible "Install for
// Command Line" (copies to %LOCALAPPDATA%\SharpSFV and edits the user PATH, keeping its
// registry type) that replaces the original's silent self-installer.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "Win32Util.h"

#include <shobjidl.h>

namespace sfv::gui {

// --- Explorer (HKCU, no elevation needed)
bool RegisterShell(Str* error);
bool UnregisterShell(Str* error);

// --- Command-line install
Str InstallDirectory(); // %LOCALAPPDATA%\SharpSFV
bool IsInstalledForCommandLine();
// Copies SharpSFV.exe (and SharpSFV-cli.exe, if next to it) and adds the folder to PATH.
bool InstallForCommandLine(Vec<Str>* copied, Str* error);
// Removes the folder from PATH and deletes the copies (not while running from there).
bool UninstallForCommandLine(Vec<Str>* removed, Str* error);

// --- Taskbar button progress (green / red / yellow)
class TaskbarProgress {
public:
    TaskbarProgress() = default;
    ~TaskbarProgress();
    TaskbarProgress(const TaskbarProgress&) = delete;
    TaskbarProgress& operator=(const TaskbarProgress&) = delete;

    static UINT ButtonCreatedMessage(); // "TaskbarButtonCreated": call Attach when it arrives
    void Attach(HWND hwnd);
    void SetValue(ULONGLONG done, ULONGLONG total);
    void SetState(TBPFLAG state);

private:
    ITaskbarList3* list_ = nullptr;
    HWND hwnd_ = nullptr;
};

} // namespace sfv::gui
