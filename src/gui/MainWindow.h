// SharpSFV (C++ port) - the main window.
// Ported from Form1 and its partial classes (Form1.cs, .Actions, .ActiveJobs, .DragDrop,
// .Menus, .UI.Layout, .UI.VirtualMode, .Processing). The implementation is split the same
// way: MainWindow.cpp (lifecycle, layout, settings), MainWindowMenus.cpp,
// MainWindowList.cpp (virtual list), MainWindowRun.cpp (engine, mini mode),
// MainWindowActions.cpp (user commands).
//
// Threading: the engine runs on one worker thread. Worker-side callbacks only store data
// in the members marked "shared" (under their mutex) and PostMessage a WM_APP_* note;
// everything else is touched by the UI thread only. The FileStore is designed for one
// writer thread plus UI readers; it is only cleared or compacted while nothing runs.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "App.h"
#include "AppSettings.h"
#include "DriveClassifier.h"
#include "Engine.h"
#include "FileStore.h"
#include "JobStore.h"
#include "ListModel.h"
#include "SingleInstance.h"
#include "SystemIntegration.h"

#include <commctrl.h>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sfv::gui {

class MainWindow : public EngineObserver {
public:
    // createMode: launched by Explorer's "Create Checksum" (mini window, auto-save, exit).
    MainWindow(std::vector<std::string> args, bool createMode);
    ~MainWindow() override;
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool Create(int showCommand);
    int RunMessageLoop();

    // EngineObserver (called on worker threads)
    void OnItemsDiscovered(std::vector<int> indices) override;
    void OnLargeFileBegin(int index, const std::string& fileName) override;
    void OnLargeFileProgress(int index, double percent) override;
    void OnLargeFileEnd(int index) override;

private:
    enum class RunKind { None, Create, Verify, Jobs };

    static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    UINT Dpi() const { return dpi_; }
    int S(int v) const { return Scale(v, dpi_); }

    // --- MainWindow.cpp: creation, layout, fonts, settings, title
    void CreateStandardUi();
    void CreateMiniUi();
    HWND MakeLabel(HWND parent, const wchar_t* textValue, HFONT font);
    HWND MakeEdit(HWND parent, UINT id, const std::string& value);
    HWND MakeCheck(HWND parent, UINT id, const wchar_t* textValue, bool checked);
    void AddTooltip(HWND control, const wchar_t* tip);
    void CreateFonts();
    void ApplyFonts();
    void Layout();
    void LayoutMini();
    void OnDpiChanged(UINT newDpi, const RECT* suggested);
    RECT SplitterRect() const;
    void ToggleActiveJobsPanel(bool show);
    void ApplySettings();
    void EnsureFocusReachable();
    void SaveSettings();
    void SetAlgorithm(HashType t);
    void UpdateIdleTitle();
    void OnClose();

    // --- MainWindowMenus.cpp
    void BuildMenu();
    void CreateAccelerators();
    void SyncMenu();
    void OnCommand(UINT id, UINT code, HWND control);
    void SetAppMode(bool jobMode);

    // --- MainWindowList.cpp
    void AddColumn(const wchar_t* title, int width, ColumnKind kind);
    void SetupStandardColumns(bool verification);
    void SetupJobColumns();
    void RestoreColumnOrder();
    std::vector<std::pair<std::string, int>> CurrentColumnOrder() const;
    int ColumnIndex(ColumnKind k) const;
    void OnGetDispInfo(NMLVDISPINFOW* info);
    LRESULT OnCustomDraw(NMLVCUSTOMDRAW* cd);
    void OnColumnClick(int column);
    void UpdateSortArrows();
    bool OnHeaderItemChanging(const NMHEADERW* nm);
    void SetColumnWidth(int column, int width);
    FilterSpec CurrentFilter() const;
    void ApplyFilter();
    void RefreshItemCount(bool full);
    void RecalculateColumnWidths();
    void ApplyNameColumnWidth();
    std::vector<int> SelectedRows() const;
    void ShowContextMenu(POINT screen);
    void LayoutStatsLabels();
    void UpdateStats(int current, int total, int ok, int bad, int missing, const std::string& speed = {});
    void RecountStats();
    void UpdateJobStats(const std::string& speed = {});
    void SetStatusLabel(HWND label, const std::string& value, COLORREF color);
    void SetWarnings(std::vector<std::string> warnings);

    // --- MainWindowRun.cpp
    void HandlePaths(std::vector<std::string> paths);
    void StartCreation(std::vector<std::string> paths, std::string baseDirectory);
    void StartVerification(const std::string& checksumFile);
    void StartJobQueue();
    void BeginRun(RunKind kind);
    void SetProcessingState(bool processing);
    void SetProgressBarState(int pbstState);
    void OnProgressMessage();
    void OnDiscovered();
    void OnLargeFiles();
    void OnVerifyLoaded();
    void OnDriveDecided();
    void OnRunDone();
    void TogglePause();
    void Stop();
    void OnPipePaths();
    void LaunchCreateMode();

    // --- MainWindowActions.cpp
    void ActionOpen();
    void ActionSave();
    void ActionCopy();
    void ActionPaste();
    void ActionCompareClipboard();
    void ActionSelectAll();
    void ActionOpenFolder();
    void ActionRename();
    void ActionDelete();
    void ActionRemoveFromList();
    void ActionMoveBadFiles();
    void ActionRenameBadFiles();
    void ActionBadFilesScript();
    void ActionDuplicateScript(bool copy);
    void ActionDataStorage();
    void ActionRegisterShell(bool reg);
    void ActionInstall(bool install);
    void ActionDiskInfo();
    void ActionResetDefaults();
    void RemoveStoreItems(std::vector<int> storeIndices);
    std::string CurrentRootDirectory() const;
    std::vector<int> BadItems() const;
    std::optional<std::string> SaveScriptDialog(const char* defaultName);

    // --- window and controls (UI thread)
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    HMENU menu_ = nullptr;
    HACCEL accelFull_ = nullptr, accelEdit_ = nullptr;
    HFONT font_ = nullptr, fontBold_ = nullptr, fontStrike_ = nullptr, fontMono_ = nullptr;
    std::vector<HFONT> retiredFonts_; // replaced by a DPI change; deleted once no control uses them
    HWND list_ = nullptr, active_ = nullptr, progress_ = nullptr, progressFile_ = nullptr;
    HWND stop_ = nullptr, pause_ = nullptr, tooltip_ = nullptr;
    HWND statsPanel_ = nullptr, filterPanel_ = nullptr, advPanel_ = nullptr, commentsPanel_ = nullptr;
    HWND lblProgress_ = nullptr, lblTime_ = nullptr, lblWarning_ = nullptr;
    HWND lblOk_ = nullptr, lblBad_ = nullptr, lblMissing_ = nullptr;
    HWND lblSearch_ = nullptr, txtFilter_ = nullptr, lblStatus_ = nullptr, cmbStatus_ = nullptr, chkDups_ = nullptr;
    HWND lblPrefix_ = nullptr, txtPrefix_ = nullptr, lblInclude_ = nullptr, txtInclude_ = nullptr;
    HWND lblExclude_ = nullptr, txtExclude_ = nullptr, chkRecursive_ = nullptr, chkComments_ = nullptr;
    HWND txtComments_ = nullptr;
    std::vector<HWND> panelLabels_; // static labels whose size follows their text

    // --- state (UI thread)
    std::vector<std::string> startupArgs_;
    AppSettings settings_;
    HashType currentAlgo_ = HashType::XXHASH3;
    bool createMode_ = false;
    bool jobMode_ = false;
    bool verificationMode_ = false;
    bool storeHoldsJobFiles_ = false; // the store has the job runner's leftovers, not a user list
    bool processing_ = false;
    bool paused_ = false;
    bool skipSaveOnClose_ = false;
    bool closing_ = false;
    std::vector<int> display_;                 // display row -> store index
    std::vector<ColumnKind> columns_;          // column -> kind
    std::vector<int> originalWidths_;          // column -> width at creation (resize limits)
    int sortColumn_ = -1;
    SortOrder sortOrder_ = SortOrder::None;
    bool settingWidth_ = false;
    int cachedFullPathWidth_ = 400, cachedNameWidth_ = 300;
    bool activeVisible_ = false;
    int splitterPos_ = 0;
    bool draggingSplitter_ = false;
    std::vector<std::string> warnings_;
    std::string commentsText_;
    TaskbarProgress taskbar_;

    // --- engine
    FileStore store_;
    JobStore jobs_;
    ProgressThrottle throttle_;
    Engine engine_;
    Thread worker_;
    RunKind runKind_ = RunKind::None;
    std::chrono::steady_clock::time_point runStart_;

    // --- create mode
    CreateModeServer pipeServer_;
    std::vector<std::string> collected_;
    std::string autoSavePath_;
    bool createLaunched_ = false;

    // --- shared with the worker (guarded)
    Mutex sharedMutex_;
    ProgressInfo progress_snapshot_;
    std::vector<int> discoveredPending_;
    struct LargeFile {
        std::string name;
        double percent = 0;
        bool ended = false;
    };
    std::map<int, LargeFile> largeFiles_;
    RunResult result_;
    std::string runError_;
    std::vector<int> verifyOrder_;
    std::string verifyComments_;
    DriveDecision driveDecision_;
    std::atomic<bool> discoveredPosted_{false}, largePosted_{false}, jobsPosted_{false};
    HWND notifyTarget_ = nullptr; // hwnd_, readable from workers
};

} // namespace sfv::gui
