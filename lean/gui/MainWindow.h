// SharpSFV (lean C++ rewrite) - the main window.
// Port of src/gui/MainWindow.* (itself ported from Form1 and its partial classes). The
// implementation is split the same way: MainWindow.cpp (lifecycle, layout, settings),
// MainWindowMenus.cpp, MainWindowList.cpp (virtual list), MainWindowRun.cpp (engine, mini
// mode), MainWindowActions.cpp (user commands).
//
// Threading: the engine runs on one worker thread (plus its own hashing threads). Worker-side
// callbacks only store data in the members marked "shared" (under sharedMutex_) and
// PostMessage a WM_APP_* note; everything else is touched by the UI thread only. The
// parameters of a run are written to the run* members before the worker starts and are only
// read by it while it runs.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "../base/Thread.h"
#include "../core/DriveStore.h"
#include "../core/Engine.h"
#include "../core/JobStore.h"
#include "../core/ListModel.h"
#include "../core/Settings.h"
#include "../core/Store.h"
#include "App.h"
#include "SingleInstance.h"
#include "SystemIntegration.h"

#include <commctrl.h>

namespace sfv::gui {

class MainWindow : public EngineObserver {
public:
    // createMode: launched by Explorer's "Create Checksum" (mini window, auto-save, exit).
    MainWindow(const Vec<Str>& args, bool createMode);
    ~MainWindow() override;
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool Create(int showCommand);
    int RunMessageLoop();

    // EngineObserver (called on worker threads)
    void OnItemsDiscovered(Vec<int>& indices) override;
    void OnLargeFileBegin(int index, StrView fileName) override;
    void OnLargeFileProgress(int index, double percent) override;
    void OnLargeFileEnd(int index) override;

private:
    enum class RunKind { None, Create, Verify, Jobs };

    static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    int S(int v) const { return Scale(v, dpi_); }

    // --- MainWindow.cpp: creation, layout, fonts, settings, title
    void CreateStandardUi();
    void CreateMiniUi();
    HWND MakeLabel(HWND parent, const wchar_t* textValue, HFONT font);
    HWND MakeEdit(HWND parent, UINT id, StrView value);
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
    Vec<ColumnOrderEntry> CurrentColumnOrder() const;
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
    Vec<int> SelectedRows() const;
    void ShowContextMenu(POINT screen);
    void LayoutStatsLabels();
    void UpdateStats(int current, int total, int ok, int bad, int missing, StrView speed = {});
    void RecountStats();
    void UpdateJobStats(StrView speed = {});
    void SetStatusLabel(HWND label, StrView value, COLORREF color);
    void SetWarnings(Vec<Str> warnings);

    // --- MainWindowRun.cpp
    void HandlePaths(Vec<Str> paths);
    void StartCreation(const Vec<Str>& paths, StrView baseDirectory);
    void StartVerification(StrView checksumFile);
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
    static void Present(void* self, const ProgressInfo& p);
    static void CreationMain(void* self);
    static void VerifyMain(void* self);
    static void JobsMain(void* self);
    void PostOnce(bool& flag, UINT message);

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
    void RemoveStoreItems(Vec<int> storeIndices);
    Str CurrentRootDirectory() const;
    Vec<int> BadItems() const;
    Opt<Str> SaveScriptDialog(const char* defaultName);

    // --- window and controls (UI thread)
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    HMENU menu_ = nullptr;
    HACCEL accelFull_ = nullptr, accelEdit_ = nullptr;
    HFONT font_ = nullptr, fontBold_ = nullptr, fontStrike_ = nullptr, fontMono_ = nullptr;
    Vec<HFONT> retiredFonts_; // replaced by a DPI change; deleted once no control uses them
    HWND list_ = nullptr, active_ = nullptr, progress_ = nullptr, progressFile_ = nullptr;
    HWND stop_ = nullptr, pause_ = nullptr, tooltip_ = nullptr;
    HWND statsPanel_ = nullptr, filterPanel_ = nullptr, advPanel_ = nullptr, commentsPanel_ = nullptr;
    HWND lblProgress_ = nullptr, lblTime_ = nullptr, lblWarning_ = nullptr;
    HWND lblOk_ = nullptr, lblBad_ = nullptr, lblMissing_ = nullptr;
    HWND lblSearch_ = nullptr, txtFilter_ = nullptr, lblStatus_ = nullptr, cmbStatus_ = nullptr, chkDups_ = nullptr;
    HWND lblPrefix_ = nullptr, txtPrefix_ = nullptr, lblInclude_ = nullptr, txtInclude_ = nullptr;
    HWND lblExclude_ = nullptr, txtExclude_ = nullptr, chkRecursive_ = nullptr, chkComments_ = nullptr;
    HWND txtComments_ = nullptr;
    Vec<HWND> panelLabels_; // static labels whose size follows their text

    // --- state (UI thread)
    Vec<Str> startupArgs_;
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
    Vec<int> display_;          // display row -> store index
    Vec<ColumnKind> columns_;   // column -> kind
    Vec<int> originalWidths_;   // column -> width at creation (resize limits)
    int sortColumn_ = -1;
    SortOrder sortOrder_ = SortOrder::None;
    bool settingWidth_ = false;
    int cachedFullPathWidth_ = 400, cachedNameWidth_ = 300;
    bool activeVisible_ = false;
    int splitterPos_ = 0;
    bool draggingSplitter_ = false;
    Vec<Str> warnings_;
    Str commentsText_;
    TaskbarProgress taskbar_;

    // --- engine
    FileStore store_;
    JobStore jobs_;
    ProgressThrottle throttle_;
    Engine engine_;
    Thread worker_;
    RunKind runKind_ = RunKind::None;
    i64 runStart_ = 0;

    // --- parameters of the running worker (written before it starts)
    Vec<Str> runPaths_;
    Str runBase_;
    Str runFile_;
    AppSettings runSettings_;
    HashRunOptions runOpt_;
    HashType runAlgo_ = HashType::XXHASH3;

    // --- create mode
    CreateModeServer pipeServer_;
    Vec<Str> collected_;
    Str autoSavePath_;
    bool createLaunched_ = false;

    // --- shared with the worker (guarded by sharedMutex_; the *Posted_ flags are __atomic)
    Mutex sharedMutex_;
    ProgressInfo progressSnapshot_;
    Vec<int> discoveredPending_;
    struct LargeFile {
        int index = -1;
        Str name;
        double percent = 0;
        bool ended = false;
    };
    Vec<LargeFile> largeFiles_; // sorted by index (a std::map in the standard build)
    RunResult result_;
    Str runError_;
    Vec<int> verifyOrder_;
    Str verifyComments_;
    DriveDecision driveDecision_;
    bool progressPosted_ = false, discoveredPosted_ = false, largePosted_ = false, jobsPosted_ = false;
    HWND notifyTarget_ = nullptr; // hwnd_, readable from workers
};

} // namespace sfv::gui
