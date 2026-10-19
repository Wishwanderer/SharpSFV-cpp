// SharpSFV (lean C++ rewrite) - main window: running the engine, progress, completion, mini mode.
// Port of src/gui/MainWindowRun.cpp (Form1.Processing.cs, HandleCompletion / LaunchCreateMode /
// pipe handling in Form1.cs, HandleDroppedPaths / SetProcessingState in Form1.DragDrop.cs,
// Form1.ActiveJobs.cs, PerformTogglePause / PerformCancelAction in Form1.Actions.cs).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "MainWindow.h"

#include "../base/FileSystem.h"
#include "../base/Path.h"
#include "../base/System.h"
#include "../core/ChecksumFile.h"

#include <windowsx.h>

namespace sfv::gui {

namespace {

Str NameOfFolder(StrView p) {
    Str t(p);
    while (t.Size() > 3 && path::IsSep(t.Back())) t.PopBack();
    const StrView n = path::GetFileName(t);
    return n.Empty() ? t : Str(n);
}

bool Automatic(DriveDecision::Source s) {
    return s == DriveDecision::Source::Stored || s == DriveDecision::Source::DetectedSaved ||
           s == DriveDecision::Source::DetectedNotSaved;
}

// Position of the entry for `index` in the sorted list, or where it would go.
usize FindLarge(const auto& files, int index) {
    usize i = 0;
    while (i < files.Size() && files[i].index < index) ++i;
    return i;
}

} // namespace

// ------------------------------------------------------------------ engine callbacks (worker threads)

void MainWindow::PostOnce(bool& flag, UINT message) {
    if (!__atomic_exchange_n(&flag, true, __ATOMIC_ACQ_REL)) PostMessageW(notifyTarget_, message, 0, 0);
}

void MainWindow::Present(void* self, const ProgressInfo& p) {
    auto* w = static_cast<MainWindow*>(self);
    {
        LockGuard g(w->sharedMutex_);
        w->progressSnapshot_ = p;
    }
    // Coalesced: one message in flight at a time; the UI reads the latest snapshot.
    w->PostOnce(w->progressPosted_, WM_APP_PROGRESS);
}

void MainWindow::OnItemsDiscovered(Vec<int>& indices) {
    {
        LockGuard g(sharedMutex_);
        for (const int i : indices) discoveredPending_.Push(i);
    }
    PostOnce(discoveredPosted_, WM_APP_DISCOVERED);
}

void MainWindow::OnLargeFileBegin(int index, StrView fileName) {
    {
        LockGuard g(sharedMutex_);
        const usize at = FindLarge(largeFiles_, index);
        LargeFile f;
        f.index = index;
        f.name = fileName;
        if (at < largeFiles_.Size() && largeFiles_[at].index == index) {
            largeFiles_[at] = Move(f);
        } else {
            largeFiles_.Push(LargeFile());
            for (usize k = largeFiles_.Size() - 1; k > at; --k) largeFiles_[k] = Move(largeFiles_[k - 1]);
            largeFiles_[at] = Move(f);
        }
    }
    PostOnce(largePosted_, WM_APP_LARGEFILES);
}

void MainWindow::OnLargeFileProgress(int index, double percent) {
    {
        LockGuard g(sharedMutex_);
        const usize at = FindLarge(largeFiles_, index);
        if (at >= largeFiles_.Size() || largeFiles_[at].index != index) return;
        largeFiles_[at].percent = percent;
    }
    PostOnce(largePosted_, WM_APP_LARGEFILES);
}

void MainWindow::OnLargeFileEnd(int index) {
    {
        LockGuard g(sharedMutex_);
        const usize at = FindLarge(largeFiles_, index);
        if (at >= largeFiles_.Size() || largeFiles_[at].index != index) return;
        largeFiles_[at].ended = true;
    }
    PostOnce(largePosted_, WM_APP_LARGEFILES);
}

// ------------------------------------------------------------------ starting work

void MainWindow::HandlePaths(Vec<Str> paths) {
    if (paths.Empty()) return;
    if (jobMode_) {
        // A drop becomes one job: a folder is named after itself and saved beside it,
        // several items are saved in their deepest common folder.
        Str baseDir, jobName;
        if (paths.Size() == 1) {
            if (fs::DirectoryExists(paths[0])) {
                baseDir = path::GetDirectoryName(paths[0]).ValueOr(paths[0]);
                jobName = NameOfFolder(paths[0]);
            } else {
                baseDir = path::GetDirectoryName(paths[0]).ValueOr(Str());
                jobName = path::GetFileName(paths[0]);
            }
        } else {
            Vec<Str> parents;
            for (const Str& p : paths) parents.Push(path::GetDirectoryName(p).ValueOr(p));
            baseDir = path::FindCommonBasePath(parents);
            jobName = baseDir.Empty() ? Str("Multi_Files") : NameOfFolder(baseDir);
        }
        jobs_.Add(jobName, baseDir, Move(paths));
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
    for (const Str& p : paths) containsFolder |= fs::DirectoryExists(p);
    // A single checksum file means "verify it".
    if (!containsFolder && paths.Size() == 1 && IsVerificationExtension(paths[0]) && fs::FileExists(paths[0])) {
        StartVerification(paths[0]);
        return;
    }
    Str base;
    if (paths.Size() == 1 && fs::DirectoryExists(paths[0])) {
        base = paths[0];
    } else {
        Vec<Str> parents;
        for (const Str& p : paths) parents.Push(path::GetDirectoryName(p).ValueOr(p));
        base = path::FindCommonBasePath(parents);
    }
    StartCreation(paths, base);
}

void MainWindow::BeginRun(RunKind kind) {
    runKind_ = kind;
    runStart_ = TickMs();
    cachedNameWidth_ = 0; // a new list: the previous run's widths no longer apply
    cachedFullPathWidth_ = S(400);
    engine_.ResetCancel();
    engine_.Resume();
    throttle_.ResetMetrics();
    throttle_.SetShowThroughput(settings_.showThroughputStats);
    {
        LockGuard g(sharedMutex_);
        result_ = RunResult();
        runError_.Clear();
        largeFiles_.Clear();
        discoveredPending_.Clear();
        driveDecision_ = DriveDecision();
    }
    SetProcessingState(true);
    taskbar_.SetState(TBPF_NORMAL);
    taskbar_.SetValue(0, 1);
}

void MainWindow::CreationMain(void* self) {
    auto* w = static_cast<MainWindow*>(self);
    DataStorage storage(w->runSettings_);
    const DriveResolver drives(&storage);
    Str probe;
    if (!w->runPaths_.Empty())
        probe = fs::DirectoryExists(w->runPaths_[0]) ? w->runPaths_[0] : path::GetDirectoryName(w->runPaths_[0]).ValueOr(Str());
    const DriveDecision d = drives.Resolve(w->runSettings_.processingMode, probe.Empty() ? w->runBase_ : probe);
    {
        LockGuard g(w->sharedMutex_);
        w->driveDecision_ = d;
    }
    PostMessageW(w->notifyTarget_, WM_APP_DRIVE_DECIDED, 0, 0);
    w->runOpt_.isHdd = d.isHdd;
    const RunResult r = w->engine_.HashFiles(w->runPaths_, w->runBase_, w->runOpt_, [w](const Counters& c) {
        w->throttle_.Update(c.completed, c.total, c.ok, c.bad, 0);
    });
    {
        LockGuard g(w->sharedMutex_);
        w->result_ = r;
    }
    PostMessageW(w->notifyTarget_, WM_APP_RUN_DONE, 0, 0);
}

void MainWindow::StartCreation(const Vec<Str>& paths, StrView baseDirectory) {
    BeginRun(RunKind::Create);
    if (!createMode_) {
        SetupStandardColumns(false);
        commentsText_.Clear();
        ShowWindow(commentsPanel_, SW_HIDE);
        SetWarnings(Vec<Str>());
        Layout();
    }
    store_.Clear();
    display_.Clear();
    if (list_) RefreshItemCount(true);
    if (!createMode_) SetWindowTextUtf8(hwnd_, "SharpSFV - Creating...");

    runSettings_ = settings_; // the worker never touches settings_
    runPaths_ = paths;
    runBase_ = baseDirectory;
    runOpt_ = HashRunOptions();
    runOpt_.algo = currentAlgo_;
    runOpt_.updateFileList = !createMode_;
    runOpt_.reportLargeFiles = true; // Active Jobs panel, or the mini window's file bar
    runOpt_.applyFilters = runSettings_.showAdvancedBar;
    runOpt_.includePattern = runSettings_.includePattern;
    runOpt_.excludePattern = runSettings_.excludePattern;
    runOpt_.recursive = (createMode_ || runSettings_.showAdvancedBar) ? runSettings_.scanRecursive : true;
    if (!worker_.Start(&CreationMain, this)) CreationMain(this); // no thread: run here (UI waits)
}

void MainWindow::VerifyMain(void* self) {
    auto* w = static_cast<MainWindow*>(self);
    Str err;
    ParsedChecksumFile parsed;
    if (!ParseChecksumFile(w->runFile_, w->runAlgo_, parsed, &err)) {
        {
            LockGuard g(w->sharedMutex_);
            w->runError_ = "Error parsing: " + err;
        }
        PostMessageW(w->notifyTarget_, WM_APP_RUN_DONE, 0, 0);
        return;
    }
    const Str baseFolder = path::GetDirectoryName(w->runFile_).ValueOr(Str());
    Vec<int> order = PopulateVerificationStore(w->store_, parsed, baseFolder, true);
    {
        LockGuard g(w->sharedMutex_);
        w->verifyOrder_ = order;
        w->verifyComments_ = parsed.comments;
    }
    PostMessageW(w->notifyTarget_, WM_APP_VERIFY_LOADED, 0, 0);

    DataStorage storage(w->runSettings_);
    const DriveDecision d = DriveResolver(&storage).Resolve(w->runSettings_.processingMode, baseFolder);
    {
        LockGuard g(w->sharedMutex_);
        w->driveDecision_ = d;
    }
    PostMessageW(w->notifyTarget_, WM_APP_DRIVE_DECIDED, 0, 0);

    HashRunOptions opt;
    opt.algo = w->runAlgo_;
    opt.isHdd = d.isHdd;
    opt.reportLargeFiles = true;
    const RunResult r = w->engine_.Verify(order, opt, [w](const Counters& c) {
        w->throttle_.Update(c.completed, c.total, c.ok, c.bad, c.missing);
    });
    {
        LockGuard g(w->sharedMutex_);
        w->result_ = r;
    }
    PostMessageW(w->notifyTarget_, WM_APP_RUN_DONE, 0, 0);
}

void MainWindow::StartVerification(StrView checksumFile) {
    const Str file(checksumFile); // may point into the list being cleared below
    BeginRun(RunKind::Verify);
    SetWarnings(Vec<Str>());
    const HashType algo = DetectAlgoFromExtension(file, currentAlgo_);
    SetAlgorithm(algo);
    SetupStandardColumns(true);
    store_.Clear();
    display_.Clear();
    RefreshItemCount(true);
    commentsText_.Clear();
    ShowWindow(commentsPanel_, SW_HIDE);
    Layout();
    SetWindowTextUtf8(hwnd_, "SharpSFV - Verifying...");

    runSettings_ = settings_;
    runFile_ = file;
    runAlgo_ = algo;
    if (!worker_.Start(&VerifyMain, this)) VerifyMain(this);
}

void MainWindow::JobsMain(void* self) {
    auto* w = static_cast<MainWindow*>(self);
    DataStorage storage(w->runSettings_);
    const DriveResolver drives(&storage);
    JobRunner::Hooks hooks;
    hooks.ctx = w;
    hooks.jobsChanged = [](void* ctx) {
        auto* m = static_cast<MainWindow*>(ctx);
        m->PostOnce(m->jobsPosted_, WM_APP_JOBS_CHANGED);
    };
    hooks.progress = [](void* ctx, const Counters& c) {
        static_cast<MainWindow*>(ctx)->throttle_.Update(c.completed, c.total, c.ok, c.bad, 0);
    };
    JobRunner::Run(w->jobs_, w->engine_, w->runSettings_, w->runAlgo_, false, drives, hooks);
    {
        LockGuard g(w->sharedMutex_);
        w->result_ = RunResult();
        w->result_.cancelled = w->engine_.IsCancelled();
    }
    PostMessageW(w->notifyTarget_, WM_APP_RUN_DONE, 0, 0);
}

void MainWindow::StartJobQueue() {
    if (worker_.Running()) return; // the running queue picks up new jobs itself
    if (!jobs_.NextQueued()) return;
    BeginRun(RunKind::Jobs);
    display_.Clear(); // the job runner reuses (and clears) the file store
    storeHoldsJobFiles_ = true;
    verificationMode_ = false;
    commentsText_.Clear();
    runSettings_ = settings_;
    runAlgo_ = currentAlgo_;
    if (!worker_.Start(&JobsMain, this)) JobsMain(this);
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
            const JobRef job = jobs_.Get(i);
            if (job && job->CompareExchangeStatus(paused_ ? JobStatus::InProgress : JobStatus::Paused,
                                                  paused_ ? JobStatus::Paused : JobStatus::InProgress)) {
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
    __atomic_store_n(&progressPosted_, false, __ATOMIC_RELEASE);
    ProgressInfo p;
    {
        LockGuard g(sharedMutex_);
        p = progressSnapshot_;
    }
    int safeTotal = p.total <= 0 ? store_.Count() : p.total;
    if (safeTotal <= 0) safeTotal = 1;
    SendMessageW(progress_, PBM_SETRANGE32, 0, safeTotal);
    SendMessageW(progress_, PBM_SETPOS, static_cast<WPARAM>(Min(p.current, safeTotal)), 0);
    taskbar_.SetValue(static_cast<ULONGLONG>(p.current), static_cast<ULONGLONG>(safeTotal));
    if (createMode_) {
        SetWindowTextUtf8(hwnd_, "SharpSFV - Creating... " + ToStr(p.current) + " / " + ToStr(safeTotal));
    } else if (jobMode_) {
        UpdateJobStats(p.speed);
        InvalidateRect(list_, nullptr, FALSE);
    } else {
        UpdateStats(p.current, safeTotal, p.ok, p.bad, p.missing, p.speed);
        InvalidateRect(list_, nullptr, FALSE);
    }
}

void MainWindow::OnDiscovered() {
    __atomic_store_n(&discoveredPosted_, false, __ATOMIC_RELEASE);
    Vec<int> batch;
    {
        LockGuard g(sharedMutex_);
        batch = Move(discoveredPending_);
        discoveredPending_ = Vec<int>();
    }
    if (jobMode_ || createMode_ || batch.Empty()) return;
    const FilterSpec f = CurrentFilter();
    if (f.duplicates) return; // duplicates are computed from finished hashes (re-apply the filter)
    if (FilterActive(f)) {
        for (const int i : batch)
            if (MatchesFilter(store_, i, f)) display_.Push(i);
    } else {
        for (const int i : batch) display_.Push(i);
    }
    RefreshItemCount(false);
}

void MainWindow::OnLargeFiles() {
    __atomic_store_n(&largePosted_, false, __ATOMIC_RELEASE);
    Vec<LargeFile> snapshot;
    {
        LockGuard g(sharedMutex_);
        snapshot = largeFiles_;
        Vec<LargeFile> kept;
        for (LargeFile& f : largeFiles_)
            if (!f.ended) kept.Push(Move(f));
        largeFiles_ = Move(kept);
    }
    if (createMode_) { // mini window: second bar = current large file
        double pct = 0;
        for (const LargeFile& f : snapshot)
            if (!f.ended && f.percent > pct) pct = f.percent;
        SendMessageW(progressFile_, PBM_SETPOS, static_cast<WPARAM>(pct), 0);
        return;
    }
    if (jobMode_ || !active_) return;
    for (const LargeFile& f : snapshot) {
        LVFINDINFOW fi{};
        fi.flags = LVFI_PARAM;
        fi.lParam = f.index;
        const int row = ListView_FindItem(active_, -1, &fi);
        if (f.ended) {
            if (row >= 0) ListView_DeleteItem(active_, row);
            continue;
        }
        const text::WStr pct = f.percent > 0 ? W(text::FormatF1(f.percent) + "%") : W("Starting...");
        if (row < 0) {
            const text::WStr name = W(f.name);
            LVITEMW it{};
            it.mask = LVIF_TEXT | LVIF_PARAM;
            it.iItem = ListView_GetItemCount(active_);
            it.pszText = const_cast<wchar_t*>(name.C());
            it.lParam = f.index;
            const int r = ListView_InsertItem(active_, &it);
            ListView_SetItemText(active_, r, 1, const_cast<wchar_t*>(pct.C()));
        } else {
            ListView_SetItemText(active_, row, 1, const_cast<wchar_t*>(pct.C()));
        }
    }
    ToggleActiveJobsPanel(ListView_GetItemCount(active_) > 0);
}

void MainWindow::OnVerifyLoaded() {
    Vec<int> order;
    Str comments;
    {
        LockGuard g(sharedMutex_);
        order = Move(verifyOrder_);
        verifyOrder_ = Vec<int>();
        comments = Move(verifyComments_);
        verifyComments_ = Str();
    }
    display_ = Move(order);
    RefreshItemCount(true);
    UpdateStats(0, store_.Count(), 0, 0, 0);
    SendMessageW(progress_, PBM_SETRANGE32, 0, Max(1, store_.Count()));
    commentsText_ = comments;
    Str shown;
    for (const Str& l : text::SplitLines(comments)) shown += l + "\r\n";
    SetWindowTextUtf8(txtComments_, shown);
    ShowWindow(commentsPanel_, text::IsNullOrWhiteSpace(comments) ? SW_HIDE : SW_SHOW);
    Layout();
}

void MainWindow::OnDriveDecided() {
    DriveDecision d;
    {
        LockGuard g(sharedMutex_);
        d = driveDecision_;
    }
    if (createMode_ || jobMode_) return;
    const char* what = runKind_ == RunKind::Verify ? "SharpSFV - Verifying... [" : "SharpSFV - Creating... [";
    SetWindowTextUtf8(hwnd_, Str(what) + (d.isHdd ? "HDD/Seq" : "SSD/Par") + "]");
    if (Automatic(d.source) && d.detectionFailed) {
        Vec<Str> w = warnings_;
        w.Push("Drive type not detected: using HDD mode (see Options > Processing Mode)");
        SetWarnings(Move(w));
    }
}

void MainWindow::OnRunDone() {
    if (worker_.Running()) worker_.Join();
    RunResult r;
    Str error;
    {
        LockGuard g(sharedMutex_);
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

    if (!error.Empty()) {
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

    const int total = Max(1, store_.Count());
    SetBarImmediately(progress_, Min(r.completed, total), total);
    const int pbState = (r.bad > 0 || r.missing > 0) ? PBST_ERROR : (cancelled ? PBST_PAUSED : PBST_NORMAL);
    SetProgressBarState(pbState);
    taskbar_.SetValue(static_cast<ULONGLONG>(r.completed), static_cast<ULONGLONG>(total));
    taskbar_.SetState(pbState == PBST_ERROR ? TBPF_ERROR : (cancelled ? TBPF_PAUSED : TBPF_NORMAL));

    if (createMode_) {
        SendMessageW(progressFile_, PBM_SETPOS, 100, 0);
        if (!cancelled) {
            Str err;
            if (!WriteCreateMode(autoSavePath_, store_, settings_, currentAlgo_, &err))
                MsgBox(hwnd_, "Error saving: " + err, "SharpSFV", MB_OK | MB_ICONERROR);
        }
        DestroyWindow(hwnd_); // done (or cancelled): the mini window closes
        return;
    }

    RecalculateColumnWidths();
    ApplyNameColumnWidth(); // grows the name column for long names (full paths: fits them)
    UpdateStats(r.completed, store_.Count(), r.ok, r.bad, r.missing);
    const Str algo = ToString(currentAlgo_);
    if (cancelled) {
        SetWindowTextUtf8(hwnd_, "SharpSFV - Cancelled [" + algo + "]");
    } else if (verify) {
        SetWindowTextUtf8(hwnd_, r.bad == 0 && r.missing == 0
                                     ? "SharpSFV [" + algo + "] - All Files OK"
                                     : "SharpSFV [" + algo + "] - " + ToStr(r.bad) + " Bad, " + ToStr(r.missing) + " Missing");
    } else {
        SetWindowTextUtf8(hwnd_, "SharpSFV - Creation Complete [" + algo + "]");
    }
    if (settings_.showTimeTab && !cancelled) {
        SetWindowTextUtf8(lblTime_, "Total Elapsed Time: " + ToStr(TickMs() - runStart_) + " ms");
        ShowWindow(lblTime_, SW_SHOW);
    }
    // Replaces "Legacy SFV (Endian Mismatch) Detected": report the byte orders found.
    if (verify && currentAlgo_ == HashType::Crc32 && r.crcReversedMatches > 0) {
        Vec<Str> w = warnings_;
        w.Push("CRC-32 byte order: " + ToStr(r.crcStandardMatches) + " standard, " + ToStr(r.crcReversedMatches) +
               " reversed");
        SetWarnings(Move(w));
    }
    const FilterSpec f = CurrentFilter();
    if (f.duplicates) ApplyFilter();
    LayoutStatsLabels();
    InvalidateRect(list_, nullptr, FALSE);
    SyncMenu();
}

// ------------------------------------------------------------------ mini (create) mode

void MainWindow::OnPipePaths() {
    for (Str& p : pipeServer_.TakePaths()) {
        if (processing_) continue; // too late for this run (as in the original)
        collected_.Push(Move(p));
    }
    if (!createLaunched_) SetTimer(hwnd_, TIMER_CREATE, 200, nullptr); // restart the debounce
}

void MainWindow::LaunchCreateMode() {
    if (createLaunched_) return;
    createLaunched_ = true;
    for (Str& p : pipeServer_.TakePaths()) collected_.Push(Move(p));
    if (collected_.Empty()) {
        DestroyWindow(hwnd_);
        return;
    }
    Str baseDir;
    if (collected_.Size() == 1 && fs::DirectoryExists(collected_[0])) baseDir = collected_[0];
    else if (collected_.Size() == 1) baseDir = path::GetDirectoryName(collected_[0]).ValueOr(Str());
    else baseDir = path::FindCommonBasePath(collected_);

    int filterIndex = 1;
    switch (settings_.defaultAlgo) {
        case HashType::MD5: filterIndex = 2; break;
        case HashType::SHA1: filterIndex = 3; break;
        case HashType::SHA256: filterIndex = 4; break;
        case HashType::XXHASH3: filterIndex = 5; break;
        default: break;
    }
    const Opt<SaveResult> chosen = SaveFileDialog(nullptr,
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

    for (Str& p : pipeServer_.TakePaths()) collected_.Push(Move(p)); // arrived during the dialog

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
