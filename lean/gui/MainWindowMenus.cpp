// SharpSFV (lean C++ rewrite) - main window: menus, accelerators, command dispatch, mode
// switching. Port of src/gui/MainWindowMenus.cpp (Form1.Menus.cs, SetAppMode /
// UpdateMenuAvailability in Form1.UI.Layout.cs).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "MainWindow.h"

#include "Dialogs.h"

#include <windowsx.h>

namespace sfv::gui {

namespace {
void Item(HMENU m, UINT id, const wchar_t* textValue) { AppendMenuW(m, MF_STRING, id, textValue); }
void Separator(HMENU m) { AppendMenuW(m, MF_SEPARATOR, 0, nullptr); }
HMENU Sub(HMENU parent, const wchar_t* textValue) {
    HMENU s = CreatePopupMenu();
    AppendMenuW(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(s), textValue);
    return s;
}
} // namespace

void MainWindow::BuildMenu() {
    menu_ = CreateMenu();

    HMENU file = Sub(menu_, L"&File");
    Item(file, ID_FILE_OPEN, L"&Open...\tCtrl+O");
    Item(file, ID_FILE_SAVE, L"&Save As...\tCtrl+S");
    Separator(file);
    Item(file, ID_FILE_EXIT, L"E&xit\tAlt+F4");

    HMENU edit = Sub(menu_, L"&Edit");
    Item(edit, ID_EDIT_COPY, L"&Copy Entry\tCtrl+C");
    Item(edit, ID_EDIT_PASTE, L"&Paste Paths\tCtrl+V");
    Item(edit, ID_EDIT_COMPARE, L"Compare with C&lipboard");
    Separator(edit);
    Item(edit, ID_EDIT_SELECTALL, L"Select &All\tCtrl+A");

    HMENU mode = Sub(menu_, L"&Mode");
    Item(mode, ID_MODE_STANDARD, L"&Standard Mode");
    Item(mode, ID_MODE_JOB, L"&Job Queue Mode");
    Separator(mode);
    Item(mode, ID_MODE_CLEARDONE, L"&Clear Completed Jobs");

    HMENU view = Sub(menu_, L"&View");
    Item(view, ID_VIEW_HASH, L"Show &Hash");
    Item(view, ID_VIEW_EXPECTED, L"Show &Expected Hash");
    Item(view, ID_VIEW_TIME, L"Show &Time Elapsed Tab");
    Item(view, ID_VIEW_THROUGHPUT, L"Show Throughput && E&TA");
    Item(view, ID_VIEW_FULLPATHS, L"Show &Full File Paths");
    Separator(view);
    Item(view, ID_VIEW_LOCKCOLS, L"&Lock Column Order");

    HMENU opt = Sub(menu_, L"&Options");
    HMENU pathStorage = Sub(opt, L"&Path Storage");
    Item(pathStorage, ID_OPT_PATH_REL, L"&Relative Paths (Default)");
    Item(pathStorage, ID_OPT_PATH_ABS, L"&Absolute Paths");
    Item(opt, ID_OPT_ADVANCED, L"Show A&dvanced Options Bar");
    Item(opt, ID_OPT_FILTER, L"Show &Search/Filter Bar");
    HMENU proc = Sub(opt, L"Processing &Mode");
    Item(proc, ID_OPT_PROC_AUTO, L"&Automatic Detection (Default)");
    Item(proc, ID_OPT_PROC_HDD, L"&HDD Mode (Sequential)");
    Item(proc, ID_OPT_PROC_SSD, L"&SSD Mode (Parallel)");
    Separator(proc);
    Item(proc, ID_OPT_DRIVES, L"&Drive Classification...");
    HMENU algo = Sub(opt, L"Default Hashing &Algorithm");
    const wchar_t* algoNames[] = {L"xxHash-3 (128-bit)", L"Crc32 (SFV)", L"MD5", L"SHA-1", L"SHA-256"};
    for (UINT i = 0; i < 5; ++i) Item(algo, ID_OPT_ALGO_FIRST + i, algoNames[i]);
    HMENU crc = Sub(opt, L"&CRC-32 Byte Order (SFV)");
    Item(crc, ID_OPT_CRC_STD, L"&Standard - CBF43926 (QuickSFV, RapidCRC, 7-Zip)");
    Item(crc, ID_OPT_CRC_REV, L"&Reversed - 2639F4CB (C# SharpSFV)");
    Separator(opt);
    Item(opt, ID_OPT_DATASTORAGE, L"&Local Data Storage...");
    HMENU sys = Sub(opt, L"System &Integration");
    Item(sys, ID_OPT_SHELL_REG, L"&Register Explorer Context Menu");
    Item(sys, ID_OPT_SHELL_UNREG, L"&Unregister Explorer Context Menu");
    Separator(sys);
    Item(sys, ID_OPT_INSTALL, L"&Install for Command Line (user folder + PATH)...");
    Item(sys, ID_OPT_UNINSTALL, L"Uninstall from &Command Line...");
    HMENU bad = Sub(opt, L"&Bad File Tools");
    Item(bad, ID_OPT_BAD_MOVE, L"&Move Bad Files to '_BAD_FILES'...");
    Item(bad, ID_OPT_BAD_RENAME, L"&Rename Bad Files (*.CORRUPT)...");
    Separator(bad);
    Item(bad, ID_OPT_BAD_SCRIPT, L"&Generate 'Delete BAD Files' Script");
    HMENU dup = Sub(opt, L"D&uplicate Tools");
    Item(dup, ID_OPT_DUP_COPY, L"Generate &Copy Duplicates Script");
    Item(dup, ID_OPT_DUP_DELETE, L"Generate &Delete Duplicates Script");

    HMENU help = Sub(menu_, L"&Help");
    Item(help, ID_HELP_DISK, L"View current &Disk Data");
    Separator(help);
    Item(help, ID_HELP_RESET, L"&Reset to Default Values");
    Separator(help);
    Item(help, ID_HELP_CREDITS, L"&Credits");
}

void MainWindow::CreateAccelerators() {
    ACCEL full[] = {
        {FVIRTKEY | FCONTROL, 'O', static_cast<WORD>(ID_FILE_OPEN)},
        {FVIRTKEY | FCONTROL, 'S', static_cast<WORD>(ID_FILE_SAVE)},
        {FVIRTKEY | FCONTROL, 'C', static_cast<WORD>(ID_EDIT_COPY)},
        {FVIRTKEY | FCONTROL, 'V', static_cast<WORD>(ID_EDIT_PASTE)},
        {FVIRTKEY | FCONTROL, 'A', static_cast<WORD>(ID_EDIT_SELECTALL)},
    };
    accelFull_ = CreateAcceleratorTableW(full, 5);
    accelEdit_ = CreateAcceleratorTableW(full, 2); // text boxes keep Ctrl+C/V/A
}

void MainWindow::SyncMenu() {
    if (!menu_) return;
    auto check = [&](UINT id, bool on) { CheckMenuItem(menu_, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
    auto enable = [&](UINT id, bool on) { EnableMenuItem(menu_, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED)); };
    auto radio = [&](UINT first, UINT last, UINT on) {
        CheckMenuRadioItem(menu_, first, last, on, MF_BYCOMMAND);
    };

    radio(ID_MODE_STANDARD, ID_MODE_JOB, jobMode_ ? ID_MODE_JOB : ID_MODE_STANDARD);
    enable(ID_MODE_STANDARD, !processing_);
    enable(ID_MODE_JOB, !processing_);
    enable(ID_MODE_CLEARDONE, jobMode_);

    check(ID_VIEW_HASH, settings_.showHashCol);
    check(ID_VIEW_EXPECTED, settings_.showExpectedHashCol);
    check(ID_VIEW_TIME, settings_.showTimeTab);
    check(ID_VIEW_THROUGHPUT, settings_.showThroughputStats);
    check(ID_VIEW_FULLPATHS, settings_.showFullPaths);
    check(ID_VIEW_LOCKCOLS, settings_.lockColumns);
    // Standard-mode features are unavailable in Job Queue mode (UpdateMenuAvailability).
    static const UINT kStandardOnly[] = {ID_VIEW_HASH, ID_VIEW_EXPECTED, ID_VIEW_TIME, ID_OPT_FILTER, ID_OPT_ADVANCED};
    for (const UINT id : kStandardOnly) enable(id, !jobMode_);

    radio(ID_OPT_PATH_REL, ID_OPT_PATH_ABS,
          settings_.pathStorageMode == PathStorageMode::Absolute ? ID_OPT_PATH_ABS : ID_OPT_PATH_REL);
    check(ID_OPT_ADVANCED, settings_.showAdvancedBar);
    check(ID_OPT_FILTER, settings_.showFilterPanel);
    radio(ID_OPT_PROC_AUTO, ID_OPT_PROC_SSD, ID_OPT_PROC_AUTO + static_cast<UINT>(settings_.processingMode));
    radio(ID_OPT_ALGO_FIRST, ID_OPT_ALGO_LAST, ID_OPT_ALGO_FIRST + static_cast<UINT>(currentAlgo_));
    radio(ID_OPT_CRC_STD, ID_OPT_CRC_REV,
          settings_.crc32ByteOrder == Crc32ByteOrder::Reversed ? ID_OPT_CRC_REV : ID_OPT_CRC_STD);

    const bool idleStandard = !processing_ && !jobMode_;
    const bool hasBad = idleStandard && !BadItems().Empty();
    static const UINT kBadTools[] = {ID_OPT_BAD_MOVE, ID_OPT_BAD_RENAME, ID_OPT_BAD_SCRIPT};
    for (const UINT id : kBadTools) enable(id, hasBad);
    const bool dups = idleStandard && chkDups_ && Button_GetCheck(chkDups_) == BST_CHECKED && !display_.Empty();
    enable(ID_OPT_DUP_COPY, dups);
    enable(ID_OPT_DUP_DELETE, dups);
    DrawMenuBar(hwnd_);
}

void MainWindow::OnCommand(UINT id, UINT code, HWND control) {
    // --- control notifications
    if (control) {
        switch (id) {
            case IDC_STOP: if (code == BN_CLICKED) Stop(); return;
            case IDC_PAUSE: if (code == BN_CLICKED) TogglePause(); return;
            case IDC_FILTER_TEXT: if (code == EN_CHANGE) SetTimer(hwnd_, TIMER_FILTER, 300, nullptr); return;
            case IDC_FILTER_STATUS: if (code == CBN_SELCHANGE) ApplyFilter(); return;
            case IDC_FILTER_DUPS: if (code == BN_CLICKED) ApplyFilter(); return;
            case IDC_ADV_PREFIX: if (code == EN_CHANGE) settings_.pathPrefix = GetWindowTextUtf8(control); return;
            case IDC_ADV_INCLUDE: if (code == EN_CHANGE) settings_.includePattern = GetWindowTextUtf8(control); return;
            case IDC_ADV_EXCLUDE: if (code == EN_CHANGE) settings_.excludePattern = GetWindowTextUtf8(control); return;
            case IDC_ADV_RECURSIVE: if (code == BN_CLICKED) settings_.scanRecursive = Button_GetCheck(control) == BST_CHECKED; return;
            case IDC_ADV_COMMENTS:
                if (code == BN_CLICKED) settings_.enableChecksumComments = Button_GetCheck(control) == BST_CHECKED;
                return;
            default: break;
        }
    }
    if (id == IDOK || id == IDCANCEL) return; // Enter/Esc routed by IsDialogMessage

    if (id >= ID_OPT_ALGO_FIRST && id <= ID_OPT_ALGO_LAST) {
        SetAlgorithm(static_cast<HashType>(id - ID_OPT_ALGO_FIRST));
        return;
    }
    switch (id) {
        case ID_FILE_OPEN: ActionOpen(); break;
        case ID_FILE_SAVE: ActionSave(); break;
        case ID_FILE_EXIT: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
        case ID_EDIT_COPY: case ID_CTX_COPY: ActionCopy(); break;
        case ID_EDIT_PASTE: ActionPaste(); break;
        case ID_EDIT_COMPARE: ActionCompareClipboard(); break;
        case ID_EDIT_SELECTALL: ActionSelectAll(); break;
        case ID_MODE_STANDARD: SetAppMode(false); break;
        case ID_MODE_JOB: SetAppMode(true); break;
        case ID_MODE_CLEARDONE:
            if (jobMode_) {
                jobs_.RemoveCompleted();
                RefreshItemCount(true);
                UpdateJobStats();
            }
            break;
        case ID_VIEW_HASH:
            settings_.showHashCol = !settings_.showHashCol;
            if (jobMode_) InvalidateRect(list_, nullptr, FALSE);
            else SetupStandardColumns(verificationMode_);
            break;
        case ID_VIEW_EXPECTED:
            settings_.showExpectedHashCol = !settings_.showExpectedHashCol;
            if (jobMode_) InvalidateRect(list_, nullptr, FALSE);
            else SetupStandardColumns(verificationMode_);
            break;
        case ID_VIEW_TIME:
            settings_.showTimeTab = !settings_.showTimeTab;
            if (!jobMode_) SetupStandardColumns(verificationMode_);
            Layout();
            break;
        case ID_VIEW_THROUGHPUT:
            settings_.showThroughputStats = !settings_.showThroughputStats;
            throttle_.SetShowThroughput(settings_.showThroughputStats);
            break;
        case ID_VIEW_FULLPATHS:
            settings_.showFullPaths = !settings_.showFullPaths;
            RecalculateColumnWidths();
            ApplyNameColumnWidth();
            InvalidateRect(list_, nullptr, FALSE);
            break;
        case ID_VIEW_LOCKCOLS: {
            settings_.lockColumns = !settings_.lockColumns;
            ListView_SetExtendedListViewStyleEx(list_, LVS_EX_HEADERDRAGDROP, settings_.lockColumns ? 0 : LVS_EX_HEADERDRAGDROP);
            break;
        }
        case ID_OPT_PATH_REL: settings_.pathStorageMode = PathStorageMode::Relative; break;
        case ID_OPT_PATH_ABS: settings_.pathStorageMode = PathStorageMode::Absolute; break;
        case ID_OPT_ADVANCED:
            if (jobMode_) break;
            settings_.showAdvancedBar = !settings_.showAdvancedBar;
            ShowWindow(advPanel_, settings_.showAdvancedBar ? SW_SHOW : SW_HIDE);
            Layout();
            break;
        case ID_OPT_FILTER:
            if (jobMode_) break;
            settings_.showFilterPanel = !settings_.showFilterPanel;
            ShowWindow(filterPanel_, settings_.showFilterPanel ? SW_SHOW : SW_HIDE);
            Layout();
            break;
        case ID_OPT_PROC_AUTO: settings_.processingMode = ProcessingMode::Auto; break;
        case ID_OPT_PROC_HDD: settings_.processingMode = ProcessingMode::HDD; break;
        case ID_OPT_PROC_SSD: settings_.processingMode = ProcessingMode::SSD; break;
        case ID_OPT_DRIVES: DrivesDialog(hwnd_, settings_); break;
        case ID_OPT_CRC_STD:
        case ID_OPT_CRC_REV:
            settings_.crc32ByteOrder = id == ID_OPT_CRC_REV ? Crc32ByteOrder::Reversed : Crc32ByteOrder::Standard;
            store_.SetDisplayCrcOrder(settings_.crc32ByteOrder);
            InvalidateRect(list_, nullptr, FALSE);
            break;
        case ID_OPT_DATASTORAGE: ActionDataStorage(); break;
        case ID_OPT_SHELL_REG: ActionRegisterShell(true); break;
        case ID_OPT_SHELL_UNREG: ActionRegisterShell(false); break;
        case ID_OPT_INSTALL: ActionInstall(true); break;
        case ID_OPT_UNINSTALL: ActionInstall(false); break;
        case ID_OPT_BAD_MOVE: ActionMoveBadFiles(); break;
        case ID_OPT_BAD_RENAME: ActionRenameBadFiles(); break;
        case ID_OPT_BAD_SCRIPT: ActionBadFilesScript(); break;
        case ID_OPT_DUP_COPY: ActionDuplicateScript(true); break;
        case ID_OPT_DUP_DELETE: ActionDuplicateScript(false); break;
        case ID_HELP_DISK: ActionDiskInfo(); break;
        case ID_HELP_RESET: ActionResetDefaults(); break;
        case ID_HELP_CREDITS: AboutDialog(hwnd_); break;
        case ID_CTX_OPENFOLDER: ActionOpenFolder(); break;
        case ID_CTX_RENAME: ActionRename(); break;
        case ID_CTX_DELETE: ActionDelete(); break;
        case ID_CTX_REMOVE: ActionRemoveFromList(); break;
        default: return;
    }
    SyncMenu();
}

void MainWindow::SetAppMode(bool jobMode) {
    if (jobMode_ == jobMode || processing_) return;
    jobMode_ = jobMode;
    SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    if (jobMode_) {
        SetupJobColumns();
        // Hide the standard-mode panels.
        ToggleActiveJobsPanel(false);
        ShowWindow(filterPanel_, SW_HIDE);
        ShowWindow(advPanel_, SW_HIDE);
        ShowWindow(commentsPanel_, SW_HIDE);
        SetWarnings(Vec<Str>());
        ShowWindow(lblTime_, SW_HIDE);
        UpdateJobStats();
    } else {
        if (storeHoldsJobFiles_) { // the last job's files are not the standard list: start empty
            store_.Clear();
            display_.Clear();
            storeHoldsJobFiles_ = false;
        }
        SetupStandardColumns(verificationMode_);
        ShowWindow(filterPanel_, settings_.showFilterPanel ? SW_SHOW : SW_HIDE);
        ShowWindow(advPanel_, settings_.showAdvancedBar ? SW_SHOW : SW_HIDE);
        ShowWindow(commentsPanel_, commentsText_.Empty() ? SW_HIDE : SW_SHOW);
        RecountStats(); // the list kept its results: show them again
    }
    RefreshItemCount(true);
    SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    UpdateIdleTitle();
    Layout();
    SyncMenu();
}

} // namespace sfv::gui
