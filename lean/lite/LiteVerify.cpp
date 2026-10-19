// SharpSFV Lite - the verification run (see LiteVerify.h and the spec).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-06: added (SharpSFV Lite).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
//
// Threads: the calling thread reads the list, scans sizes, then runs the "tick" loop (every
// 50 ms: Pause/Stop, parent; every 200 ms: status file, log flush, console progress). Up to 12
// workers take files in list order through one atomic counter, each publishing the file it is
// on and how far it has got. Counters are atomics; log lines go out under one lock, in the
// order files finish.
#include "LiteVerify.h"

#include "../base/FileSystem.h"
#include "../base/Path.h"
#include "../base/System.h"
#include "../base/Text.h"
#include "../base/Thread.h"
#include "../core/ChecksumFile.h"
#include "../core/Drive.h"
#include "../core/Hash.h"
#include "LitePlatform.h"

namespace sfv::lite {

namespace {

constexpr usize kHddBuffer = 8u * 1024 * 1024; // as the full engine: one big sequential read
constexpr usize kSsdBuffer = 512u * 1024;      // many files at once, smaller reads
constexpr i64 kTickMs = 50;                    // Pause/Stop/parent polling
constexpr i64 kStatusMs = 200;                 // status file, log flush
constexpr i64 kConsoleMs = 1000;               // console progress line
constexpr i64 kEtaWindowMs = 5000;             // speed averaged over the last 5 s
constexpr i64 kEtaWarmupMs = 3000;             // no estimate in the first 3 s of hashing
constexpr int kEtaSamples = 64;                // ring of (active ms, bytes) samples

inline i64 Load(const i64& v) { return __atomic_load_n(&v, __ATOMIC_RELAXED); }
inline int Load(const int& v) { return __atomic_load_n(&v, __ATOMIC_RELAXED); }
inline bool Load(const bool& v) { return __atomic_load_n(&v, __ATOMIC_ACQUIRE); }
inline void Store(i64& v, i64 x) { __atomic_store_n(&v, x, __ATOMIC_RELAXED); }
inline void Store(int& v, int x) { __atomic_store_n(&v, x, __ATOMIC_RELAXED); }
inline void Store(bool& v, bool x) { __atomic_store_n(&v, x, __ATOMIC_RELEASE); }
inline int Inc(int& v) { return __atomic_add_fetch(&v, 1, __ATOMIC_ACQ_REL); }
inline void Add(i64& v, i64 x) { __atomic_fetch_add(&v, x, __ATOMIC_RELAXED); }

const char* AlgoName(HashType t) {
    switch (t) {
        case HashType::Crc32: return "CRC-32";
        case HashType::MD5: return "MD5";
        default: return "xxHash3";
    }
}

bool ParseAlgo(StrView v, HashType& out) {
    if (text::EqualsIgnoreCase(v, "crc32") || text::EqualsIgnoreCase(v, "crc") || text::EqualsIgnoreCase(v, "sfv"))
        out = HashType::Crc32;
    else if (text::EqualsIgnoreCase(v, "md5"))
        out = HashType::MD5;
    else if (text::EqualsIgnoreCase(v, "xxh3") || text::EqualsIgnoreCase(v, "xxhash3") || text::EqualsIgnoreCase(v, "xxhash"))
        out = HashType::XXHASH3;
    else
        return false;
    return true;
}

// The algorithm named by the list's extension; false for other extensions.
bool AlgoFromExtension(StrView list, HashType& out) {
    if (text::EndsWithIgnoreCase(list, ".sfv")) out = HashType::Crc32;
    else if (text::EndsWithIgnoreCase(list, ".md5")) out = HashType::MD5;
    else if (text::EndsWithIgnoreCase(list, ".xxh3")) out = HashType::XXHASH3;
    else return false;
    return true;
}

Str Permille(i64 done, i64 total) {
    if (total <= 0) return Str("0");
    i64 p = done * 1000 / total;
    if (p > 1000) p = 1000;
    if (p < 0) p = 0;
    return ToStr(p);
}

bool RealDetectHdd(StrView folder) {
    const drive::VolumeInfo v = drive::IdentifyVolume(folder);
    return drive::Detect(v).isHdd; // detection failed: HDD, the safe choice (as the full CLI)
}

struct Entry {
    Str name;          // path as written in the list
    Str full;          // base + name
    Vec<u8> expected;
    i64 size = 0;
};

struct Slot { // one per worker row; read by the tick loop while the worker writes it
    int entry = -1; // index into entries, -1 = idle
    i64 done = 0;   // bytes of that file hashed so far
};

enum class Verdict { Ok, Bad, Error, Aborted };

class Runner {
public:
    Runner(const Options& opt, Control& control, ParentWatch& parent, const Environment& env)
        : opt_(opt), control_(control), parent_(parent), env_(env), startTick_(TickMs()) {}
    int Run();
    void ReportFailure(StrView message) { (void)Fail(message); }

    struct WorkerArg {
        Runner* runner;
        int slot;
    };
    static void WorkerEntry(void* p) {
        WorkerArg* a = static_cast<WorkerArg*>(p);
        a->runner->Work(a->slot);
    }

private:
    // ---- set-up and finish
    int Fail(StrView message);
    bool LoadList(Str& error);
    void Scan();
    void StartWorkers();
    void Finish(int exitCode, bool stoppedByParent);

    // ---- workers
    void Work(int slot);
    Verdict VerifyOne(int slot, const Entry& e, Hasher& h, u8* buf, usize cap, Digest& calc, Str& reason);
    bool WaitWhilePaused(); // false once stopped
    void Record(const Entry& e, Verdict v, const Digest& calc, StrView reason);

    // ---- the tick loop
    void Tick(bool force);
    void PollControl();
    i64 ActiveMs() const; // elapsed, pauses not counted
    i64 RemainingSeconds(); // -1 = "-"
    void WriteStatus(StrView state, int exitCode, StrView error = {});
    void ConsoleProgress(bool final);
    void LogLine(StrView line, bool alsoConsole);

    const Options& opt_;
    Control& control_;
    ParentWatch& parent_;
    const Environment& env_;

    HashType algo_ = HashType::Crc32;
    Str base_;
    Vec<Entry> entries_;
    Vec<int> present_; // indices of entries that exist, in list order
    i64 bytesTotal_ = 0;
    bool isHdd_ = true;
    int rows_ = 1;    // worker rows in the status file
    int started_ = 0; // worker threads running
    Thread threads_[kMaxThreads];
    WorkerArg args_[kMaxThreads];
    Slot slots_[kMaxThreads];

    // shared counters (atomics)
    int next_ = 0;
    int ok_ = 0, bad_ = 0, missing_ = 0, error_ = 0, finished_ = 0, workersDone_ = 0;
    int parked_ = 0; // workers waiting at the pause gate (PAUSED is shown only once all are)
    i64 bytesDone_ = 0;
    bool paused_ = false, stopped_ = false;
    Mutex gateLock_;
    CondVar gate_;

    // state of the tick loop (calling thread only)
    const char* state_ = "SCANNING";
    bool hashing_ = false;
    i64 startTick_ = 0, pausedTotal_ = 0, pauseStart_ = 0, hashStartActive_ = 0;
    i64 lastStatus_ = -1000000, lastConsole_ = -1000000;
    i64 seq_ = 0;
    bool stopByParent_ = false;
    i64 sampleT_[kEtaSamples] = {}, sampleB_[kEtaSamples] = {};
    int sampleCount_ = 0, sampleHead_ = 0;
    fs::TextWriter status_;
    Str statusTmp_;

    // log and console (shared, under logLock_)
    Mutex logLock_;
    fs::TextWriter log_;
    bool logOpen_ = false, logError_ = false;
    usize consoleWidth_ = 0; // length of the progress line on screen (console mode)
};

// ------------------------------------------------------------------ set-up

int Runner::Fail(StrView message) {
    if (!opt_.statusPath.Empty()) WriteStatus("FAILED", kExitFailed, message);
    else console::WriteError(Str("SharpSFV-Lite: ") + message);
    if (logOpen_) {
        log_.WriteLine(Str("Failed: ") + message);
        log_.Close();
        logOpen_ = false;
    }
    return kExitFailed;
}

bool Runner::LoadList(Str& error) {
    if (!AlgoFromExtension(opt_.list, algo_)) {
        if (!opt_.hasAlgo) {
            error = "The list's extension does not name an algorithm (.sfv, .md5, .xxh3): add -algo crc32|md5|xxh3";
            return false;
        }
        algo_ = opt_.algo;
    }
    if (!fs::FileExists(opt_.list)) {
        error = "Checksum list not found: " + opt_.list;
        return false;
    }
    ParsedChecksumFile parsed;
    Str err;
    if (!ParseChecksumFile(opt_.list, algo_, parsed, &err)) {
        error = "Cannot read the checksum list: " + err;
        return false;
    }
    if (parsed.entries.Empty()) {
        error = "The checksum list has no entries: " + opt_.list;
        return false;
    }
    base_ = opt_.base.Empty() ? path::GetDirectoryName(opt_.list).ValueOr(Str()) : opt_.base;
    entries_.Reserve(parsed.entries.Size());
    for (ChecksumEntry& ce : parsed.entries) {
        Entry e;
        e.full = path::Combine(base_, ce.fileName); // as the full CLI's store builds it
        e.name = Move(ce.fileName);
        e.expected = Move(ce.expectedHash);
        entries_.Push(Move(e));
    }
    return true;
}

void Runner::Scan() {
    present_.Reserve(entries_.Size());
    for (usize i = 0; i < entries_.Size(); ++i) {
        Tick(false);
        while (Load(paused_) && !Load(stopped_)) { // pause holds the scan too
            plat::SleepMs(static_cast<u32>(kTickMs));
            Tick(false);
        }
        if (Load(stopped_)) return;
        Entry& e = entries_[i];
        if (!fs::FileExists(e.full)) { // directories count as missing, as in the full CLI
            Inc(missing_);
            Inc(finished_);
            LogLine("MISSING  " + e.name, true);
            continue;
        }
        const i64 size = fs::GetFileSize(e.full);
        e.size = size > 0 ? size : 0;
        bytesTotal_ += e.size;
        present_.Push(static_cast<int>(i));
    }
}

void Runner::StartWorkers() {
    int want = rows_;
    if (static_cast<usize>(want) > present_.Size()) want = static_cast<int>(present_.Size());
    for (int i = 0; i < want; ++i) {
        args_[i].runner = this;
        args_[i].slot = i;
        if (threads_[started_].Start(&Runner::WorkerEntry, &args_[i])) ++started_;
    }
    if (started_ == 0 && !present_.Empty()) { // no threads at all: hash on this thread
        Work(0); // no ticks meanwhile (Pause/Stop ignored): a last resort only
    }
}

// ------------------------------------------------------------------ workers

bool Runner::WaitWhilePaused() {
    if (Load(stopped_)) return false;
    if (!Load(paused_)) return true;
    LockGuard g(gateLock_);
    Inc(parked_);
    while (Load(paused_) && !Load(stopped_)) gate_.Wait(gateLock_);
    __atomic_sub_fetch(&parked_, 1, __ATOMIC_ACQ_REL);
    return !Load(stopped_);
}

void Runner::Work(int slot) {
    const usize cap = isHdd_ ? kHddBuffer : kSsdBuffer;
    u8* buf = static_cast<u8*>(MemAlloc(cap));
    Hasher h;
    const bool ready = h.Init(algo_);
    const int count = static_cast<int>(present_.Size());
    for (;;) {
        if (!WaitWhilePaused()) break;
        const int k = Inc(next_) - 1;
        if (k >= count) break;
        const int idx = present_[static_cast<usize>(k)];
        const Entry& e = entries_[static_cast<usize>(idx)];
        Store(slots_[slot].done, 0);
        Store(slots_[slot].entry, idx);
        Digest calc;
        Str reason;
        Verdict v;
        if (!ready) {
            v = Verdict::Error;
            reason = Str("cannot hash: the ") + AlgoName(algo_) + " provider is unavailable";
        } else {
            v = VerifyOne(slot, e, h, buf, cap, calc, reason);
        }
        if (v == Verdict::Aborted) break; // stopped mid-file: not counted
        // CRC-32 matches in either byte order (QuickSFV and C# SharpSFV style), as in the full CLI.
        if (v == Verdict::Ok && CompareDigest(calc, e.expected, algo_ == HashType::Crc32) == DigestMatch::Mismatch)
            v = Verdict::Bad;
        Record(e, v, calc, reason);
        Store(slots_[slot].entry, -1);
    }
    Store(slots_[slot].entry, -1);
    MemFree(buf);
    Inc(workersDone_);
}

Verdict Runner::VerifyOne(int slot, const Entry& e, Hasher& h, u8* buf, usize cap, Digest& calc, Str& reason) {
    fs::FileReader reader;
    if (!reader.Open(e.full)) {
        reason = "cannot open: " + plat::LastErrorText();
        return Verdict::Error;
    }
    h.Reset();
    for (;;) {
        if (!WaitWhilePaused()) return Verdict::Aborted; // pause between blocks; stop abandons the file
        const i64 n = reader.Read(buf, cap);
        if (n < 0) {
            reason = "cannot read: " + plat::LastErrorText();
            return Verdict::Error;
        }
        if (n == 0) break;
        h.Update(buf, static_cast<usize>(n));
        Add(slots_[slot].done, n);
        Add(bytesDone_, n);
    }
    if (!h.Final(calc)) {
        reason = "cannot hash: the hash could not be completed";
        return Verdict::Error;
    }
    return Verdict::Ok;
}

void Runner::Record(const Entry& e, Verdict v, const Digest& calc, StrView reason) {
    Str line;
    switch (v) {
        case Verdict::Ok:
            Inc(ok_);
            line = "OK       " + e.name;
            break;
        case Verdict::Bad: {
            Inc(bad_);
            // Expected as written in the list; actual as hex digits (CRC-32: standard, QuickSFV
            // byte order, whatever order the list uses, so the line never depends on timing).
            line = "BAD      " + e.name + "   expected " + ToHex(e.expected) + "   actual " + ToHex(calc);
            break;
        }
        default:
            Inc(error_);
            line = "ERROR    " + e.name + "   " + reason;
            break;
    }
    Inc(finished_);
    LogLine(line, v != Verdict::Ok);
}

// ------------------------------------------------------------------ the tick loop

void Runner::PollControl() {
    bool pause = false, stop = false;
    control_.Poll(pause, stop);
    if (opt_.statusPath.Empty() && Interrupted()) stop = true; // Ctrl+C in console mode
    if (!stop && parent_.ParentGone()) {
        stop = true;
        stopByParent_ = true;
    }
    const i64 now = TickMs();
    if (stop && !Load(stopped_)) {
        LockGuard g(gateLock_);
        Store(stopped_, true);
        gate_.NotifyAll();
    }
    if (pause != Load(paused_) && !Load(stopped_)) {
        LockGuard g(gateLock_);
        if (pause) {
            pauseStart_ = now;
        } else {
            pausedTotal_ += now - pauseStart_;
            sampleCount_ = 0; // the speed before the pause says little about the speed after it
        }
        Store(paused_, pause);
        gate_.NotifyAll();
    }
}

i64 Runner::ActiveMs() const {
    const i64 now = TickMs();
    i64 ms = now - startTick_ - pausedTotal_;
    if (Load(paused_)) ms -= now - pauseStart_;
    return ms < 0 ? 0 : ms;
}

i64 Runner::RemainingSeconds() {
    if (!hashing_ || Load(paused_)) return -1;
    const i64 t = ActiveMs(), b = Load(bytesDone_);
    // Record a sample, then measure over the window.
    sampleT_[sampleHead_] = t, sampleB_[sampleHead_] = b;
    sampleHead_ = (sampleHead_ + 1) % kEtaSamples;
    if (sampleCount_ < kEtaSamples) ++sampleCount_;
    if (t - hashStartActive_ < kEtaWarmupMs) return -1;
    int oldest = (sampleHead_ - sampleCount_ + kEtaSamples) % kEtaSamples;
    for (int i = 0; i < sampleCount_; ++i) { // the oldest sample still inside the window
        const int k = (sampleHead_ - sampleCount_ + i + kEtaSamples) % kEtaSamples;
        if (t - sampleT_[k] <= kEtaWindowMs) {
            oldest = k;
            break;
        }
    }
    const i64 dt = t - sampleT_[oldest], db = b - sampleB_[oldest];
    if (dt <= 0 || db <= 0) return -1;
    const i64 left = bytesTotal_ - b;
    if (left <= 0) return 0;
    const double secs = static_cast<double>(left) * static_cast<double>(dt) / static_cast<double>(db) / 1000.0;
    return static_cast<i64>(secs + 0.5);
}

void Runner::WriteStatus(StrView state, int exitCode, StrView error) {
    if (opt_.statusPath.Empty()) return;
    const i64 remaining = RemainingSeconds();
    const char* nl = "\r\n";
    Str s;
    s.Reserve(1024);
    s.Append("\xEF\xBB\xBF").Append("SharpSFV-Lite status 1").Append(nl);
    s.Append("state ").Append(state).Append(nl);
    s.Append("seq ").AppendInt(++seq_).Append(nl);
    const i64 done = Load(bytesDone_);
    Str overall;
    if (exitCode >= 0 && exitCode != kExitStopped && exitCode != kExitFailed) overall = "1000"; // finished
    else if (bytesTotal_ > 0) overall = Permille(done, bytesTotal_);
    else overall = Permille(Load(finished_), static_cast<i64>(entries_.Size()));
    s.Append("progress ").Append(overall).Append(' ').AppendInt(Load(finished_)).Append(' ')
        .AppendInt(static_cast<i64>(entries_.Size())).Append(nl);
    s.Append("counts ").AppendInt(Load(ok_)).Append(' ').AppendInt(Load(bad_)).Append(' ').AppendInt(Load(missing_))
        .Append(' ').AppendInt(Load(error_)).Append(nl);
    s.Append("bytes ").AppendInt(done).Append(' ').AppendInt(bytesTotal_).Append(nl);
    s.Append("time ").AppendInt(ActiveMs() / 1000).Append(' ');
    if (remaining < 0) s.Append('-');
    else s.AppendInt(remaining);
    s.Append(nl);
    for (int i = 0; i < rows_; ++i) {
        s.Append("worker ").AppendInt(i + 1).Append(' ');
        const int idx = Load(slots_[i].entry);
        if (idx < 0) {
            s.Append('-');
        } else {
            const Entry& e = entries_[static_cast<usize>(idx)];
            s.Append(e.size > 0 ? Permille(Load(slots_[i].done), e.size) : Str("0")).Append(' ').Append(e.name);
        }
        s.Append(nl);
    }
    if (logError_) s.Append("logerror 1").Append(nl);
    if (!error.Empty()) s.Append("error ").Append(error).Append(nl);
    s.Append("exit ");
    if (exitCode < 0) s.Append('-');
    else s.AppendInt(exitCode);
    s.Append(nl);

    if (statusTmp_.Empty()) statusTmp_ = opt_.statusPath + ".tmp";
    if (!status_.Open(statusTmp_)) return;
    status_.Write(s);
    if (!status_.Close()) return;
    plat::MoveReplace(statusTmp_, opt_.statusPath); // a reader holding it open: next write
}

void Runner::ConsoleProgress(bool final) {
    if (!opt_.statusPath.Empty()) return;
    Str line = "  ";
    const i64 done = Load(bytesDone_);
    const i64 pm = final ? 1000 : (bytesTotal_ > 0 ? done * 1000 / bytesTotal_ : 0);
    line.AppendInt(Min<i64>(pm, 1000) / 10).Append('.').AppendInt(Min<i64>(pm, 1000) % 10).Append("%   OK ");
    line.AppendInt(Load(ok_)).Append("  BAD ").AppendInt(Load(bad_)).Append("  MISSING ").AppendInt(Load(missing_));
    line.Append("  ERROR ").AppendInt(Load(error_)).Append("   elapsed ").Append(FormatHms(ActiveMs() / 1000));
    if (Load(paused_)) {
        line.Append("   paused");
    } else if (!final) {
        const i64 rem = RemainingSeconds();
        line.Append("   left ").Append(rem < 0 ? Str("--:--:--") : FormatHms(rem));
    }
    LockGuard g(logLock_);
    Str out = "\r" + line;
    for (usize i = line.Size(); i < consoleWidth_; ++i) out.Append(' ');
    consoleWidth_ = line.Size();
    if (final) {
        out.Append(fs::kNewLine);
        consoleWidth_ = 0;
    }
    console::Write(out);
}

void Runner::LogLine(StrView line, bool alsoConsole) {
    LockGuard g(logLock_);
    if (logOpen_) log_.WriteLine(line);
    if (alsoConsole && opt_.statusPath.Empty()) {
        Str out = "\r" + line;
        for (usize i = line.Size(); i < consoleWidth_; ++i) out.Append(' ');
        out.Append(fs::kNewLine);
        consoleWidth_ = 0;
        console::Write(out);
    }
}

void Runner::Tick(bool force) {
    PollControl();
    const i64 now = TickMs();
    if (force || now - lastStatus_ >= kStatusMs) {
        lastStatus_ = now;
        // PAUSED only once every running worker has stopped at the gate (each finishes the
        // block it is reading first), so a PAUSED status never shows bytes still moving.
        const bool allParked = Load(parked_) >= started_ - Load(workersDone_);
        state_ = Load(paused_) && allParked ? "PAUSED" : (hashing_ ? "RUNNING" : "SCANNING");
        WriteStatus(state_, -1);
        LockGuard g(logLock_);
        if (logOpen_ && !log_.Flush() && !logError_) logError_ = true; // verification carries on
    }
    if (now - lastConsole_ >= kConsoleMs) {
        lastConsole_ = now;
        if (hashing_) ConsoleProgress(false);
    }
}

// ------------------------------------------------------------------ the run

void Runner::Finish(int exitCode, bool stoppedByParent) {
    const i64 activeSec = ActiveMs() / 1000;
    const i64 notChecked = static_cast<i64>(entries_.Size()) - Load(finished_);
    if (opt_.statusPath.Empty() && hashing_) ConsoleProgress(true);
    hashing_ = false; // the final status shows no remaining time
    {
        LockGuard g(logLock_);
        if (logOpen_) {
            log_.WriteLine("");
            if (exitCode == kExitStopped) {
                log_.WriteLine(Str(stoppedByParent ? "Stopped because Setup exited. " : "Stopped by user. ") +
                               GroupThousands(notChecked) + " files not checked.");
            }
            log_.WriteLine("Finished: " + plat::NowText() + "   Elapsed " + FormatHms(activeSec) + " (pauses not counted)");
            log_.WriteLine("Result:   OK " + GroupThousands(Load(ok_)) + "   BAD " + GroupThousands(Load(bad_)) +
                           "   MISSING " + GroupThousands(Load(missing_)) + "   ERROR " + GroupThousands(Load(error_)));
            if (!log_.Close()) logError_ = true;
            logOpen_ = false;
        }
    }
    WriteStatus(exitCode == kExitStopped ? "STOPPED" : "DONE", exitCode);
    if (opt_.statusPath.Empty()) {
        if (exitCode == kExitStopped)
            console::WriteLine(Str(stoppedByParent ? "Stopped: the starting program exited." : "Stopped.") + " " +
                               GroupThousands(notChecked) + " files not checked.");
        console::WriteLine("Result: OK " + GroupThousands(Load(ok_)) + "   BAD " + GroupThousands(Load(bad_)) +
                           "   MISSING " + GroupThousands(Load(missing_)) + "   ERROR " + GroupThousands(Load(error_)) +
                           "   elapsed " + FormatHms(activeSec));
    }
}

int Runner::Run() {
    if (!opt_.statusPath.Empty()) WriteStatus("SCANNING", -1); // the installer sees the run at once
    if (!control_.StartedOk()) return Fail("Cannot create the Pause/Stop events for -control " + opt_.control);

    Str error;
    if (!opt_.logPath.Empty()) {
        Str err;
        if (!log_.Open(opt_.logPath, &err)) return Fail("Cannot create the log file " + opt_.logPath + ": " + err);
        logOpen_ = true;
    }
    if (!LoadList(error)) return Fail(error);

    // Drive and workers: decided before the scan, so the log header is complete before the
    // first MISSING line.
    if (opt_.mode == DriveMode::Hdd) isHdd_ = true;
    else if (opt_.mode == DriveMode::Ssd) isHdd_ = false;
    else isHdd_ = env_.detectHdd ? env_.detectHdd(base_) : RealDetectHdd(base_.Empty() ? StrView(".") : StrView(base_));
    if (isHdd_) {
        rows_ = 1;
    } else {
        rows_ = opt_.threads > 0 ? opt_.threads : Min(ProcessorCount(), kMaxThreads);
        if (rows_ > kMaxThreads) rows_ = kMaxThreads;
        if (rows_ < 1) rows_ = 1;
    }
    Str listFull = path::GetFullPath(opt_.list);
    if (listFull.Empty()) listFull = opt_.list;
    Str baseFull = path::GetFullPath(base_.Empty() ? StrView(".") : StrView(base_));
    if (baseFull.Empty()) baseFull = base_;
    Str modeText = isHdd_ ? Str("HDD, 1 worker") : Str("SSD, ");
    if (!isHdd_) modeText.AppendInt(rows_).Append(rows_ == 1 ? " worker" : " workers");
    if (logOpen_) {
        log_.WriteLine("\xEF\xBB\xBFSharpSFV-Lite verification log");
        log_.WriteLine("Started:  " + plat::NowText());
        log_.WriteLine("List:     " + listFull + " (" + AlgoName(algo_) + ", " +
                       GroupThousands(static_cast<i64>(entries_.Size())) + " entries)");
        log_.WriteLine("Folder:   " + baseFull);
        log_.WriteLine("Mode:     " + modeText);
        log_.WriteLine("");
    }
    if (opt_.statusPath.Empty())
        console::WriteLine("Verifying " + listFull + " (" + AlgoName(algo_) + ", " +
                           GroupThousands(static_cast<i64>(entries_.Size())) + " entries; " + modeText + ")");

    Scan();
    if (Load(stopped_)) {
        Finish(kExitStopped, stopByParent_);
        return kExitStopped;
    }

    hashing_ = true;
    hashStartActive_ = ActiveMs();
    Tick(true);
    StartWorkers();
    while (Load(workersDone_) < started_) {
        plat::SleepMs(static_cast<u32>(kTickMs));
        Tick(false);
    }
    for (int i = 0; i < started_; ++i) threads_[i].Join();

    const bool stopped = Load(stopped_) && Load(finished_) < static_cast<int>(entries_.Size());
    int code;
    if (stopped) code = kExitStopped;
    else code = (Load(bad_) + Load(missing_) + Load(error_)) > 0 ? kExitProblems : kExitOk;
    Finish(code, stopByParent_);
    return code;
}

} // namespace

// ------------------------------------------------------------------ public API

Str GroupThousands(i64 v) {
    const bool neg = v < 0;
    u64 u = neg ? static_cast<u64>(-(v + 1)) + 1 : static_cast<u64>(v);
    char digits[32];
    int n = 0;
    do {
        if (n % 4 == 3) digits[n++] = ',';
        digits[n++] = static_cast<char>('0' + u % 10);
        u /= 10;
    } while (u > 0);
    Str s;
    if (neg) s.Append('-');
    while (n > 0) s.Append(digits[--n]);
    return s;
}

Str FormatHms(i64 seconds) {
    if (seconds < 0) seconds = 0;
    const i64 h = seconds / 3600, m = (seconds / 60) % 60, sec = seconds % 60;
    Str s;
    if (h < 10) s.Append('0');
    s.AppendInt(h).Append(':');
    s.Append(static_cast<char>('0' + m / 10)).Append(static_cast<char>('0' + m % 10)).Append(':');
    s.Append(static_cast<char>('0' + sec / 10)).Append(static_cast<char>('0' + sec % 10));
    return s;
}

Str UsageText() {
    return Str(
        "SharpSFV Lite - verifies installed files against a checksum list (for installers)\n"
        "\n"
        "Usage:\n"
        "  SharpSFV-Lite <checksum list> [options]\n"
        "\n"
        "Options:\n"
        "  -base <folder>     Folder the list's paths are relative to (default: the list's folder)\n"
        "  -status <file>     Keep a status file current (installer mode: nothing is printed)\n"
        "  -log <file>        Write one line per file, plus a summary\n"
        "  -control <name>    Pause / Stop through the events Local\\<name>.pause and Local\\<name>.stop\n"
        "  -threads <1-12>    Most files hashed at once on an SSD (default: processor count, at most 12)\n"
        "  -mode <mode>       auto | hdd | ssd (default: auto)\n"
        "  -algo <name>       crc32 | md5 | xxh3, for lists not named .sfv, .md5 or .xxh3\n"
        "\n"
        "Exit codes: 0 all OK, 1 BAD/MISSING/ERROR files, 2 could not start, 3 stopped.\n");
}

bool ParseArgs(const Vec<Str>& args, Options& out, Str& error) {
    auto value = [&](usize& i, Str& dst) -> bool {
        if (i + 1 >= args.Size()) {
            error = "Missing value after " + args[i];
            return false;
        }
        dst = args[++i];
        return true;
    };
    bool haveList = false;
    for (usize i = 0; i < args.Size(); ++i) {
        const Str& a = args[i];
        const bool isOption = (a.Size() > 1 && a[0] == '-') || a == "/?";
        if (!isOption) {
            if (haveList) {
                error = "More than one checksum list given: " + a;
                return false;
            }
            out.list = a;
            haveList = true;
            continue;
        }
        const StrView name = a.Sub(1);
        Str v;
        if (text::EqualsIgnoreCase(name, "base")) {
            if (!value(i, out.base)) return false;
        } else if (text::EqualsIgnoreCase(name, "status")) {
            if (!value(i, out.statusPath)) return false;
        } else if (text::EqualsIgnoreCase(name, "log")) {
            if (!value(i, out.logPath)) return false;
        } else if (text::EqualsIgnoreCase(name, "control")) {
            if (!value(i, out.control)) return false;
            if (out.control.Empty() || out.control.Find('\\') != kNpos || out.control.Find('/') != kNpos) {
                error = "-control: the name must not be empty or contain \\ or /";
                return false;
            }
        } else if (text::EqualsIgnoreCase(name, "threads")) {
            if (!value(i, v)) return false;
            int n = 0;
            if (!text::TryParseInt32(v, n) || n < 1 || n > kMaxThreads) {
                error = "-threads: expected a number from 1 to 12, got " + v;
                return false;
            }
            out.threads = n;
        } else if (text::EqualsIgnoreCase(name, "mode")) {
            if (!value(i, v)) return false;
            if (text::EqualsIgnoreCase(v, "auto")) out.mode = DriveMode::Auto;
            else if (text::EqualsIgnoreCase(v, "hdd")) out.mode = DriveMode::Hdd;
            else if (text::EqualsIgnoreCase(v, "ssd")) out.mode = DriveMode::Ssd;
            else {
                error = "-mode: expected auto, hdd or ssd, got " + v;
                return false;
            }
        } else if (text::EqualsIgnoreCase(name, "algo")) {
            if (!value(i, v)) return false;
            if (!ParseAlgo(v, out.algo)) {
                error = "-algo: expected crc32, md5 or xxh3, got " + v;
                return false;
            }
            out.hasAlgo = true;
        } else if (text::EqualsIgnoreCase(name, "?") || text::EqualsIgnoreCase(name, "h") ||
                   text::EqualsIgnoreCase(name, "help") || text::EqualsIgnoreCase(name, "-help")) {
            error = "";
            return false;
        } else {
            error = "Unknown option: " + a;
            return false;
        }
    }
    if (!haveList) {
        error = "No checksum list given";
        return false;
    }
    return true;
}

void WriteFailedStatus(StrView statusPath, StrView error) {
    Options opt;
    opt.statusPath = statusPath;
    NoControl control;
    NoParentWatch parent;
    Runner* r = new Runner(opt, control, parent, Environment{});
    r->ReportFailure(error);
    delete r;
}

int Run(const Options& opt, Control& control, ParentWatch& parent, const Environment& env) {
    Runner* r = new Runner(opt, control, parent, env); // large (worker slots, ring buffers)
    const int code = r->Run();
    delete r;
    return code;
}

} // namespace sfv::lite
