// SharpSFV (C++ port) - hashing engine.
// Ported from Form1.Processing.cs (ExecuteHashingEngine, RunVerification's engine part,
// ThrottledUiUpdate, ProcessJobQueue, CountFilesAsync), Form1.Processing.Helpers.cs
// (ProcessFileEntry) and IO/ProgressStream.cs.
//
// Architecture (unchanged from the original):
//   Producer : one thread enumerates files and pushes jobs into a bounded queue
//              (50,000 slots; the producer blocks when it is full = backpressure).
//   Consumers: 1 thread on HDD (8 MB reads) or one per CPU on SSD (512 KB reads). Each
//              owns its buffer and hasher for the whole run (no per-file allocation).
// System.Threading.Channels -> BoundedQueue, ManualResetEventSlim -> PauseGate,
// CancellationTokenSource -> atomic flag, ArrayPool buffers -> per-thread buffers.
//
// The engine knows nothing about windows or consoles; it reports through EngineObserver
// and ProgressThrottle so the Win32 GUI and the headless CLI share it unchanged.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
// Modified 2026-10-03: ResetCancel; native thread primitives (Threading.h).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "AppSettings.h"
#include "ChecksumParser.h"
#include "FileStore.h"
#include "JobStore.h"
#include "Types.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include "Threading.h"
#include <string>
#include <vector>

namespace sfv {

class DriveResolver;

inline constexpr std::int64_t kLargeFileThreshold = 50LL * 1024 * 1024; // 50 MB

std::int64_t TickMs(); // Environment.TickCount64 equivalent (monotonic)

// ManualResetEventSlim used as a pause gate (set = running).
class PauseGate {
public:
    void Pause();
    void Resume();
    bool IsPaused() const { return paused_.load(std::memory_order_acquire); }
    // Blocks while paused. Returns false if cancelled while waiting (or already cancelled).
    bool Wait(const std::atomic<bool>& cancelled);
    void WakeAll(); // used by cancellation

private:
    std::atomic<bool> paused_{false};
    Mutex m_;
    CondVar cv_;
};

// Snapshot handed to the presenter.
struct ProgressInfo {
    int current = 0;
    int total = 0; // <= 0: unknown (the GUI then uses FileStore.Count)
    int ok = 0, bad = 0, missing = 0;
    std::string speed; // "Speed: 12.3 MB/s | ETA: 00:01:05" or "" when stats are off
};

// Port of ThrottledUiUpdate: computes throughput every ~500 ms, limits presentation to one
// update per 100 ms (except the final one) and drops frames while the presenter is busy.
class ProgressThrottle {
public:
    // The presenter MUST call Release() when it has finished presenting. A synchronous
    // presenter (console) calls it at the end; the GUI posts a message and calls it from
    // the UI thread once painted.
    using Presenter = std::function<void(const ProgressInfo&)>;

    void SetPresenter(Presenter p) { presenter_ = std::move(p); }
    void SetShowThroughput(bool on) { showThroughput_.store(on); }
    void ResetMetrics();
    void Update(int current, int total, int ok, int bad, int missing);
    void Release() { busy_.store(0, std::memory_order_release); }
    std::string SpeedString() const;

    std::atomic<std::int64_t> totalBytesDetected{0};
    std::atomic<std::int64_t> processedBytes{0};

private:
    Presenter presenter_;
    std::atomic<bool> showThroughput_{false};
    std::atomic<int> busy_{0};
    std::atomic<std::int64_t> lastUiTick_{0};
    mutable Mutex speedMutex_;
    std::int64_t lastSpeedTick_ = 0;
    std::int64_t lastSpeedBytes_ = 0;
    std::string cachedSpeed_;
};

// Notifications from worker threads. All callbacks may run on any engine thread.
class EngineObserver {
public:
    virtual ~EngineObserver() = default;
    // Newly discovered store indices (creation with updateFileList). Batched: up to 2000
    // items or every 250 ms.
    virtual void OnItemsDiscovered(std::vector<int> /*indices*/) {}
    // Files over 50 MB (the "Active Jobs" panel), when large-file reporting is enabled.
    virtual void OnLargeFileBegin(int /*index*/, const std::string& /*fileName*/) {}
    virtual void OnLargeFileProgress(int /*index*/, double /*percent*/) {}
    virtual void OnLargeFileEnd(int /*index*/) {}
};

struct Counters {
    int completed = 0, total = 0, ok = 0, bad = 0, missing = 0;
};
using CountersCallback = std::function<void(const Counters&)>;

struct HashRunOptions {
    HashType algo = HashType::XXHASH3;
    bool isHdd = true;
    bool updateFileList = false;    // emit OnItemsDiscovered batches
    bool reportLargeFiles = false;  // emit OnLargeFile* events
    bool applyFilters = false;      // ShowAdvancedBar: include/exclude patterns active
    std::string includePattern, excludePattern;
    bool recursive = true;
    // Never hash this file (the checksum file being written by the CLI). Case-insensitive
    // on Windows. Empty = none.
    std::string skipPath;
};

struct RunResult {
    int completed = 0, ok = 0, bad = 0, missing = 0;
    bool cancelled = false;
    // CRC-32 verification: how many matching entries were written in each byte order.
    // Both non-zero = a file mixing QuickSFV-style and C#-SharpSFV-style values.
    int crcStandardMatches = 0;
    int crcReversedMatches = 0;
};

class Engine {
public:
    Engine(FileStore& store, ProgressThrottle& throttle, EngineObserver* observer = nullptr);

    // ExecuteHashingEngine: enumerates `paths` (files and folders), adds every file to the
    // store (relative to baseDirectory) and hashes it. Store should be cleared first.
    RunResult HashFiles(const std::vector<std::string>& paths, const std::string& baseDirectory,
                        const HashRunOptions& opt, const CountersCallback& progress);

    // Verification engine. The store must already hold the parsed entries (expected
    // hashes); `order` is the processing order (display order in the GUI).
    RunResult Verify(const std::vector<int>& order, const HashRunOptions& opt, const CountersCallback& progress);

    // Exact file count for a set of inputs (CountFilesAsync), honouring filters. Stops early
    // (returning the count so far) when cancelled.
    int CountFiles(const std::vector<std::string>& paths, const HashRunOptions& opt) const;

    void Pause() { pause_.Pause(); }
    void Resume() { pause_.Resume(); }
    bool IsPaused() const { return pause_.IsPaused(); }
    void Cancel();
    bool IsCancelled() const { return cancelled_.load(); }
    // Clears a previous Cancel(). Called by whoever starts a run (a new Engine starts
    // un-cancelled). HashFiles/Verify do not reset it themselves, so a Cancel() during a
    // job's counting phase is not lost when its hashing starts.
    void ResetCancel() { cancelled_.store(false); }

    FileStore& Store() { return store_; }
    ProgressThrottle& Throttle() { return throttle_; }

private:
    struct Worker;
    bool HashOne(int index, const std::string& fullPath, Worker& w, bool reportLarge, Digest& out);

    FileStore& store_;
    ProgressThrottle& throttle_;
    EngineObserver* observer_;
    PauseGate pause_;
    std::atomic<bool> cancelled_{false};
    Mutex abortMutex_;
    std::function<void()> abortQueue_;
};

// Creates the populated store for a verification run: one entry per parsed line,
// relative to the checksum file's folder. Returns the processing order: sorted by full
// path (case-insensitive) when sortByPath (GUI), file order otherwise (headless).
std::vector<int> PopulateVerificationStore(FileStore& store, const ParsedChecksumFile& parsed,
                                           const std::string& baseFolder, bool sortByPath);

// Job Queue mode (ProcessJobQueue). Runs until no job is Queued or the engine is cancelled.
class JobRunner {
public:
    struct Hooks {
        std::function<void()> jobsChanged;                         // repaint the job list (throttled by caller)
        std::function<void(const std::string&)> log;               // headless messages
        std::function<void(const Counters&)> progress;             // overall progress (exact totals)
    };

    // createMode: the app was launched from the Explorer context menu (affects recursion).
    // drives decides HDD/SSD per job (settings.processingMode + DriveType.ini).
    static void Run(JobStore& jobs, Engine& engine, const AppSettings& settings, HashType algo, bool createMode,
                    const DriveResolver& drives, const Hooks& hooks);
};

} // namespace sfv
