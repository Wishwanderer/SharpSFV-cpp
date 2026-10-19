// SharpSFV (C++ port) - Windows shell integration, taskbar progress, command-line install.
//
//   Explorer context menu   <- PerformRegisterShell / PerformUnregisterShell (Form1.Actions.cs)
//   Taskbar progress        <- ITaskbarList3 COM import (Interop/Win32Storage.cs)
//   Command-line install    <- Utils/SelfInstaller.cs, now an explicit, reversible action.
//
// The original self-installer ran on every start, silently: it copied SharpSFV.exe into
// %LOCALAPPDATA%\SharpSFV and appended that folder to the user's PATH. Here it only happens
// when the user chooses Options > System Integration > Install for Command Line, which
// explains what it does first; "Uninstall" removes the PATH entry and the copies. PATH is
// written back with its original registry type (REG_EXPAND_SZ stays REG_EXPAND_SZ;
// .NET's Environment.SetEnvironmentVariable rewrites it as REG_SZ, which stops %VAR%
// references in an existing PATH from expanding).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "Win32Util.h"

#include <shobjidl.h>

#include <string>
#include <vector>

namespace sfv::gui {

// --- Explorer (HKCU, no elevation needed)
bool RegisterShell(std::string* error);
bool UnregisterShell(std::string* error);

// --- Command-line install
std::string InstallDirectory(); // %LOCALAPPDATA%\SharpSFV
bool IsInstalledForCommandLine();
// Copies SharpSFV.exe (and SharpSFV-cli.exe, if next to it) and adds the folder to PATH.
bool InstallForCommandLine(std::vector<std::string>* copied, std::string* error);
// Removes the folder from PATH and deletes the copies (not while running from there).
bool UninstallForCommandLine(std::vector<std::string>* removed, std::string* error);

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
