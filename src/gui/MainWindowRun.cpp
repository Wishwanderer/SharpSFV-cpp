// SharpSFV (C++ port) - main window: running the engine, progress, completion, mini mode.
// Ported from Form1.Processing.cs (RunHashCreation, RunVerification, ProcessJobQueue,
// ThrottledUiUpdate), HandleCompletion / LaunchCreateMode / pipe handling (Form1.cs),
// HandleDroppedPaths / SetProcessingState (Form1.DragDrop.cs), Form1.ActiveJobs.cs and
// PerformTogglePause / PerformCancelAction (Form1.Actions.cs).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "MainWindow.h"

#include "ChecksumParser.h"
#include "ChecksumWriter.h"
#include "DataStorage.h"
#include "FileSystem.h"
#include "PathUtil.h"
#include "Text.h"

#include <windowsx.h>

namespace sfv::gui {

namespace {
// Progress bar colors: PBST_NORMAL (green), PBST_ERROR (red), PBST_PAUSED (yellow) from commctrl.h.

std::string NameOfFolder(const std::string& p) {
    std::string t = p;
    while (t.size() > 3 && path::IsSep(t.back())) t.pop_back();
    const std::string_view n = path::GetFileName(t);
    return n.empty() ? t : std::string(n);
}

bool Automatic(DriveDecision::Source s) {
    return s == DriveDecision::Source::Stored || s == DriveDecision::Source::DetectedSaved ||
           s == DriveDecision::Source::DetectedNotSaved;
}
} // namespace

// ------------------------------------------------------------------ engine callbacks (worker threads)

void MainWindow::OnItemsDiscovered(std::vector<int> indices) {
    {
        std::lock_guard lock(sharedMutex_);
        discoveredPending_.insert(discoveredPending_.end(), indices.begin(), indices.end());
    }
    if (!discoveredPosted_.exchange(true)) PostMessageW(notifyTarget_, WM_APP_DISCOVERED, 0, 0);
}

void MainWindow::OnLargeFileBegin(int index, const std::string& fileName) {
    {
        std::lock_guard lock(sharedMutex_);
        largeFiles_[index] = LargeFile{fileName, 0.0, false};
    }
    if (!largePosted_.exchange(true)) PostMessageW(notifyTarget_, WM_APP_LARGEFILES, 0, 0);
}

void MainWindow::OnLargeFileProgress(int index, double percent) {
    {
        std::lock_guard lock(sharedMutex_);
        auto it = largeFiles_.find(index);
        if (it == largeFiles_.end()) return;
        it->second.percent = percent;
    }
    if (!largePosted_.exchange(true)) PostMessageW(notifyTarget_, WM_APP_LARGEFILES, 0, 0);
}

void MainWindow::OnLargeFileEnd(int index) {
    {
        std::lock_guard lock(sharedMutex_);
        auto it = largeFiles_.find(index);
        if (it == largeFiles_.end()) return;
        it->second.ended = true;
    }
    if (!largePosted_.exchange(true)) PostMessageW(notifyTarget_, WM_APP_LARGEFILES, 0, 0);
}

// ------------------------------------------------------------------ starting work

void MainWindow::HandlePaths(std::vector<std::string> paths) {
    if (paths.empty()) return;
    if (jobMode_) {
        // A drop becomes one job: a folder is named after itself and saved beside it,
        // several items are saved in their deepest common folder.
        std::string baseDir, jobName;
        if (paths.size() == 1) {
            if (fs::DirectoryExists(paths[0])) {
                baseDir = path::GetDirectoryName(paths[0]).value_or(paths[0]);
                jobName = NameOfFolder(paths[0]);
            } else {
                baseDir = path::GetDirectoryName(paths[0]).value_or("");
                jobName = std::string(path::GetFileName(paths[0]));
            }
        } else {
            std::vector<std::string> parents;
            for (const auto& p : paths) parents.push_back(path::GetDirectoryName(p).value_or(p));
            baseDir = path::FindCommonBasePath(parents);
            jobName = baseDir.empty() ? std::string("Multi_Files") : NameOfFolder(baseDir);
        }
        jobs_.Add(jobName, baseDir, std::move(paths));
        RefreshItemCount(false);
        InvalidateRect(list_, nullptr, FALSE);
        UpdateJobStats();
        StartJobQueue();
        return;
    }

    if (processing_) return;
    // Reset sorting.
    sortColumn_ = -1;
    sortOrder_ = SortOrder::None;
    UpdateSortArrows();

    bool containsFolder = false;
    for (const auto& p : paths) containsFolder |= fs::DirectoryExists(p);
    // A single checksum file means "verify it".
    if (!containsFolder && paths.size() == 1 && IsVerificationExtension(paths[0]) && fs::FileExists(paths[0])) {
        StartVerification(paths[0]);
        return;
    }
    std::string base;
    if (paths.size() == 1 && fs::DirectoryExists(paths[0])) {
        base = paths[0];
    } else {
        std::vector<std::string> parents;
        for (const auto& p : paths) parents.push_back(path::GetDirectoryName(p).value_or(p));
        base = path::FindCommonBasePath(parents);
    }
    StartCreation(std::move(paths), std::move(base));
}

void MainWindow::BeginRun(RunKind kind) {
    runKind_ = kind;
    runStart_ = std::chrono::steady_clock::now();
    cachedNameWidth_ = 0; // a new list: the previous run's widths no longer apply
    cachedFullPathWidth_ = S(400);
    engine_.ResetCancel();
    engine_.Resume();
    throttle_.ResetMetrics();
    throttle_.SetShowThroughput(settings_.showThroughputStats);
    {
        std::lock_guard lock(sharedMutex_);
        result_ = RunResult{};
        runError_.clear();
        largeFiles_.clear();
        discoveredPending_.clear();
        driveDecision_ = DriveDecision{};
    }
    SetProcessingState(true);
    taskbar_.SetState(TBPF_NORMAL);
    taskbar_.SetValue(0, 1);
}

void MainWindow::StartCreation(std::vector<std::string> paths, std::string baseDirectory) {
    BeginRun(RunKind::Create);
    if (!createMode_) {
        SetupStandardColumns(false);
        commentsText_.clear();
        ShowWindow(commentsPanel_, SW_HIDE);
        SetWarnings({});
        Layout();
    }
    store_.Clear();
    display_.clear();
    if (list_) RefreshItemCount(true);
    if (!createMode_) SetWindowTextUtf8(hwnd_, "SharpSFV - Creating...");

    AppSettings snap = settings_; // the worker never touches settings_
    HashRunOptions opt;
    opt.algo = currentAlgo_;
    opt.updateFileList = !createMode_;
    opt.reportLargeFiles = true; // Active Jobs panel, or the mini window's file bar
    opt.applyFilters = snap.showAdvancedBar;
    opt.includePattern = snap.includePattern;
    opt.excludePattern = snap.excludePattern;
    opt.recursive = (createMode_ || snap.showAdvancedBar) ? snap.scanRecursive : true;

    worker_ = Thread([this, paths = std::move(paths), base = std::move(baseDirectory), snap, opt]() mutable {
        DataStorage storage(snap);
        const DriveResolver drives(&storage);
        std::string probe;
        if (!paths.empty())
            probe = fs::DirectoryExists(paths[0]) ? paths[0] : path::GetDirectoryName(paths[0]).value_or("");
        const DriveDecision d = drives.Resolve(snap.processingMode, probe.empty() ? base : probe);
        {
            std::lock_guard lock(sharedMutex_);
            driveDecision_ = d;
        }
        PostMessageW(notifyTarget_, WM_APP_DRIVE_DECIDED, 0, 0);
        opt.isHdd = d.isHdd;
        const RunResult r = engine_.HashFiles(paths, base, opt, [this](const Counters& c) {
            throttle_.Update(c.completed, c.total, c.ok, c.bad, 0);
        });
        {
            std::lock_guard lock(sharedMutex_);
            result_ = r;
        }
        PostMessageW(notifyTarget_, WM_APP_RUN_DONE, 0, 0);
    });
}

void MainWindow::StartVerification(const std::string& checksumFile) {
    BeginRun(RunKind::Verify);
    SetWarnings({});
    const HashType algo = DetectAlgoFromExtension(checksumFile, currentAlgo_);
    SetAlgorithm(algo);
    SetupStandardColumns(true);
    store_.Clear();
    display_.clear();
    RefreshItemCount(true);
    commentsText_.clear();
    ShowWindow(commentsPanel_, SW_HIDE);
    Layout();
    SetWindowTextUtf8(hwnd_, "SharpSFV - Verifying...");

    AppSettings snap = settings_;
    worker_ = Thread([this, file = checksumFile, algo, snap]() mutable {
        std::string err;
        auto parsed = ParseChecksumFile(file, algo, &err);
        if (!parsed) {
            {
                std::lock_guard lock(sharedMutex_);
                runError_ = "Error parsing: " + err;
            }
            PostMessageW(notifyTarget_, WM_APP_RUN_DONE, 0, 0);
            return;
        }
        const std::string baseFolder = path::GetDirectoryName(file).value_or("");
        std::vector<int> order = PopulateVerificationStore(store_, *parsed, baseFolder, true);
        {
            std::lock_guard lock(sharedMutex_);
            verifyOrder_ = order;
            verifyComments_ = parsed->comments;
        }
        PostMessageW(notifyTarget_, WM_APP_VERIFY_LOADED, 0, 0);

        DataStorage storage(snap);
        const DriveDecision d = DriveResolver(&storage).Resolve(snap.processingMode, baseFolder);
        {
            std::lock_guard lock(sharedMutex_);
            driveDecision_ = d;
        }
        PostMessageW(notifyTarget_, WM_APP_DRIVE_DECIDED, 0, 0);

        HashRunOptions opt;
        opt.algo = algo;
        opt.isHdd = d.isHdd;
        opt.reportLargeFiles = true;
        const RunResult r = engine_.Verify(order, opt, [this](const Counters& c) {
            throttle_.Update(c.completed, c.total, c.ok, c.bad, c.missing);
        });
        {
            std::lock_guard lock(sharedMutex_);
            result_ = r;
        }
        PostMessageW(notifyTarget_, WM_APP_RUN_DONE, 0, 0);
    });
}

void MainWindow::StartJobQueue() {
    if (worker_.joinable()) return; // the running queue picks up new jobs itself
    if (!jobs_.NextQueued()) return;
    BeginRun(RunKind::Jobs);
    display_.clear(); // the job runner reuses (and clears) the file store
    storeHoldsJobFiles_ = true;
    verificationMode_ = false;
    commentsText_.clear();
    AppSettings snap = settings_;
    const HashType algo = currentAlgo_;
    worker_ = Thread([this, snap, algo]() mutable {
        DataStorage storage(snap);
        const DriveResolver drives(&storage);
        JobRunner::Hooks hooks;
        hooks.jobsChanged = [this] {
            if (!jobsPosted_.exchange(true)) PostMessageW(notifyTarget_, WM_APP_JOBS_CHANGED, 0, 0);
        };
        hooks.progress = [this](const Counters& c) { throttle_.Update(c.completed, c.total, c.ok, c.bad, 0); };
        JobRunner::Run(jobs_, engine_, snap, algo, false, drives, hooks);
        {
            std::lock_guard lock(sharedMutex_);
            result_ = RunResult{};
            result_.cancelled = engine_.IsCancelled();
        }
        PostMessageW(notifyTarget_, WM_APP_RUN_DONE, 0, 0);
    });
}

// ------------------------------------------------------------------ state

void MainWindow::SetProcessingState(bool processing) {
    processing_ = processing;
    EnableWindow(stop_, processing);
    EnableWindow(pause_, processing);
    if (processing) {
        SetWindowTextW(pause_, L"Pause");
        paused_ = false;
        SetProgressBarState(PBST_NORMAL);
        if (lblTime_) SetWindowTextW(lblTime_, L"");
    }
    SyncMenu();
}

namespace {
// Themed progress bars animate towards a new position, so the final position of a run may not
// be drawn before the state changes (red/yellow) or the run ends. Moving backwards is drawn at
// once, so step one past the target and back.
void SetBarImmediately(HWND bar, int pos, int max) {
    if (!bar) return;
    SendMessageW(bar, PBM_SETRANGE32, 0, max + 1);
    SendMessageW(bar, PBM_SETPOS, static_cast<WPARAM>(pos + 1), 0);
    SendMessageW(bar, PBM_SETPOS, static_cast<WPARAM>(pos), 0);
    SendMessageW(bar, PBM_SETRANGE32, 0, max);
}
} // namespace

void MainWindow::SetProgressBarState(int state) {
    if (progress_) SendMessageW(progress_, PBM_SETSTATE, static_cast<WPARAM>(state), 0);
}

void MainWindow::TogglePause() {
    if (!processing_) return;
    paused_ = !paused_;
    SetWindowTextW(pause_, paused_ ? L"Resume" : L"Pause");
    if (jobMode_) { // reflect it on the running job
        const int n = jobs_.Count();
        for (int i = 0; i < n; ++i) {
            auto job = jobs_.Get(i);
            JobStatus expected = paused_ ? JobStatus::InProgress : JobStatus::Paused;
            if (job && job->status.compare_exchange_strong(expected, paused_ ? JobStatus::Paused : JobStatus::InProgress)) {
                InvalidateRect(list_, nullptr, FALSE);
                break;
            }
        }
    }
    SetProgressBarState(paused_ ? PBST_PAUSED : PBST_NORMAL);
    taskbar_.SetState(paused_ ? TBPF_PAUSED : TBPF_NORMAL);
    if (paused_) engine_.Pause();
    else engine_.Resume();
}

void MainWindow::Stop() {
    // The C# version restarted the whole application to cancel; the engine stops cleanly.
    if (!processing_) return;
    engine_.Resume();
    engine_.Cancel();
    EnableWindow(stop_, FALSE);
    EnableWindow(pause_, FALSE);
}

// ------------------------------------------------------------------ worker notifications

void MainWindow::OnProgressMessage() {
    ProgressInfo p;
    {
        std::lock_guard lock(sharedMutex_);
        p = progress_snapshot_;
    }
    int safeTotal = p.total <= 0 ? store_.Count() : p.total;
    if (safeTotal <= 0) safeTotal = 1;
    SendMessageW(progress_, PBM_SETRANGE32, 0, safeTotal);
    SendMessageW(progress_, PBM_SETPOS, static_cast<WPARAM>(std::min(p.current, safeTotal)), 0);
    taskbar_.SetValue(static_cast<ULONGLONG>(p.current), static_cast<ULONGLONG>(safeTotal));
    if (createMode_) {
        SetWindowTextUtf8(hwnd_, "SharpSFV - Creating... " + std::to_string(p.current) + " / " + std::to_string(safeTotal));
    } else if (jobMode_) {
        UpdateJobStats(p.speed);
        InvalidateRect(list_, nullptr, FALSE);
    } else {
        UpdateStats(p.current, safeTotal, p.ok, p.bad, p.missing, p.speed);
        InvalidateRect(list_, nullptr, FALSE);
    }
    throttle_.Release();
}

void MainWindow::OnDiscovered() {
    discoveredPosted_.store(false);
    std::vector<int> batch;
    {
        std::lock_guard lock(sharedMutex_);
        batch.swap(discoveredPending_);
    }
    if (jobMode_ || createMode_ || batch.empty()) return;
    const FilterSpec f = CurrentFilter();
    if (f.duplicates) return; // duplicates are computed from finished hashes (re-apply the filter)
    if (FilterActive(f)) {
        for (int i : batch)
            if (MatchesFilter(store_, i, f)) display_.push_back(i);
    } else {
        display_.insert(display_.end(), batch.begin(), batch.end());
    }
    RefreshItemCount(false);
}

void MainWindow::OnLargeFiles() {
    largePosted_.store(false);
    std::map<int, LargeFile> snapshot;
    {
        std::lock_guard lock(sharedMutex_);
        snapshot = largeFiles_;
        std::erase_if(largeFiles_, [](const auto& kv) { return kv.second.ended; });
    }
    if (createMode_) { // mini window: second bar = current large file
        double pct = 0;
        for (const auto& [i, f] : snapshot)
            if (!f.ended) pct = std::max(pct, f.percent);
        SendMessageW(progressFile_, PBM_SETPOS, static_cast<WPARAM>(pct), 0);
        return;
    }
    if (jobMode_ || !active_) return;
    for (const auto& [index, f] : snapshot) {
        LVFINDINFOW fi{};
        fi.flags = LVFI_PARAM;
        fi.lParam = index;
        const int row = ListView_FindItem(active_, -1, &fi);
        if (f.ended) {
            if (row >= 0) ListView_DeleteItem(active_, row);
            continue;
        }
        const std::wstring pct = f.percent > 0 ? W(text::FormatF1(f.percent) + "%") : std::wstring(L"Starting...");
        if (row < 0) {
            const std::wstring name = W(f.name);
            LVITEMW it{};
            it.mask = LVIF_TEXT | LVIF_PARAM;
            it.iItem = ListView_GetItemCount(active_);
            it.pszText = const_cast<wchar_t*>(name.c_str());
            it.lParam = index;
            const int r = ListView_InsertItem(active_, &it);
            ListView_SetItemText(active_, r, 1, const_cast<wchar_t*>(pct.c_str()));
        } else {
            ListView_SetItemText(active_, row, 1, const_cast<wchar_t*>(pct.c_str()));
        }
    }
    ToggleActiveJobsPanel(ListView_GetItemCount(active_) > 0);
}

void MainWindow::OnVerifyLoaded() {
    std::vector<int> order;
    std::string comments;
    {
        std::lock_guard lock(sharedMutex_);
        order.swap(verifyOrder_);
        comments.swap(verifyComments_);
    }
    display_ = std::move(order);
    RefreshItemCount(true);
    UpdateStats(0, store_.Count(), 0, 0, 0);
    SendMessageW(progress_, PBM_SETRANGE32, 0, std::max(1, store_.Count()));
    commentsText_ = comments;
    std::string shown;
    for (const auto& l : text::SplitLines(comments)) shown += l + "\r\n";
    SetWindowTextUtf8(txtComments_, shown);
    ShowWindow(commentsPanel_, text::IsNullOrWhiteSpace(comments) ? SW_HIDE : SW_SHOW);
    Layout();
}

void MainWindow::OnDriveDecided() {
    DriveDecision d;
    {
        std::lock_guard lock(sharedMutex_);
        d = driveDecision_;
    }
    if (createMode_ || jobMode_) return;
    const char* what = runKind_ == RunKind::Verify ? "SharpSFV - Verifying... [" : "SharpSFV - Creating... [";
    SetWindowTextUtf8(hwnd_, std::string(what) + (d.isHdd ? "HDD/Seq" : "SSD/Par") + "]");
    if (Automatic(d.source) && d.detectionFailed) {
        auto w = warnings_;
        w.push_back("Drive type not detected: using HDD mode (see Options > Processing Mode)");
        SetWarnings(std::move(w));
    }
}

void MainWindow::OnRunDone() {
    if (worker_.joinable()) worker_.join();
    RunResult r;
    std::string error;
    {
        std::lock_guard lock(sharedMutex_);
        r = result_;
        error = runError_;
    }
    const RunKind kind = runKind_;
    runKind_ = RunKind::None;
    OnDiscovered(); // flush the last batch
    if (active_) {
        ListView_DeleteAllItems(active_);
        ToggleActiveJobsPanel(false);
    }
    SetProcessingState(false);

    if (!error.empty()) {
        MsgBox(hwnd_, error, "SharpSFV", MB_OK | MB_ICONERROR);
        if (createMode_) DestroyWindow(hwnd_);
        return;
    }

    if (kind == RunKind::Jobs) {
        UpdateJobStats();
        RefreshItemCount(true);
        if (!r.cancelled) { // the bar tracked the last job's files: show the queue as finished
            SetBarImmediately(progress_, 1, 1);
        } else {
            SetProgressBarState(PBST_PAUSED);
        }
        taskbar_.SetState(TBPF_NOPROGRESS);
        if (!r.cancelled && jobs_.NextQueued()) StartJobQueue(); // queued after the runner's last check
        else UpdateIdleTitle();
        return;
    }

    const bool cancelled = r.cancelled;
    const bool verify = kind == RunKind::Verify;
    if (!cancelled) MessageBeep(r.bad > 0 || (r.missing > 0 && verify) ? MB_ICONEXCLAMATION : MB_ICONASTERISK);

    const int total = std::max(1, store_.Count());
    SetBarImmediately(progress_, std::min(r.completed, total), total);
    const int pbState = (r.bad > 0 || r.missing > 0) ? PBST_ERROR : (cancelled ? PBST_PAUSED : PBST_NORMAL);
    SetProgressBarState(pbState);
    taskbar_.SetValue(static_cast<ULONGLONG>(r.completed), static_cast<ULONGLONG>(total));
    taskbar_.SetState(pbState == PBST_ERROR ? TBPF_ERROR : (cancelled ? TBPF_PAUSED : TBPF_NORMAL));

    if (createMode_) {
        SendMessageW(progressFile_, PBM_SETPOS, 100, 0);
        if (!cancelled) {
            std::string err;
            if (!WriteCreateMode(autoSavePath_, store_, settings_, currentAlgo_, &err))
                MsgBox(hwnd_, "Error saving: " + err, "SharpSFV", MB_OK | MB_ICONERROR);
        }
        DestroyWindow(hwnd_); // done (or cancelled): the mini window closes
        return;
    }

    RecalculateColumnWidths();
    ApplyNameColumnWidth(); // grows the name column for long names (full paths: fits them)
    UpdateStats(r.completed, store_.Count(), r.ok, r.bad, r.missing);
    const std::string algo = ToString(currentAlgo_);
    if (cancelled) {
        SetWindowTextUtf8(hwnd_, "SharpSFV - Cancelled [" + algo + "]");
    } else if (verify) {
        SetWindowTextUtf8(hwnd_, r.bad == 0 && r.missing == 0
                                     ? "SharpSFV [" + algo + "] - All Files OK"
                                     : "SharpSFV [" + algo + "] - " + std::to_string(r.bad) + " Bad, " +
                                           std::to_string(r.missing) + " Missing");
    } else {
        SetWindowTextUtf8(hwnd_, "SharpSFV - Creation Complete [" + algo + "]");
    }
    if (settings_.showTimeTab && !cancelled) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - runStart_);
        SetWindowTextUtf8(lblTime_, "Total Elapsed Time: " + std::to_string(ms.count()) + " ms");
        ShowWindow(lblTime_, SW_SHOW);
    }
    // Replaces "Legacy SFV (Endian Mismatch) Detected": report the byte orders found.
    if (verify && currentAlgo_ == HashType::Crc32 && r.crcReversedMatches > 0) {
        auto w = warnings_;
        w.push_back("CRC-32 byte order: " + std::to_string(r.crcStandardMatches) + " standard, " +
                    std::to_string(r.crcReversedMatches) + " reversed");
        SetWarnings(std::move(w));
    }
    const FilterSpec f = CurrentFilter();
    if (f.duplicates) ApplyFilter();
    LayoutStatsLabels();
    InvalidateRect(list_, nullptr, FALSE);
    SyncMenu();
}

// ------------------------------------------------------------------ mini (create) mode

void MainWindow::OnPipePaths() {
    for (auto& p : pipeServer_.TakePaths()) {
        if (processing_) continue; // too late for this run (as in the original)
        collected_.push_back(std::move(p));
    }
    if (!createLaunched_) SetTimer(hwnd_, TIMER_CREATE, 200, nullptr); // restart the debounce
}

void MainWindow::LaunchCreateMode() {
    if (createLaunched_) return;
    createLaunched_ = true;
    for (auto& p : pipeServer_.TakePaths()) collected_.push_back(std::move(p));
    if (collected_.empty()) {
        DestroyWindow(hwnd_);
        return;
    }
    std::string baseDir;
    if (collected_.size() == 1 && fs::DirectoryExists(collected_[0])) baseDir = collected_[0];
    else if (collected_.size() == 1) baseDir = path::GetDirectoryName(collected_[0]).value_or("");
    else baseDir = path::FindCommonBasePath(collected_);

    int filterIndex = 1;
    switch (settings_.defaultAlgo) {
        case HashType::MD5: filterIndex = 2; break;
        case HashType::SHA1: filterIndex = 3; break;
        case HashType::SHA256: filterIndex = 4; break;
        case HashType::XXHASH3: filterIndex = 5; break;
        default: break;
    }
    const auto chosen = SaveFileDialog(nullptr,
                                       "SFV File (*.sfv)|*.sfv|MD5 File (*.md5)|*.md5|SHA1 File (*.sha1)|*.sha1|"
                                       "SHA256 File (*.sha256)|*.sha256|xxHash3 (*.xxh3)|*.xxh3",
                                       "checksums", baseDir, filterIndex, "SharpSFV - Create Checksum");
    if (!chosen) {
        DestroyWindow(hwnd_);
        return;
    }
    autoSavePath_ = chosen->path;
    currentAlgo_ = DetectAlgoFromExtension(autoSavePath_, HashType::Crc32);
    // Remember the choice without disturbing the other settings (the C# version saved the
    // hidden window's geometry and reset TimeTab and ColumnOrder here).
    settings_.defaultAlgo = currentAlgo_;
    settings_.SaveCurrent();

    for (auto& p : pipeServer_.TakePaths()) collected_.push_back(std::move(p)); // arrived during the dialog

    // Centre on the screen with the mouse and show.
    RECT wr;
    GetWindowRect(hwnd_, &wr);
    POINT cursor;
    GetCursorPos(&cursor);
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &mi);
    const int w = wr.right - wr.left, h = wr.bottom - wr.top;
    SetWindowPos(hwnd_, HWND_TOP, mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2,
                 mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2, 0, 0, SWP_NOSIZE);
    SetWindowTextUtf8(hwnd_, "SharpSFV - Creating...");
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    LayoutMini();
    StartCreation(collected_, baseDir);
}

} // namespace sfv::gui
