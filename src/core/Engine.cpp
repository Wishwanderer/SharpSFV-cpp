// SharpSFV (C++ port) - hashing engine.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
// Modified 2026-10-03: cancel handling for the GUI; native threads; ETA without printf.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Engine.h"
#include "SortUtil.h"

#include "ChecksumWriter.h"
#include "DriveClassifier.h"
#include "FileFilter.h"
#include "FileSystem.h"
#include "Hasher.h"
#include "PathUtil.h"
#include "Text.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <unordered_set>

namespace sfv {

namespace {

constexpr std::size_t kQueueCapacity = 50000;
constexpr std::size_t kHddBuffer = 8u * 1024 * 1024;
constexpr std::size_t kSsdBuffer = 512u * 1024;
constexpr std::size_t kUiBatchSize = 2000;
constexpr std::int64_t kUiBatchMs = 250;

struct FileJob {
    int index = -1;
    std::string fullPath;
};

// Bounded multi-producer/multi-consumer queue (Channel.CreateBounded with FullMode.Wait).
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

    bool Push(T&& v) {
        std::unique_lock lk(m_);
        notFull_.wait(lk, [&] { return q_.size() < capacity_ || aborted_; });
        if (aborted_) return false;
        q_.push_back(std::move(v));
        notEmpty_.notify_one();
        return true;
    }
    bool Pop(T& out) {
        std::unique_lock lk(m_);
        notEmpty_.wait(lk, [&] { return !q_.empty() || closed_ || aborted_; });
        if (aborted_ || q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop_front();
        notFull_.notify_one();
        return true;
    }
    void Close() {
        std::lock_guard lk(m_);
        closed_ = true;
        notEmpty_.notify_all();
    }
    void Abort() {
        std::lock_guard lk(m_);
        aborted_ = true;
        notFull_.notify_all();
        notEmpty_.notify_all();
    }

private:
    Mutex m_;
    CondVar notFull_, notEmpty_;
    std::deque<T> q_;
    std::size_t capacity_;
    bool closed_ = false, aborted_ = false;
};

int WorkerCount(bool isHdd) {
    if (isHdd) return 1;
    const unsigned n = Thread::hardware_concurrency();
    return n == 0 ? 1 : static_cast<int>(n);
}

// Relative path of a discovered file (ProcessFileEntry). The original sliced off the base
// directory whenever the path merely started with it, so base "C:\Foo" turned
// "C:\FooBar\x" into "Bar\x"; and with an empty base (inputs on different drives) it
// dropped the leading separator of UNC paths. Both cases now take the correct route.
std::string ComputeRelativePath(const std::string& fullPath, const std::string& baseDir) {
    if (baseDir.empty()) return fullPath;
    if (fullPath.size() > baseDir.size() &&
        text::EqualsIgnoreCase(std::string_view(fullPath).substr(0, baseDir.size()), baseDir)) {
        const std::size_t off = baseDir.size();
        if (path::IsSep(fullPath[off])) return fullPath.substr(off + 1);
        if (path::IsSep(baseDir.back())) return fullPath.substr(off);
    }
    if (auto rel = path::GetRelativePath(baseDir, fullPath)) return *rel;
    return std::string(path::GetFileName(fullPath));
}

std::int64_t ElapsedMs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

std::int64_t TickMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// ------------------------------------------------------------------ PauseGate

void PauseGate::Pause() {
    std::lock_guard lk(m_);
    paused_.store(true, std::memory_order_release);
}

void PauseGate::Resume() {
    {
        std::lock_guard lk(m_);
        paused_.store(false, std::memory_order_release);
    }
    cv_.notify_all();
}

bool PauseGate::Wait(const std::atomic<bool>& cancelled) {
    if (cancelled.load()) return false;
    if (!paused_.load(std::memory_order_acquire)) return true;
    std::unique_lock lk(m_);
    cv_.wait(lk, [&] { return !paused_.load() || cancelled.load(); });
    return !cancelled.load();
}

void PauseGate::WakeAll() {
    std::lock_guard lk(m_);
    cv_.notify_all();
}

// ------------------------------------------------------------------ ProgressThrottle

void ProgressThrottle::ResetMetrics() {
    totalBytesDetected.store(0);
    processedBytes.store(0);
    std::lock_guard lk(speedMutex_);
    lastSpeedBytes_ = 0;
    lastSpeedTick_ = TickMs();
    cachedSpeed_.clear();
}

std::string ProgressThrottle::SpeedString() const {
    std::lock_guard lk(speedMutex_);
    return cachedSpeed_;
}

void ProgressThrottle::Update(int current, int total, int ok, int bad, int missing) {
    const std::int64_t now = TickMs();
    const bool showStats = showThroughput_.load();

    // 1. Throughput and ETA, every ~500 ms.
    if (showStats) {
        std::unique_lock lk(speedMutex_, std::try_to_lock);
        if (lk.owns_lock() && now - lastSpeedTick_ > 500) {
            const std::int64_t cur = processedBytes.load();
            const std::int64_t bytesDelta = cur - lastSpeedBytes_;
            const std::int64_t timeDelta = now - lastSpeedTick_;
            if (timeDelta > 0) {
                const double bps = static_cast<double>(bytesDelta) / static_cast<double>(timeDelta) * 1000.0;
                const double mbps = bps / (1024.0 * 1024.0);
                std::string eta = "--:--";
                const std::int64_t totalBytes = totalBytesDetected.load();
                if (bps > 0 && totalBytes > cur) {
                    const double secs = static_cast<double>(totalBytes - cur) / bps;
                    if (secs < 86400) { // TimeSpan.FromSeconds(...).ToString(@"hh\:mm\:ss")
                        const long long s = std::llround(secs * 1000.0) / 1000;
                        auto two = [](long long n) { return std::string{static_cast<char>('0' + n / 10), static_cast<char>('0' + n % 10)}; };
                        eta = two((s / 3600) % 24) + ":" + two((s / 60) % 60) + ":" + two(s % 60);
                    }
                }
                cachedSpeed_ = "Speed: " + text::FormatF1(mbps) + " MB/s | ETA: " + eta;
            }
            lastSpeedTick_ = now;
            lastSpeedBytes_ = cur;
        }
    }

    // 2. Time-based throttle (the final update always goes through).
    if (current < total && now - lastUiTick_.load() < 100) return;

    // 3. Drop the frame if the presenter is still busy with the previous one.
    int expected = 0;
    if (!busy_.compare_exchange_strong(expected, 1)) return;
    lastUiTick_.store(now);

    ProgressInfo info{current, total, ok, bad, missing, showStats ? SpeedString() : std::string()};
    if (presenter_)
        presenter_(info);
    else
        Release();
}

// ------------------------------------------------------------------ Engine

struct Engine::Worker {
    std::unique_ptr<Hasher> hasher;
    std::unique_ptr<std::uint8_t[]> buffer;
    std::size_t capacity = 0;

    Worker(HashType algo, std::size_t cap)
        : hasher(Hasher::Create(algo)), buffer(std::make_unique_for_overwrite<std::uint8_t[]>(cap)), capacity(cap) {}
};

Engine::Engine(FileStore& store, ProgressThrottle& throttle, EngineObserver* observer)
    : store_(store), throttle_(throttle), observer_(observer) {}

void Engine::Cancel() {
    cancelled_.store(true);
    pause_.WakeAll();
    std::lock_guard lk(abortMutex_);
    if (abortQueue_) abortQueue_();
}

bool Engine::HashOne(int index, const std::string& fullPath, Worker& w, bool reportLarge, Digest& out) {
    if (!w.hasher) return false; // crypto provider unavailable -> ERROR (as the C# null algorithm did)

    fs::FileReader reader;
    if (!reader.Open(fullPath)) return false;
    const std::int64_t len = reader.Size();
    const bool large = len > kLargeFileThreshold;
    const bool announce = large && reportLarge && observer_;
    if (announce) observer_->OnLargeFileBegin(index, std::string(path::GetFileName(fullPath)));

    // ProgressStream throttling: report at 0.1% steps, at most every 100 ms.
    std::int64_t step = len / 1000;
    if (step == 0) step = 64 * 1024;
    std::int64_t readTotal = 0, sinceReport = 0, lastTick = TickMs();

    w.hasher->Reset();
    bool ok = true;
    for (;;) {
        const std::int64_t n = reader.Read(w.buffer.get(), w.capacity);
        if (n < 0) {
            ok = false;
            break;
        }
        if (n == 0) break;
        w.hasher->Update(w.buffer.get(), static_cast<std::size_t>(n));
        throttle_.processedBytes.fetch_add(n, std::memory_order_relaxed);
        if (large) {
            readTotal += n;
            sinceReport += n;
            if (sinceReport >= step) {
                const std::int64_t now = TickMs();
                if (now - lastTick >= 100) {
                    sinceReport = 0;
                    lastTick = now;
                    if (announce)
                        observer_->OnLargeFileProgress(index, static_cast<double>(readTotal) / static_cast<double>(len) * 100.0);
                }
            }
            if (cancelled_.load()) { // big files can take minutes; stop promptly
                ok = false;
                break;
            }
        }
    }
    if (announce) observer_->OnLargeFileEnd(index);
    return ok && w.hasher->Final(out);
}

RunResult Engine::HashFiles(const std::vector<std::string>& paths, const std::string& baseDirectory,
                            const HashRunOptions& opt, const CountersCallback& progress) {
    BoundedQueue<FileJob> queue(kQueueCapacity);
    {
        std::lock_guard lk(abortMutex_);
        abortQueue_ = [&queue] { queue.Abort(); };
    }

    std::atomic<int> completed{0}, okCount{0}, errorCount{0}, queued{0};
    const int workers = WorkerCount(opt.isHdd);
    const std::size_t bufferSize = opt.isHdd ? kHddBuffer : kSsdBuffer;

    // ---------------- Producer
    Thread producer([&] {
        try {
            int originalIndex = 0;
            std::vector<int> uiBatch;
            uiBatch.reserve(kUiBatchSize);
            std::int64_t lastBatch = TickMs();
            std::unordered_set<std::string> processedPaths; // case-folded keys (OrdinalIgnoreCase)
            const std::string skipKey =
                opt.skipPath.empty() ? std::string() : text::FoldCase(path::GetFullPath(opt.skipPath));
            const std::string skipName(path::GetFileName(opt.skipPath));

            auto flush = [&] {
                if (uiBatch.empty() || !opt.updateFileList) return;
                if (observer_) observer_->OnItemsDiscovered(std::move(uiBatch));
                uiBatch = {};
                uiBatch.reserve(kUiBatchSize);
                lastBatch = TickMs();
            };

            // ProcessFileEntry. Returns false to stop producing.
            auto processEntry = [&](const std::string& fullPath, int origIndex) -> bool {
                if (!processedPaths.insert(text::FoldCase(fullPath)).second) return true; // duplicate
                if (!skipKey.empty() && text::EqualsIgnoreCase(path::GetFileName(fullPath), skipName) &&
                    text::FoldCase(path::GetFullPath(fullPath)) == skipKey)
                    return true;
                if (!pause_.Wait(cancelled_)) return false;
                const std::string_view fileName = path::GetFileName(fullPath);
                if (opt.applyFilters && !IsFileAllowed(fileName, opt.includePattern, opt.excludePattern)) return true;

                const std::string rel = ComputeRelativePath(fullPath, baseDirectory);
                const int idx = store_.Add(fileName, rel, baseDirectory, origIndex, ItemStatus::Queued);
                uiBatch.push_back(idx);
                queued.fetch_add(1);
                return queue.Push(FileJob{idx, fullPath});
            };

            for (const std::string& p : paths) {
                if (cancelled_.load() || !pause_.Wait(cancelled_)) break;

                if (fs::FileExists(p)) {
                    const std::int64_t size = fs::GetFileSize(p);
                    if (size > 0) throttle_.totalBytesDetected.fetch_add(size);
                    if (!processEntry(p, originalIndex++)) break;
                } else if (fs::DirectoryExists(p)) {
                    bool stop = false;
                    fs::EnumerateFiles(p, opt.recursive, [&](const std::string& file, std::int64_t size) {
                        if (cancelled_.load() || !pause_.Wait(cancelled_)) {
                            stop = true;
                            return false;
                        }
                        throttle_.totalBytesDetected.fetch_add(size);
                        if (!processEntry(file, originalIndex++)) {
                            stop = true;
                            return false;
                        }
                        if (opt.updateFileList && (uiBatch.size() >= kUiBatchSize || TickMs() - lastBatch > kUiBatchMs))
                            flush();
                        return true;
                    });
                    if (stop) break;
                }
            }
            flush();
        } catch (...) {
            // e.g. store capacity exceeded: stop producing, let consumers drain.
        }
        queue.Close();
    });

    // ---------------- Consumers
    auto consume = [&] {
        Worker w(opt.algo, bufferSize);
        FileJob job;
        while (queue.Pop(job)) {
            if (!pause_.Wait(cancelled_)) return;

            const auto start = std::chrono::steady_clock::now();
            Digest digest;
            const bool ok = HashOne(job.index, job.fullPath, w, opt.reportLargeFiles, digest);
            const std::int64_t ms = ElapsedMs(start);
            if (cancelled_.load()) return; // leave the interrupted file as Queued

            if (ok) {
                okCount.fetch_add(1);
                store_.SetResult(job.index, &digest, ms, ItemStatus::OK);
            } else {
                errorCount.fetch_add(1);
                store_.SetResult(job.index, nullptr, ms, ItemStatus::Error);
            }
            const int cur = completed.fetch_add(1) + 1;
            if (progress) progress(Counters{cur, queued.load(), okCount.load(), errorCount.load(), 0});
        }
    };

    std::vector<Thread> consumers;
    consumers.reserve(static_cast<std::size_t>(workers));
    for (int i = 0; i < workers; ++i) consumers.emplace_back(consume);
    for (auto& t : consumers) t.join();
    queue.Abort(); // releases the producer if consumers stopped early
    producer.join();

    {
        std::lock_guard lk(abortMutex_);
        abortQueue_ = nullptr;
    }

    RunResult r;
    r.completed = completed.load();
    r.ok = okCount.load();
    r.bad = errorCount.load();
    r.cancelled = cancelled_.load();
    return r;
}

RunResult Engine::Verify(const std::vector<int>& order, const HashRunOptions& opt, const CountersCallback& progress) {
    BoundedQueue<FileJob> queue(kQueueCapacity);
    {
        std::lock_guard lk(abortMutex_);
        abortQueue_ = [&queue] { queue.Abort(); };
    }

    std::atomic<int> completed{0}, okCount{0}, badCount{0}, missingCount{0};
    std::atomic<int> crcStandard{0}, crcReversed{0};
    const int total = static_cast<int>(order.size());
    const int workers = WorkerCount(opt.isHdd);
    const std::size_t bufferSize = opt.isHdd ? kHddBuffer : kSsdBuffer;

    auto report = [&](int cur) {
        if (progress) progress(Counters{cur, total, okCount.load(), badCount.load(), missingCount.load()});
    };

    // ---------------- Producer: existence check first, then queue for hashing.
    Thread producer([&] {
        for (const int idx : order) {
            if (cancelled_.load() || !pause_.Wait(cancelled_)) break;
            std::string fullPath = store_.GetFullPath(idx);
            if (fs::FileExists(fullPath)) {
                const std::int64_t size = fs::GetFileSize(fullPath);
                if (size > 0) throttle_.totalBytesDetected.fetch_add(size);
                store_.SetStatus(idx, ItemStatus::Pending);
                if (!queue.Push(FileJob{idx, std::move(fullPath)})) break;
            } else {
                store_.SetStatus(idx, ItemStatus::Missing);
                missingCount.fetch_add(1);
                report(completed.fetch_add(1) + 1);
            }
        }
        queue.Close();
    });

    // ---------------- Consumers
    auto consume = [&] {
        Worker w(opt.algo, bufferSize);
        FileJob job;
        while (queue.Pop(job)) {
            if (!pause_.Wait(cancelled_)) return;

            const auto start = std::chrono::steady_clock::now();
            Digest calc;
            const bool ok = HashOne(job.index, job.fullPath, w, opt.reportLargeFiles, calc);
            const std::int64_t ms = ElapsedMs(start);
            if (cancelled_.load()) return;

            // CRC-32 values are compared in both byte orders, so .sfv files from QuickSFV-style
            // tools (Standard) and from the C# SharpSFV (Reversed) both verify.
            const bool isCrc = opt.algo == HashType::Crc32;
            ItemStatus status;
            CrcMatch match = CrcMatch::None;
            if (!ok) {
                status = ItemStatus::Error;
                badCount.fetch_add(1);
            } else {
                switch (CompareDigest(calc, store_.Expected(job.index), isCrc)) {
                    case DigestMatch::Exact:
                        status = ItemStatus::OK;
                        if (isCrc) {
                            match = CrcMatch::Standard;
                            crcStandard.fetch_add(1);
                        }
                        break;
                    case DigestMatch::ByteReversed:
                        status = ItemStatus::OK;
                        match = CrcMatch::Reversed;
                        crcReversed.fetch_add(1);
                        break;
                    default:
                        status = ItemStatus::Bad;
                        break;
                }
                (status == ItemStatus::OK ? okCount : badCount).fetch_add(1);
            }
            store_.SetResult(job.index, ok ? &calc : nullptr, ms, status, match);
            report(completed.fetch_add(1) + 1);
        }
    };

    std::vector<Thread> consumers;
    consumers.reserve(static_cast<std::size_t>(workers));
    for (int i = 0; i < workers; ++i) consumers.emplace_back(consume);
    for (auto& t : consumers) t.join();
    queue.Abort();
    producer.join();

    {
        std::lock_guard lk(abortMutex_);
        abortQueue_ = nullptr;
    }

    RunResult r;
    r.completed = completed.load();
    r.ok = okCount.load();
    r.bad = badCount.load();
    r.missing = missingCount.load();
    r.cancelled = cancelled_.load();
    r.crcStandardMatches = crcStandard.load();
    r.crcReversedMatches = crcReversed.load();
    return r;
}

int Engine::CountFiles(const std::vector<std::string>& paths, const HashRunOptions& opt) const {
    int count = 0;
    auto allowed = [&](std::string_view full) {
        return !opt.applyFilters || IsFileAllowed(path::GetFileName(full), opt.includePattern, opt.excludePattern);
    };
    for (const std::string& p : paths) {
        if (cancelled_.load()) break;
        if (fs::FileExists(p)) {
            if (allowed(p)) ++count;
        } else if (fs::DirectoryExists(p)) {
            fs::EnumerateFiles(p, opt.recursive, [&](const std::string& f, std::int64_t) {
                if (allowed(f)) ++count;
                return !cancelled_.load();
            });
        }
    }
    return count;
}

std::vector<int> PopulateVerificationStore(FileStore& store, const ParsedChecksumFile& parsed,
                                           const std::string& baseFolder, bool sortByPath) {
    std::vector<int> order;
    order.reserve(parsed.entries.size());
    int loadIndex = 0;
    for (const ChecksumEntry& e : parsed.entries) {
        order.push_back(store.Add(path::GetFileName(e.fileName), e.fileName, baseFolder, loadIndex++,
                                  ItemStatus::Queued, &e.expectedHash));
    }
    if (sortByPath && !order.empty()) {
        // Indices are contiguous (single-threaded Add), so key[idx - first] is the sort key.
        const int first = order.front();
        std::vector<std::string> key(order.size());
        for (std::size_t i = 0; i < order.size(); ++i) key[i] = text::FoldCase(store.GetFullPath(order[i]));
        StableSortIndices(order, [&](int a, int b) {
            return key[static_cast<std::size_t>(a - first)] < key[static_cast<std::size_t>(b - first)];
        });
    }
    return order;
}

// ------------------------------------------------------------------ JobRunner

void JobRunner::Run(JobStore& jobs, Engine& engine, const AppSettings& settings, HashType algo, bool createMode,
                    const DriveResolver& drives, const Hooks& hooks) {
    auto changed = [&] {
        if (hooks.jobsChanged) hooks.jobsChanged();
    };
    auto log = [&](const std::string& s) {
        if (hooks.log) hooks.log(s);
    };

    while (auto job = jobs.NextQueued()) {
        // --- Scanning phase: exact count first so the percentage is accurate.
        job->status.store(JobStatus::Scanning);
        changed();
        log("Scanning Job: " + job->name);

        HashRunOptions opt;
        opt.algo = algo;
        opt.applyFilters = settings.showAdvancedBar;
        opt.includePattern = settings.includePattern;
        opt.excludePattern = settings.excludePattern;
        opt.recursive = (createMode || settings.showAdvancedBar) ? settings.scanRecursive : true;
        opt.updateFileList = false;
        opt.reportLargeFiles = false;

        const int exactTotal = engine.CountFiles(job->inputs, opt);
        if (engine.IsCancelled()) {
            job->status.store(JobStatus::Error);
            job->SetTimeStr("Cancelled");
            changed();
            log("Job Cancelled: " + job->name);
            break;
        }

        job->status.store(JobStatus::InProgress);
        changed();

        engine.Throttle().ResetMetrics();
        const auto start = std::chrono::steady_clock::now();

        std::string probe;
        if (!job->inputs.empty()) {
            const std::string& first = job->inputs[0];
            probe = fs::DirectoryExists(first) ? first : path::GetDirectoryName(first).value_or("");
        }
        const DriveDecision decision = drives.Resolve(settings.processingMode, probe.empty() ? job->rootPath : probe);
        opt.isHdd = decision.isHdd;
        log("Drive: " + DescribeDecision(decision));

        FileStore& store = engine.Store();
        store.Clear();

        std::atomic<std::int64_t> lastRepaint{TickMs()};
        const RunResult r = engine.HashFiles(job->inputs, job->rootPath, opt, [&](const Counters& c) {
            if (exactTotal > 0)
                job->progress.store(std::min(100.0, static_cast<double>(c.completed) / exactTotal * 100.0));
            const std::int64_t now = TickMs();
            std::int64_t last = lastRepaint.load();
            if ((now - last >= 250 || c.completed == exactTotal) && lastRepaint.compare_exchange_strong(last, now))
                changed();
            if (hooks.progress) {
                Counters g = c;
                g.total = exactTotal;
                hooks.progress(g);
            }
        });

        if (r.cancelled) {
            job->status.store(JobStatus::Error);
            job->SetTimeStr("Cancelled");
            store.Clear();
            changed();
            log("Job Cancelled: " + job->name);
            break;
        }

        // Save "<id>.<name><ext>" next to the job's root.
        const std::string fileName = std::to_string(job->id) + "." + path::SanitizeFileName(job->name) + ExtensionForAlgo(algo);
        const std::string fullSavePath = path::Combine(job->rootPath, fileName);
        const bool hasErrors = WriteJobFile(fullSavePath, store, settings, algo);

        const std::string timeStr = std::to_string(ElapsedMs(start)) + " ms";
        job->SetTimeStr(timeStr);
        const JobStatus finalStatus = hasErrors ? JobStatus::Error : JobStatus::Done;
        job->SetOutputPath(fullSavePath);
        job->status.store(finalStatus);
        job->progress.store(100.0);

        store.Clear();
        changed();
        log(std::string("Job Finished: ") + ToString(finalStatus) + " (" + timeStr + ")");
    }
}

} // namespace sfv
