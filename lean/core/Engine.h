// SharpSFV (lean C++ rewrite) - the hashing engine.
// Port of src/core/Engine.* (HashFiles, Verify, CountFiles, PopulateVerificationStore,
// ProgressThrottle, pause, the observer the GUI uses, and the job-queue runner).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: parallel hashing for SSDs (producer/consumer threads).
// Modified 2026-10-04: pause, observer, CountFiles and JobRunner (for the GUI).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
//
// As in the standard build: the calling thread lists the files (the producer) and worker
// threads hash them (the consumers): one worker with 8 MB reads on an HDD, one per logical
// processor with 512 KB reads on an SSD. The results do not depend on the number of workers,
// because files are stored and written in discovery order.
#pragma once

#include "../base/Str.h"
#include "../base/Thread.h"
#include "../base/Vec.h"
#include "ChecksumFile.h"
#include "JobStore.h"
#include "Settings.h"
#include "Store.h"
#include "Types.h"

namespace sfv {

inline constexpr i64 kLargeFileThreshold = 50LL * 1024 * 1024; // 50 MB

struct Counters {
    int completed = 0, total = 0, ok = 0, bad = 0, missing = 0;
};

struct ProgressInfo {
    int current = 0, total = 0, ok = 0, bad = 0, missing = 0;
    Str speed; // "Speed: 12.3 MB/s | ETA: 00:01:05", or "" when throughput stats are off
};

// ThrottledUiUpdate: throughput every ~500 ms, at most one update per 100 ms except the last.
// Update may be called from several threads; the presenter runs under a lock, so it never
// runs twice at once, and a frame older than the one already shown is dropped.
class ProgressThrottle {
public:
    using Presenter = void (*)(void* ctx, const ProgressInfo& info);
    void SetPresenter(Presenter p, void* ctx) { presenter_ = p, ctx_ = ctx; }
    void SetShowThroughput(bool on) { showThroughput_ = on; }
    void ResetMetrics(); // before a run (not thread-safe)
    void Update(int current, int total, int ok, int bad, int missing);

    Str SpeedString(); // the last throughput text (for the GUI's job statistics)

    void AddDetected(i64 n) { __atomic_fetch_add(&totalBytesDetected_, n, __ATOMIC_RELAXED); }
    void AddProcessed(i64 n) { __atomic_fetch_add(&processedBytes_, n, __ATOMIC_RELAXED); }
    i64 TotalBytesDetected() const { return __atomic_load_n(&totalBytesDetected_, __ATOMIC_RELAXED); }
    i64 ProcessedBytes() const { return __atomic_load_n(&processedBytes_, __ATOMIC_RELAXED); }

private:
    Mutex lock_;
    Presenter presenter_ = nullptr;
    void* ctx_ = nullptr;
    bool showThroughput_ = false;
    i64 totalBytesDetected_ = 0;
    i64 processedBytes_ = 0;
    i64 lastUiTick_ = 0;
    i64 lastSpeedTick_ = 0;
    i64 lastSpeedBytes_ = 0;
    int lastShown_ = -1;
    Str cachedSpeed_;
};

struct HashRunOptions {
    HashType algo = HashType::XXHASH3;
    bool isHdd = true; // HDD: one worker, 8 MB reads; SSD: one worker per processor, 512 KB
    bool updateFileList = false;   // report discovered items to the observer (GUI list)
    bool reportLargeFiles = false; // report files over 50 MB to the observer (Active Jobs)
    bool applyFilters = false; // ShowAdvancedBar: include/exclude patterns active
    Str includePattern, excludePattern;
    bool recursive = true;
    Str skipPath; // never hash this file (the checksum file being written); "" = none
};

struct RunResult {
    int completed = 0, ok = 0, bad = 0, missing = 0;
    bool cancelled = false;
    int crcStandardMatches = 0; // CRC-32 entries that matched in each byte order
    int crcReversedMatches = 0;
};

using ProgressFn = FnRef<void(const Counters&)>;

class Engine;

// ManualResetEventSlim used as a pause gate (set = running).
class PauseGate {
public:
    void Pause();
    void Resume();
    bool IsPaused() const { return __atomic_load_n(&paused_, __ATOMIC_ACQUIRE); }
    // Blocks while paused. False if cancelled (before or while waiting).
    bool Wait(const Engine& engine);
    void WakeAll();

private:
    bool paused_ = false;
    Mutex m_;
    CondVar cv_;
};

// Notifications from the engine's threads (any of them) to the GUI.
class EngineObserver {
public:
    virtual ~EngineObserver() = default;
    // Newly listed store indices (creation with updateFileList): up to 2000 or every 250 ms.
    virtual void OnItemsDiscovered(Vec<int>& indices) { (void)indices; }
    // Files over 50 MB, with reportLargeFiles (the "Active Jobs" panel).
    virtual void OnLargeFileBegin(int index, StrView fileName) { (void)index, (void)fileName; }
    virtual void OnLargeFileProgress(int index, double percent) { (void)index, (void)percent; }
    virtual void OnLargeFileEnd(int index) { (void)index; }
};

class JobQueue; // Engine.cpp

class Engine {
public:
    Engine(FileStore& store, ProgressThrottle& throttle, EngineObserver* observer = nullptr)
        : store_(store), throttle_(throttle), observer_(observer) {}

    // Enumerates `paths` (files and folders), adds every file to the store (relative to
    // baseDirectory) and hashes it.
    RunResult HashFiles(const Vec<Str>& paths, StrView baseDirectory, const HashRunOptions& opt, ProgressFn progress);
    // Verifies the store's entries (expected hashes) in `order`.
    RunResult Verify(const Vec<int>& order, const HashRunOptions& opt, ProgressFn progress);
    // Exact file count for a set of inputs, honouring the filters (stops early when cancelled).
    int CountFiles(const Vec<Str>& paths, const HashRunOptions& opt) const;

    void Pause() { pause_.Pause(); }
    void Resume() { pause_.Resume(); }
    bool IsPaused() const { return pause_.IsPaused(); }
    // Clears a previous Cancel(); whoever starts a run calls it (runs do not reset it, so a
    // Cancel() during a job's counting phase is not lost when its hashing starts).
    void ResetCancel() { __atomic_store_n(&cancelled_, false, __ATOMIC_RELEASE); }
    FileStore& Store() { return store_; }
    ProgressThrottle& Throttle() { return throttle_; }

    // Thread-safe: stops the run in progress (and any later run of this engine).
    void Cancel();
    bool IsCancelled() const {
        return __atomic_load_n(&cancelled_, __ATOMIC_ACQUIRE) || (externalCancel_ && externalCancel_());
    }
    // Polled during a run (the CLI passes its Ctrl+C flag).
    void SetExternalCancel(bool (*check)()) { externalCancel_ = check; }

    // Workers a run uses: 1 for an HDD, ProcessorCount() for an SSD. A test can set it.
    void SetWorkerCountOverride(int n) { workerOverride_ = n; }
    int WorkerCount(bool isHdd) const;

private:
    struct Worker;
    bool HashOne(int index, const Str& fullPath, Worker& w, bool reportLarge, Digest& out);
    void SetActiveQueue(JobQueue* q);

    FileStore& store_;
    ProgressThrottle& throttle_;
    EngineObserver* observer_;
    PauseGate pause_;
    bool cancelled_ = false;
    bool (*externalCancel_)() = nullptr;
    int workerOverride_ = 0;
    Mutex queueLock_;
    JobQueue* activeQueue_ = nullptr;
};

// One store entry per parsed line, relative to the checksum file's folder; returns the
// processing order: file order (headless) or sorted by full path, case-insensitively (GUI).
Vec<int> PopulateVerificationStore(FileStore& store, const ParsedChecksumFile& parsed, StrView baseFolder,
                                   bool sortByPath = false);

class DriveResolver;

// Job Queue mode (ProcessJobQueue): runs until no job is Queued or the engine is cancelled.
class JobRunner {
public:
    struct Hooks {
        void* ctx = nullptr;
        void (*jobsChanged)(void* ctx) = nullptr;                // repaint the job list
        void (*log)(void* ctx, StrView message) = nullptr;       // headless messages
        void (*progress)(void* ctx, const Counters& c) = nullptr; // overall progress (exact totals)
    };
    // createMode: launched from the Explorer context menu (affects recursion).
    static void Run(JobStore& jobs, Engine& engine, const AppSettings& settings, HashType algo, bool createMode,
                    const DriveResolver& drives, const Hooks& hooks);
};

} // namespace sfv
