// SharpSFV (lean C++ rewrite) - GUI identifiers: private window messages, menu commands,
// controls. Same values as src/gui/App.h.
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

// Posted by worker threads (never sent, so the engine never waits for the UI). Each is
// coalesced: the data lives in MainWindow members and the message only says "look".
enum : UINT {
    WM_APP_PROGRESS = WM_APP + 1, // throttled progress snapshot ready
    WM_APP_DISCOVERED,            // new store indices to show (creation)
    WM_APP_LARGEFILES,            // "Active Jobs" panel changed (files > 50 MB)
    WM_APP_JOBS_CHANGED,          // job queue rows changed
    WM_APP_VERIFY_LOADED,         // checksum file parsed, entries in the store
    WM_APP_DRIVE_DECIDED,         // HDD/SSD decision for this run
    WM_APP_RUN_DONE,              // worker finished
    WM_APP_PIPE_PATHS,            // create mode: paths from other instances
    WM_APP_STARTUP,               // first message after the window is shown
};

enum : UINT {
    TIMER_FILTER = 1,   // search box debounce (300 ms)
    TIMER_CREATE = 2,   // create mode: wait for more paths (200 ms)
};

// Menu and context-menu commands (kept clear of IDOK/IDCANCEL).
enum : UINT {
    ID_FILE_OPEN = 40001,
    ID_FILE_SAVE,
    ID_FILE_EXIT,
    ID_EDIT_COPY,
    ID_EDIT_PASTE,
    ID_EDIT_COMPARE,
    ID_EDIT_SELECTALL,
    ID_MODE_STANDARD,
    ID_MODE_JOB,
    ID_MODE_CLEARDONE,
    ID_VIEW_HASH,
    ID_VIEW_EXPECTED,
    ID_VIEW_TIME,
    ID_VIEW_THROUGHPUT,
    ID_VIEW_FULLPATHS,
    ID_VIEW_LOCKCOLS,
    ID_OPT_PATH_REL,
    ID_OPT_PATH_ABS,
    ID_OPT_ADVANCED,
    ID_OPT_FILTER,
    ID_OPT_PROC_AUTO,
    ID_OPT_PROC_HDD,
    ID_OPT_PROC_SSD,
    ID_OPT_DRIVES,
    ID_OPT_ALGO_FIRST, // + HashType value (5 items)
    ID_OPT_ALGO_LAST = ID_OPT_ALGO_FIRST + 4,
    ID_OPT_CRC_STD,
    ID_OPT_CRC_REV,
    ID_OPT_DATASTORAGE,
    ID_OPT_SHELL_REG,
    ID_OPT_SHELL_UNREG,
    ID_OPT_INSTALL,
    ID_OPT_UNINSTALL,
    ID_OPT_BAD_MOVE,
    ID_OPT_BAD_RENAME,
    ID_OPT_BAD_SCRIPT,
    ID_OPT_DUP_COPY,
    ID_OPT_DUP_DELETE,
    ID_HELP_DISK,
    ID_HELP_RESET,
    ID_HELP_CREDITS,
    ID_CTX_OPENFOLDER,
    ID_CTX_COPY,
    ID_CTX_RENAME,
    ID_CTX_DELETE,
    ID_CTX_REMOVE,
};

// Child control IDs.
enum : UINT {
    IDC_LIST = 100,
    IDC_ACTIVE,
    IDC_PROGRESS,
    IDC_PROGRESS_FILE,
    IDC_STOP,
    IDC_PAUSE,
    IDC_FILTER_TEXT,
    IDC_FILTER_STATUS,
    IDC_FILTER_DUPS,
    IDC_ADV_PREFIX,
    IDC_ADV_INCLUDE,
    IDC_ADV_EXCLUDE,
    IDC_ADV_RECURSIVE,
    IDC_ADV_COMMENTS,
    IDC_COMMENTS,
};

} // namespace sfv::gui
