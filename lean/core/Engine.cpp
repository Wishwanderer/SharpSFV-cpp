// SharpSFV (lean C++ rewrite) - the hashing engine (see Engine.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: parallel hashing for SSDs (producer/consumer threads, as the
// standard build does); thread-safe progress.
// Modified 2026-10-04: pause, observer (list batches, large files), CountFiles, JobRunner.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Engine.h"

#include "../base/FileSystem.h"
#include "../base/Path.h"
#include "../base/StrSet.h"
#include "../base/System.h"
#include "../base/Thread.h"
#include "../base/Text.h"
#include "Filter.h"
#include "Hash.h"
#include "../base/Sort.h"

namespace sfv {

namespace {

constexpr usize kQueueCapacity = 50000;             // Channel.CreateBounded(50000)
constexpr usize kHddBuffer = 8u * 1024 * 1024;      // HDD: one big sequential read at a time
constexpr usize kSsdBuffer = 512u * 1024;           // SSD: smaller reads, many files at once
constexpr usize kUiBatchSize = 2000;                // discovered items per list update
constexpr i64 kUiBatchMs = 250;                     // ... or this often

struct FileJob {
    int index = -1;
    Str fullPath;
};

inline int AtomicInc(int& v) { return __atomic_add_fetch(&v, 1, __ATOMIC_ACQ_REL); }
inline int AtomicGet(const int& v) { return __atomic_load_n(&v, __ATOMIC_ACQUIRE); }

// Relative path of a discovered file (ProcessFileEntry, with the standard build's fixes).
Str ComputeRelativePath(const Str& fullPath, StrView baseDir) {
    if (baseDir.Empty()) return fullPath;
    if (fullPath.Size() > baseDir.Size() && text::EqualsIgnoreCase(fullPath.Sub(0, baseDir.Size()), baseDir)) {
        const usize off = baseDir.Size();
        if (path::IsSep(fullPath[off])) return Str(fullPath.Sub(off + 1));
        if (path::IsSep(baseDir.Back())) return Str(fullPath.Sub(off));
    }
    if (Opt<Str> rel = path::GetRelativePath(baseDir, fullPath)) return *rel;
    return Str(path::GetFileName(fullPath));
}

Str TwoDigits(i64 n) {
    Str s;
    s.Append(static_cast<char>('0' + n / 10)).Append(static_cast<char>('0' + n % 10));
    return s;
}

} // namespace

// Bounded multi-producer/multi-consumer queue (Channel.CreateBounded with FullMode.Wait).
class JobQueue {
public:
    explicit JobQueue(usize capacity) : items_(new FileJob[capacity]), cap_(capacity) {}
    ~JobQueue() { delete[] items_; }
    JobQueue(const JobQueue&) = delete;
    JobQueue& operator=(const JobQueue&) = delete;

    bool Push(FileJob&& job) {
        LockGuard g(m_);
        while (count_ == cap_ && !aborted_) notFull_.Wait(m_);
        if (aborted_) return false;
        items_[(head_ + count_) % cap_] = Move(job);
        ++count_;
        notEmpty_.NotifyOne();
        return true;
    }
    bool Pop(FileJob& out) {
        LockGuard g(m_);
        while (count_ == 0 && !closed_ && !aborted_) notEmpty_.Wait(m_);
        if (aborted_ || count_ == 0) return false;
        out = Move(items_[head_]);
        head_ = (head_ + 1) % cap_;
        --count_;
        notFull_.NotifyOne();
        return true;
    }
    void Close() { // no more jobs: consumers finish what is queued, then stop
        LockGuard g(m_);
        closed_ = true;
        notEmpty_.NotifyAll();
    }
    void Abort() { // stop now: wakes everyone, drops what is queued
        LockGuard g(m_);
        aborted_ = true;
        notFull_.NotifyAll();
        notEmpty_.NotifyAll();
    }

private:
    Mutex m_;
    CondVar notFull_, notEmpty_;
    FileJob* items_;
    usize cap_, head_ = 0, count_ = 0;
    bool closed_ = false, aborted_ = false;
};

// ------------------------------------------------------------------ ProgressThrottle

void ProgressThrottle::ResetMetrics() {
    totalBytesDetected_ = 0;
    processedBytes_ = 0;
    lastSpeedBytes_ = 0;
    lastSpeedTick_ = TickMs();
    lastShown_ = -1;
    cachedSpeed_.Clear();
}

void ProgressThrottle::Update(int current, int total, int ok, int bad, int missing) {
    LockGuard g(lock_);
    const i64 now = TickMs();
    // 1. Throughput and ETA, every ~500 ms.
    if (showThroughput_ && now - lastSpeedTick_ > 500) {
        const i64 cur = ProcessedBytes();
        const i64 totalBytes = TotalBytesDetected();
        const i64 bytesDelta = cur - lastSpeedBytes_;
        const i64 timeDelta = now - lastSpeedTick_;
        if (timeDelta > 0) {
            const double bps = static_cast<double>(bytesDelta) / static_cast<double>(timeDelta) * 1000.0;
            const double mbps = bps / (1024.0 * 1024.0);
            Str eta = "--:--";
            if (bps > 0 && totalBytes > cur) {
                const double secs = static_cast<double>(totalBytes - cur) / bps;
                if (secs < 86400) { // TimeSpan.ToString(@"hh\:mm\:ss")
                    const i64 s = static_cast<i64>(secs * 1000.0 + 0.5) / 1000;
                    eta = TwoDigits((s / 3600) % 24) + ":" + TwoDigits((s / 60) % 60) + ":" + TwoDigits(s % 60);
                }
            }
            cachedSpeed_ = Str("Speed: ") + text::FormatF1(mbps) + " MB/s | ETA: " + eta;
        }
        lastSpeedTick_ = now;
        lastSpeedBytes_ = cur;
    }
    // 2. A worker that finished earlier but got here later: its frame is already out of date.
    if (current < lastShown_) return;
    // 3. Time-based throttle (the final update always goes through).
    if (current < total && now - lastUiTick_ < 100) return;
    lastUiTick_ = now;
    lastShown_ = current;
    if (!presenter_) return;
    ProgressInfo info;
    info.current = current, info.total = total, info.ok = ok, info.bad = bad, info.missing = missing;
    if (showThroughput_) info.speed = cachedSpeed_;
    presenter_(ctx_, info);
}

Str ProgressThrottle::SpeedString() {
    LockGuard g(lock_);
    return cachedSpeed_;
}

// ------------------------------------------------------------------ PauseGate

void PauseGate::Pause() {
    LockGuard g(m_);
    __atomic_store_n(&paused_, true, __ATOMIC_RELEASE);
}

void PauseGate::Resume() {
    LockGuard g(m_);
    __atomic_store_n(&paused_, false, __ATOMIC_RELEASE);
    cv_.NotifyAll();
}

bool PauseGate::Wait(const Engine& engine) {
    if (engine.IsCancelled()) return false;
    if (!IsPaused()) return true;
    LockGuard g(m_);
    while (IsPaused() && !engine.IsCancelled()) cv_.Wait(m_);
    return !engine.IsCancelled();
}

void PauseGate::WakeAll() {
    LockGuard g(m_);
    cv_.NotifyAll();
}

// ------------------------------------------------------------------ Engine

// What each consumer thread owns: its hasher and its read buffer.
struct Engine::Worker {
    Hasher hasher;
    bool ready;
    u8* buffer;
    usize capacity;
    Worker(HashType algo, usize size)
        : ready(hasher.Init(algo)), buffer(static_cast<u8*>(MemAlloc(size))), capacity(size) {}
    ~Worker() { MemFree(buffer); }
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
};

void Engine::Cancel() {
    __atomic_store_n(&cancelled_, true, __ATOMIC_RELEASE);
    pause_.WakeAll();
    LockGuard g(queueLock_);
    if (activeQueue_) activeQueue_->Abort();
}

void Engine::SetActiveQueue(JobQueue* q) {
    LockGuard g(queueLock_);
    activeQueue_ = q;
}

int Engine::WorkerCount(bool isHdd) const {
    if (workerOverride_ > 0) return workerOverride_;
    return isHdd ? 1 : ProcessorCount();
}

bool Engine::HashOne(int index, const Str& fullPath, Worker& w, bool reportLarge, Digest& out) {
    if (!w.ready) return false; // crypto provider unavailable -> ERROR (as the C# null algorithm did)
    fs::FileReader reader;
    if (!reader.Open(fullPath)) return false;
    const i64 len = reader.Size();
    const bool large = len > kLargeFileThreshold;
    const bool announce = large && reportLarge && observer_;
    if (announce) observer_->OnLargeFileBegin(index, path::GetFileName(fullPath));

    // ProgressStream throttling: report at 0.1% steps, at most every 100 ms.
    i64 step = len / 1000;
    if (step == 0) step = 64 * 1024;
    i64 readTotal = 0, sinceReport = 0, lastTick = TickMs();

    w.hasher.Reset();
    bool ok = true;
    for (;;) {
        const i64 n = reader.Read(w.buffer, w.capacity);
        if (n < 0) {
            ok = false;
            break;
        }
        if (n == 0) break;
        w.hasher.Update(w.buffer, static_cast<usize>(n));
        throttle_.AddProcessed(n);
        if (announce) {
            readTotal += n;
            sinceReport += n;
            if (sinceReport >= step) {
                const i64 now = TickMs();
                if (now - lastTick >= 100) {
                    sinceReport = 0;
                    lastTick = now;
                    observer_->OnLargeFileProgress(index, static_cast<double>(readTotal) / static_cast<double>(len) * 100.0);
                }
            }
        }
        if (IsCancelled()) { // big files can take minutes; stop promptly
            ok = false;
            break;
        }
    }
    if (announce) observer_->OnLargeFileEnd(index);
    return ok && w.hasher.Final(out);
}

namespace {

// Starts `count` consumer threads running `consume`. Returns how many started; with none
// (thread creation failed), the producer hashes each file itself, one at a time.
template <class F> int StartWorkers(Thread* threads, int count, F& consume) {
    int started = 0;
    for (int i = 0; i < count; ++i)
        if (threads[i].Start(consume)) ++started;
    return started;
}

} // namespace

RunResult Engine::HashFiles(const Vec<Str>& paths, StrView baseDirectory, const HashRunOptions& opt,
                            ProgressFn progress) {
    JobQueue queue(kQueueCapacity);
    SetActiveQueue(&queue);
    int completed = 0, okCount = 0, errorCount = 0, queued = 0; // shared: __atomic only
    const int workers = WorkerCount(opt.isHdd);
    const usize bufferSize = opt.isHdd ? kHddBuffer : kSsdBuffer;

    // ---------------- Consumers: hash one file. False when cancelled.
    auto hashJob = [&](Worker& w, const FileJob& job) -> bool {
        if (!pause_.Wait(*this)) return false;
        const i64 start = TickMs();
        Digest digest;
        const bool ok = HashOne(job.index, job.fullPath, w, opt.reportLargeFiles, digest);
        const i64 ms = TickMs() - start;
        if (IsCancelled()) return false; // leave the interrupted file as Queued
        if (ok) {
            AtomicInc(okCount);
            store_.SetResult(job.index, &digest, ms, ItemStatus::OK);
        } else {
            AtomicInc(errorCount);
            store_.SetResult(job.index, nullptr, ms, ItemStatus::Error);
        }
        Counters c;
        c.completed = AtomicInc(completed);
        c.total = AtomicGet(queued), c.ok = AtomicGet(okCount), c.bad = AtomicGet(errorCount);
        progress(c);
        return true;
    };
    auto consume = [&] {
        Worker w(opt.algo, bufferSize);
        FileJob job;
        while (queue.Pop(job))
            if (!hashJob(w, job)) {
                queue.Abort(); // releases the producer and the other workers
                return;
            }
    };
    Thread* threads = new Thread[workers];
    const int started = StartWorkers(threads, workers, consume);
    Worker* inlineWorker = started == 0 ? new Worker(opt.algo, bufferSize) : nullptr;

    // ---------------- Producer (this thread): list, filter, add to the store, queue.
    int originalIndex = 0;
    bool stop = false;
    StrSet processedPaths; // case-folded keys (OrdinalIgnoreCase)
    const Str skipKey = opt.skipPath.Empty() ? Str() : text::FoldCase(path::GetFullPath(opt.skipPath));
    const Str skipName(path::GetFileName(opt.skipPath));
    const bool batches = opt.updateFileList && observer_;
    Vec<int> uiBatch;
    i64 lastBatch = TickMs();
    auto flush = [&] {
        if (!batches || uiBatch.Empty()) return;
        observer_->OnItemsDiscovered(uiBatch); // takes the indices
        uiBatch.Clear();
        lastBatch = TickMs();
    };

    // ProcessFileEntry. Returns false to stop producing.
    auto processEntry = [&](const Str& fullPath) -> bool {
        const int origIndex = originalIndex++;
        if (!processedPaths.Insert(text::FoldCase(fullPath))) return true; // duplicate
        if (!skipKey.Empty() && text::EqualsIgnoreCase(path::GetFileName(fullPath), skipName) &&
            text::FoldCase(path::GetFullPath(fullPath)) == skipKey)
            return true;
        if (!pause_.Wait(*this)) return false;
        const StrView fileName = path::GetFileName(fullPath);
        if (opt.applyFilters && !IsFileAllowed(fileName, opt.includePattern, opt.excludePattern)) return true;

        FileJob job;
        job.index = store_.Add(fileName, ComputeRelativePath(fullPath, baseDirectory), baseDirectory, origIndex,
                               ItemStatus::Queued);
        if (job.index < 0) return false; // store full: stop adding, let the workers drain
        AtomicInc(queued);
        if (batches) uiBatch.Push(job.index);
        job.fullPath = fullPath;
        return inlineWorker ? hashJob(*inlineWorker, job) : queue.Push(Move(job));
    };

    for (const Str& p : paths) {
        if (stop || !pause_.Wait(*this)) break;
        if (fs::FileExists(p)) {
            const i64 size = fs::GetFileSize(p);
            if (size > 0) throttle_.AddDetected(size);
            if (!processEntry(p)) stop = true;
        } else if (fs::DirectoryExists(p)) {
            fs::EnumerateFiles(p, opt.recursive, [&](const Str& file, i64 size) {
                if (!pause_.Wait(*this) || !(throttle_.AddDetected(size), processEntry(file))) {
                    stop = true;
                    return false;
                }
                if (batches && (uiBatch.Size() >= kUiBatchSize || TickMs() - lastBatch > kUiBatchMs)) flush();
                return true;
            });
        }
    }
    flush();
    queue.Close();
    for (int i = 0; i < workers; ++i) threads[i].Join();
    delete[] threads;
    delete inlineWorker;
    SetActiveQueue(nullptr);

    RunResult r;
    r.completed = completed, r.ok = okCount, r.bad = errorCount;
    r.cancelled = IsCancelled();
    return r;
}

RunResult Engine::Verify(const Vec<int>& order, const HashRunOptions& opt, ProgressFn progress) {
    JobQueue queue(kQueueCapacity);
    SetActiveQueue(&queue);
    int completed = 0, okCount = 0, badCount = 0, missingCount = 0, crcStandard = 0, crcReversed = 0;
    const int total = static_cast<int>(order.Size());
    const int workers = WorkerCount(opt.isHdd);
    const usize bufferSize = opt.isHdd ? kHddBuffer : kSsdBuffer;
    const bool isCrc = opt.algo == HashType::Crc32;

    auto report = [&](int cur) {
        Counters c;
        c.completed = cur, c.total = total;
        c.ok = AtomicGet(okCount), c.bad = AtomicGet(badCount), c.missing = AtomicGet(missingCount);
        progress(c);
    };

    // ---------------- Consumers: hash and compare one file. False when cancelled.
    auto verifyJob = [&](Worker& w, const FileJob& job) -> bool {
        if (!pause_.Wait(*this)) return false;
        const i64 start = TickMs();
        Digest calc;
        const bool ok = HashOne(job.index, job.fullPath, w, opt.reportLargeFiles, calc);
        const i64 ms = TickMs() - start;
        if (IsCancelled()) return false;

        // CRC-32 values are compared in both byte orders (QuickSFV-style and C# SharpSFV-style).
        ItemStatus status;
        CrcMatch match = CrcMatch::None;
        if (!ok) {
            status = ItemStatus::Error;
            AtomicInc(badCount);
        } else {
            switch (CompareDigest(calc, store_.Expected(job.index), isCrc)) {
                case DigestMatch::Exact:
                    status = ItemStatus::OK;
                    if (isCrc) {
                        match = CrcMatch::Standard;
                        AtomicInc(crcStandard);
                    }
                    break;
                case DigestMatch::ByteReversed:
                    status = ItemStatus::OK;
                    match = CrcMatch::Reversed;
                    AtomicInc(crcReversed);
                    break;
                default:
                    status = ItemStatus::Bad;
                    break;
            }
            AtomicInc(status == ItemStatus::OK ? okCount : badCount);
        }
        store_.SetResult(job.index, ok ? &calc : nullptr, ms, status, match);
        report(AtomicInc(completed));
        return true;
    };
    auto consume = [&] {
        Worker w(opt.algo, bufferSize);
        FileJob job;
        while (queue.Pop(job))
            if (!verifyJob(w, job)) {
                queue.Abort();
                return;
            }
    };
    Thread* threads = new Thread[workers];
    const int started = StartWorkers(threads, workers, consume);
    Worker* inlineWorker = started == 0 ? new Worker(opt.algo, bufferSize) : nullptr;

    // ---------------- Producer (this thread): existence check first, then queue for hashing.
    for (const int idx : order) {
        if (!pause_.Wait(*this)) break;
        FileJob job;
        job.index = idx;
        job.fullPath = store_.GetFullPath(idx);
        if (!fs::FileExists(job.fullPath)) {
            store_.SetStatus(idx, ItemStatus::Missing);
            AtomicInc(missingCount);
            report(AtomicInc(completed));
            continue;
        }
        const i64 size = fs::GetFileSize(job.fullPath);
        if (size > 0) throttle_.AddDetected(size);
        store_.SetStatus(idx, ItemStatus::Pending);
        if (!(inlineWorker ? verifyJob(*inlineWorker, job) : queue.Push(Move(job)))) break;
    }
    queue.Close();
    for (int i = 0; i < workers; ++i) threads[i].Join();
    delete[] threads;
    delete inlineWorker;
    SetActiveQueue(nullptr);

    RunResult r;
    r.completed = completed, r.ok = okCount, r.bad = badCount, r.missing = missingCount;
    r.crcStandardMatches = crcStandard, r.crcReversedMatches = crcReversed;
    r.cancelled = IsCancelled();
    return r;
}

Vec<int> PopulateVerificationStore(FileStore& store, const ParsedChecksumFile& parsed, StrView baseFolder,
                                   bool sortByPath) {
    Vec<int> order;
    order.Reserve(parsed.entries.Size());
    int loadIndex = 0;
    for (const ChecksumEntry& e : parsed.entries) {
        const int idx = store.Add(path::GetFileName(e.fileName), e.fileName, baseFolder, loadIndex++, ItemStatus::Queued,
                                  &e.expectedHash);
        if (idx < 0) break;
        order.Push(idx);
    }
    if (sortByPath && !order.Empty()) {
        // Indices are contiguous (single-threaded Add), so key[idx - first] is the sort key.
        const int first = order[0];
        Vec<Str> key;
        key.Resize(order.Size());
        for (usize i = 0; i < order.Size(); ++i) key[i] = text::FoldCase(store.GetFullPath(order[i]));
        StableSortIndices(order, [&](int a, int b) {
            return key[static_cast<usize>(a - first)] < key[static_cast<usize>(b - first)];
        });
    }
    return order;
}

} // namespace sfv
