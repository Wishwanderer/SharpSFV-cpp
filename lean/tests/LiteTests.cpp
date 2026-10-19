// SharpSFV Lite - unit tests (see lite/LiteVerify.h and the spec).
// Runs whole verifications on generated files: verdicts, counters, the status file, the log,
// Pause / Stop, the parent going away, failures, and argument parsing.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-06: added (SharpSFV Lite).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "base/FileSystem.h"
#include "base/Path.h"
#include "base/System.h"
#include "base/Text.h"
#include "base/Thread.h"
#include "core/Hash.h"
#include "lite/LitePlatform.h"
#include "lite/LiteVerify.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace sfv;

static int g_checks = 0, g_failures = 0;

static void Report(bool ok, const char* expr, const char* file, int line, StrView detail = {}) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    console::WriteError(Str("FAIL ") + file + ":" + ToStr(line) + "  " + expr + (detail.Empty() ? Str() : "  " + Str(detail)));
}
#define CHECK(e) Report((e), #e, __FILE__, __LINE__)
#define CHECK_EQ(a, b)                                                                                        \
    do {                                                                                                      \
        const Str sa_(a), sb_(b);                                                                             \
        Report(sa_ == sb_, #a " == " #b, __FILE__, __LINE__, Str("got [") + sa_ + "] want [" + sb_ + "]");    \
    } while (0)

// ------------------------------------------------------------------ helpers

static Str TempDir() {
#ifdef _WIN32
    wchar_t base[MAX_PATH];
    GetTempPathW(MAX_PATH, base);
    static int counter = 0;
    for (;;) {
        Str dir = path::Combine(text::Narrow(base), "sfvlite_" + ToStr(static_cast<i64>(GetCurrentProcessId())) + "_" +
                                                        ToStr(static_cast<i64>(GetTickCount64())) + "_" + ToStr(++counter));
        if (CreateDirectoryW(text::WStr(dir).C(), nullptr)) return dir;
    }
#else
    char tmpl[] = "/tmp/sfvlite_XXXXXX";
    return mkdtemp(tmpl);
#endif
}

static void MakeDir(const Str& p) {
#ifdef _WIN32
    CreateDirectoryW(text::WStr(p).C(), nullptr);
#else
    mkdir(p.CStr(), 0755);
#endif
}

static void WriteFile(const Str& p, StrView content) {
    fs::TextWriter w;
    w.Open(p);
    w.Write(content);
    w.Close();
}

static Str ReadFile(const Str& p) {
    Str s;
    fs::ReadAllBytes(p, s);
    return s;
}

// Pseudo-random file content of `size` bytes.
static Str Content(u32 seed, usize size) {
    Str s;
    s.Resize(size);
    for (usize i = 0; i < size; ++i) s[i] = static_cast<char>((seed = seed * 1664525u + 1013904223u) >> 24);
    return s;
}

static Digest DigestOf(HashType t, StrView data) {
    Hasher h;
    h.Init(t);
    h.Update(reinterpret_cast<const u8*>(data.Data()), data.Size());
    Digest d;
    h.Final(d);
    return d;
}

static Str ListLine(HashType t, StrView name, const Digest& d, bool reversedCrc = false) {
    if (t == HashType::Crc32) return Str(name) + " " + FormatDigest(d, reversedCrc ? Crc32ByteOrder::Reversed : Crc32ByteOrder::Standard);
    return ToHex(d) + " *" + name;
}

// The status file as key -> rest-of-line (worker rows as "worker N").
struct Status {
    Vec<Str> keys, values;
    bool bom = false;
    Str Get(StrView key) const {
        for (usize i = 0; i < keys.Size(); ++i)
            if (keys[i] == key) return values[i];
        return Str("<absent>");
    }
    int Count(StrView key) const {
        int n = 0;
        for (const Str& k : keys)
            if (k == key) ++n;
        return n;
    }
};

static Status ReadStatus(const Str& p) {
    Status st;
    Str raw = ReadFile(p);
    StrView v = raw;
    if (v.StartsWith("\xEF\xBB\xBF")) {
        st.bom = true;
        v = v.Sub(3);
    }
    for (const Str& line : text::SplitLines(v)) {
        if (line.Empty()) continue;
        const usize sp = line.Find(' ');
        Str key = sp == kNpos ? line : Str(line.Sub(0, sp));
        Str val = sp == kNpos ? Str() : Str(line.Sub(sp + 1));
        if (key == "worker") { // "worker N rest" -> key "worker N"
            const usize sp2 = val.Find(' ');
            key = "worker " + Str(val.Sub(0, sp2));
            val = sp2 == kNpos ? Str() : Str(val.Sub(sp2 + 1));
        }
        st.keys.Push(Move(key));
        st.values.Push(Move(val));
    }
    return st;
}

static bool HasLine(const Str& text, StrView line) {
    for (const Str& l : text::SplitLines(text))
        if (l == line) return true;
    return false;
}

static bool HasLineStarting(const Str& text, StrView prefix) {
    for (const Str& l : text::SplitLines(text))
        if (l.StartsWith(prefix)) return true;
    return false;
}

static bool AlwaysHdd(StrView) { return true; }
static bool AlwaysSsd(StrView) { return false; }

class TestControl final : public lite::Control {
public:
    void Poll(bool& pause, bool& stop) override {
        pause = __atomic_load_n(&pause_, __ATOMIC_ACQUIRE);
        stop = __atomic_load_n(&stop_, __ATOMIC_ACQUIRE);
        __atomic_add_fetch(&polls_, 1, __ATOMIC_RELAXED);
    }
    void SetPause(bool v) { __atomic_store_n(&pause_, v, __ATOMIC_RELEASE); }
    void SetStop(bool v) { __atomic_store_n(&stop_, v, __ATOMIC_RELEASE); }

private:
    bool pause_ = false, stop_ = false;
    int polls_ = 0;
};

class CountdownParent final : public lite::ParentWatch {
public:
    explicit CountdownParent(int polls) : left_(polls) {}
    bool ParentGone() override { return --left_ < 0; }

private:
    int left_;
};

// A set of files plus a list for one algorithm.
struct Corpus {
    Str root, listPath;
    int okCount = 0;
};

// 6 good files (one with a space and one with a non-ASCII name, one empty, one in a
// subfolder), 1 damaged, 1 missing.
static Corpus MakeCorpus(HashType algo, StrView ext, bool reversedCrc = false) {
    Corpus c;
    c.root = TempDir();
    MakeDir(path::Combine(c.root, "Data"));
    MakeDir(path::Combine(c.root, "Verify"));
    struct F {
        const char* name;
        usize size;
    };
    const F files[] = {{"Bin/game.exe", 70000},     {"Data/core.pak", 1300000}, {"Data/music bank.bank", 5000},
                       {"Data/m\xC3\xBAsica.ogg", 777}, {"Data/empty.txt", 0},       {"readme.txt", 123}};
    MakeDir(path::Combine(c.root, "Bin"));
    Str list = "; SharpSFV Lite test list\r\n";
    u32 seed = 7;
    for (const F& f : files) {
        const Str data = Content(++seed, f.size);
        WriteFile(path::Combine(c.root, f.name), data);
        list += ListLine(algo, f.name, DigestOf(algo, data), reversedCrc) + "\r\n";
        ++c.okCount;
    }
    // Damaged: listed with the digest of different content.
    WriteFile(path::Combine(c.root, "Data/broken.dat"), Content(99, 4096));
    list += ListLine(algo, "Data/broken.dat", DigestOf(algo, Content(98, 4096)), reversedCrc) + "\r\n";
    // Missing.
    list += ListLine(algo, "Data/hd/tex07.pak", DigestOf(algo, "x"), reversedCrc) + "\r\n";
    c.listPath = path::Combine(c.root, Str("Verify/game") + ext);
    WriteFile(c.listPath, list);
    return c;
}

// ------------------------------------------------------------------ tests

static void TestHelpers() {
    CHECK_EQ(lite::GroupThousands(0), "0");
    CHECK_EQ(lite::GroupThousands(999), "999");
    CHECK_EQ(lite::GroupThousands(1000), "1,000");
    CHECK_EQ(lite::GroupThousands(20120), "20,120");
    CHECK_EQ(lite::GroupThousands(1234567890), "1,234,567,890");
    CHECK_EQ(lite::FormatHms(0), "00:00:00");
    CHECK_EQ(lite::FormatHms(192), "00:03:12");
    CHECK_EQ(lite::FormatHms(3600 * 27 + 61), "27:01:01");
    CHECK_EQ(lite::FormatHms(-5), "00:00:00");
}

static void TestArgs() {
    Vec<Str> a;
    lite::Options o;
    Str err;
    a.Push("C:\\Game\\Verify\\game.xxh3");
    a.Push("-base"), a.Push("C:\\Game");
    a.Push("-STATUS"), a.Push("s.txt");
    a.Push("-log"), a.Push("v.log");
    a.Push("-control"), a.Push("Setup-42");
    a.Push("-threads"), a.Push("12");
    a.Push("-mode"), a.Push("ssd");
    a.Push("-algo"), a.Push("md5");
    CHECK(lite::ParseArgs(a, o, err));
    CHECK_EQ(o.list, "C:\\Game\\Verify\\game.xxh3");
    CHECK_EQ(o.base, "C:\\Game");
    CHECK_EQ(o.statusPath, "s.txt");
    CHECK_EQ(o.logPath, "v.log");
    CHECK_EQ(o.control, "Setup-42");
    CHECK(o.threads == 12 && o.mode == lite::DriveMode::Ssd && o.hasAlgo && o.algo == HashType::MD5);

    struct Bad {
        const char* args[3];
        const char* errorStart;
    };
    const Bad bad[] = {{{"x.sfv", "-threads", "13"}, "-threads"}, {{"x.sfv", "-threads", "0"}, "-threads"},
                       {{"x.sfv", "-mode", "fast"}, "-mode"},    {{"x.sfv", "-algo", "sha1"}, "-algo"},
                       {{"x.sfv", "-control", "a\\b"}, "-control"}, {{"x.sfv", "-bogus", ""}, "Unknown option"},
                       {{"x.sfv", "y.sfv", ""}, "More than one"},  {{"-status", "s.txt", ""}, "No checksum list"},
                       {{"x.sfv", "-log", nullptr}, "Missing value"}};
    for (const Bad& b : bad) {
        Vec<Str> v;
        for (const char* s : b.args)
            if (s && *s) v.Push(s);
        lite::Options o2;
        Str e2;
        CHECK(!lite::ParseArgs(v, o2, e2));
        CHECK(e2.StartsWith(b.errorStart));
    }
    // A bad value after -status still leaves the status path, for a FAILED status.
    Vec<Str> v;
    v.Push("x.sfv"), v.Push("-status"), v.Push("s.txt"), v.Push("-threads"), v.Push("99");
    lite::Options o3;
    Str e3;
    CHECK(!lite::ParseArgs(v, o3, e3));
    CHECK_EQ(o3.statusPath, "s.txt");
    // Help: false with an empty message.
    Vec<Str> h;
    h.Push("-?");
    lite::Options o4;
    Str e4 = "x";
    CHECK(!lite::ParseArgs(h, o4, e4) && e4.Empty());
}

static void TestVerdicts(HashType algo, StrView ext, bool reversedCrc, bool ssd) {
    const Corpus c = MakeCorpus(algo, ext, reversedCrc);
    lite::Options o;
    o.list = c.listPath;
    o.base = c.root;
    o.statusPath = path::Combine(c.root, "status.txt");
    o.logPath = path::Combine(c.root, "verify.log");
    if (ssd) o.threads = 3;
    lite::Environment env;
    env.detectHdd = ssd ? AlwaysSsd : AlwaysHdd;
    lite::NoControl control;
    lite::NoParentWatch parent;
    const int code = lite::Run(o, control, parent, env);
    CHECK(code == lite::kExitProblems);

    const Status st = ReadStatus(o.statusPath);
    CHECK(st.bom);
    CHECK(st.keys.Size() > 0 && st.keys[0] == "SharpSFV-Lite");
    CHECK_EQ(st.Get("SharpSFV-Lite"), "status 1");
    CHECK_EQ(st.Get("state"), "DONE");
    CHECK_EQ(st.Get("counts"), "6 1 1 0");
    CHECK_EQ(st.Get("progress"), "1000 8 8");
    CHECK_EQ(st.Get("exit"), "1");
    CHECK(st.Get("time").EndsWith(" -"));
    CHECK(st.Count("worker 1") == 1);
    CHECK(st.Count("worker 3") == (ssd ? 1 : 0));
    CHECK(st.Count("worker 4") == 0);
    CHECK_EQ(st.Get("worker 1"), "-");
    // Bytes: everything present was read.
    i64 expectBytes = 70000 + 1300000 + 5000 + 777 + 0 + 123 + 4096;
    CHECK_EQ(st.Get("bytes"), ToStr(expectBytes) + " " + ToStr(expectBytes));
    CHECK(!fs::FileExists(o.statusPath + ".tmp"));

    const Str log = ReadFile(o.logPath);
    CHECK(log.StartsWith("\xEF\xBB\xBFSharpSFV-Lite verification log"));
    CHECK(HasLineStarting(log, "Started:  "));
    CHECK(HasLineStarting(log, "List:     "));
    CHECK(HasLineStarting(log, Str("Mode:     ") + (ssd ? "SSD, 3 workers" : "HDD, 1 worker")));
    CHECK(HasLine(log, "OK       Bin/game.exe"));
    CHECK(HasLine(log, "OK       Data/music bank.bank"));
    CHECK(HasLine(log, "OK       Data/m\xC3\xBAsica.ogg"));
    CHECK(HasLine(log, "OK       Data/empty.txt"));
    CHECK(HasLine(log, "MISSING  Data/hd/tex07.pak"));
    const Str expected = ListLine(algo, "", DigestOf(algo, Content(98, 4096)), reversedCrc);
    Str expHex = algo == HashType::Crc32 ? Str(text::Trim(expected)) : Str(expected.Sub(0, expected.Find(' ')));
    const Digest actualD = DigestOf(algo, Content(99, 4096));
    const Str actHex = ToHex(actualD); // CRC-32: always standard (QuickSFV) byte order
    CHECK(HasLine(log, "BAD      Data/broken.dat   expected " + expHex + "   actual " + actHex));
    CHECK(HasLine(log, "Result:   OK 6   BAD 1   MISSING 1   ERROR 0"));
    CHECK(HasLineStarting(log, "Finished: "));
    // MISSING lines come before every OK/BAD line (they are found by the size scan).
    const usize missingAt = log.Find("MISSING  "), firstOk = log.Find("OK       ");
    CHECK(missingAt != kNpos && firstOk != kNpos && missingAt < firstOk);
}

static void TestAllOkAndConsole() {
    const Corpus c = MakeCorpus(HashType::XXHASH3, ".xxh3");
    // Drop the damaged and missing entries.
    Str list = ReadFile(c.listPath);
    Str kept;
    for (const Str& l : text::SplitLines(list))
        if (l.Find("broken") == kNpos && l.Find("tex07") == kNpos && !l.Empty()) kept += l + "\r\n";
    WriteFile(c.listPath, kept);
    lite::Options o;
    o.list = c.listPath;
    o.base = c.root;
    o.mode = lite::DriveMode::Ssd;
    lite::NoControl control;
    lite::NoParentWatch parent;
    CHECK(lite::Run(o, control, parent) == lite::kExitOk); // console mode: prints, writes no files
    CHECK(!fs::FileExists(path::Combine(c.root, "status.txt")));

    // Default base = the list's folder: paths no longer resolve, so all are missing.
    lite::Options o2;
    o2.list = c.listPath;
    o2.statusPath = path::Combine(c.root, "s2.txt");
    o2.mode = lite::DriveMode::Hdd;
    CHECK(lite::Run(o2, control, parent) == lite::kExitProblems);
    CHECK_EQ(ReadStatus(o2.statusPath).Get("counts"), "0 0 6 0");
    CHECK_EQ(ReadStatus(o2.statusPath).Get("bytes"), "0 0");
    CHECK_EQ(ReadStatus(o2.statusPath).Get("progress"), "1000 6 6");
}

static void TestFailures() {
    const Str root = TempDir();
    lite::NoControl control;
    lite::NoParentWatch parent;
    lite::Options o;
    o.statusPath = path::Combine(root, "status.txt");
    o.list = path::Combine(root, "nope.sfv");
    CHECK(lite::Run(o, control, parent) == lite::kExitFailed);
    Status st = ReadStatus(o.statusPath);
    CHECK_EQ(st.Get("state"), "FAILED");
    CHECK_EQ(st.Get("exit"), "2");
    CHECK(st.Get("error").StartsWith("Checksum list not found"));

    WriteFile(path::Combine(root, "list.txt"), "x 00000000\r\n");
    o.list = path::Combine(root, "list.txt");
    CHECK(lite::Run(o, control, parent) == lite::kExitFailed);
    CHECK(ReadStatus(o.statusPath).Get("error").Find("-algo") != kNpos);
    o.hasAlgo = true, o.algo = HashType::Crc32; // with -algo the .txt list is accepted
    o.mode = lite::DriveMode::Hdd;
    CHECK(lite::Run(o, control, parent) == lite::kExitProblems); // x is missing
    CHECK_EQ(ReadStatus(o.statusPath).Get("counts"), "0 0 1 0");

    WriteFile(path::Combine(root, "empty.sfv"), "; nothing here\r\n");
    lite::Options e;
    e.statusPath = o.statusPath;
    e.list = path::Combine(root, "empty.sfv");
    CHECK(lite::Run(e, control, parent) == lite::kExitFailed);
    CHECK(ReadStatus(o.statusPath).Get("error").StartsWith("The checksum list has no entries"));

    // A log in a folder that does not exist.
    lite::Options l;
    l.statusPath = o.statusPath;
    l.list = path::Combine(root, "list.txt");
    l.hasAlgo = true;
    l.logPath = path::Combine(root, "no/such/dir/v.log");
    CHECK(lite::Run(l, control, parent) == lite::kExitFailed);
    CHECK(ReadStatus(o.statusPath).Get("error").StartsWith("Cannot create the log file"));

    // Argument errors (before a run) in installer mode.
    lite::WriteFailedStatus(o.statusPath, "-threads: expected a number from 1 to 12, got 99");
    st = ReadStatus(o.statusPath);
    CHECK_EQ(st.Get("state"), "FAILED");
    CHECK_EQ(st.Get("error"), "-threads: expected a number from 1 to 12, got 99");
    CHECK_EQ(st.Get("time"), "0 -");
    CHECK_EQ(st.Get("exit"), "2");
}

// Runs lite::Run on a thread (so the test can drive Pause / Stop meanwhile).
struct RunThread {
    lite::Options opt;
    lite::Control* control = nullptr;
    lite::ParentWatch* parent = nullptr;
    lite::Environment env;
    int code = -1;
    Thread t;
    void Start() { t.Start(*this); }
    void operator()() { __atomic_store_n(&code, lite::Run(opt, *control, *parent, env), __ATOMIC_RELEASE); }
    int Wait() {
        t.Join();
        return __atomic_load_n(&code, __ATOMIC_ACQUIRE);
    }
};

static bool WaitForState(const Str& statusPath, StrView state, int ms) {
    for (int waited = 0; waited < ms; waited += 20) {
        if (fs::FileExists(statusPath) && ReadStatus(statusPath).Get("state") == state) return true;
        lite::plat::SleepMs(20);
    }
    return false;
}

static void TestPauseResumeStop() {
    const Corpus c = MakeCorpus(HashType::MD5, ".md5");
    // 1. Paused from the start: the scan holds, the clock holds, Stop ends it.
    {
        TestControl control;
        control.SetPause(true);
        lite::NoParentWatch parent;
        RunThread r;
        r.opt.list = c.listPath, r.opt.base = c.root;
        r.opt.statusPath = path::Combine(c.root, "p1.txt");
        r.opt.logPath = path::Combine(c.root, "p1.log");
        r.env.detectHdd = AlwaysHdd;
        r.control = &control, r.parent = &parent;
        r.Start();
        CHECK(WaitForState(r.opt.statusPath, "PAUSED", 5000));
        const Str t1 = ReadStatus(r.opt.statusPath).Get("time");
        lite::plat::SleepMs(1300);
        const Status st = ReadStatus(r.opt.statusPath);
        CHECK_EQ(st.Get("state"), "PAUSED");
        CHECK_EQ(st.Get("time"), t1); // elapsed does not move while paused, remaining is "-"
        CHECK(st.Get("time").EndsWith(" -"));
        CHECK_EQ(st.Get("counts"), "0 0 0 0");
        control.SetStop(true);
        CHECK(r.Wait() == lite::kExitStopped);
        const Status end = ReadStatus(r.opt.statusPath);
        CHECK_EQ(end.Get("state"), "STOPPED");
        CHECK_EQ(end.Get("exit"), "3");
        const Str log = ReadFile(r.opt.logPath);
        CHECK(HasLine(log, "Stopped by user. 8 files not checked."));
        CHECK(HasLine(log, "Result:   OK 0   BAD 0   MISSING 0   ERROR 0"));
    }
    // 2. Paused from the start, then resumed: the run completes normally.
    {
        TestControl control;
        control.SetPause(true);
        lite::NoParentWatch parent;
        RunThread r;
        r.opt.list = c.listPath, r.opt.base = c.root;
        r.opt.statusPath = path::Combine(c.root, "p2.txt");
        r.env.detectHdd = AlwaysSsd;
        r.opt.threads = 2;
        r.control = &control, r.parent = &parent;
        r.Start();
        CHECK(WaitForState(r.opt.statusPath, "PAUSED", 5000));
        control.SetPause(false);
        CHECK(r.Wait() == lite::kExitProblems);
        const Status st = ReadStatus(r.opt.statusPath);
        CHECK_EQ(st.Get("state"), "DONE");
        CHECK_EQ(st.Get("counts"), "6 1 1 0");
        CHECK(st.Count("worker 2") == 1);
    }
    // 3. Stop before anything: exit 3 even with nothing hashed.
    {
        TestControl control;
        control.SetStop(true);
        lite::NoParentWatch parent;
        lite::Options o;
        o.list = c.listPath, o.base = c.root, o.statusPath = path::Combine(c.root, "p3.txt");
        o.mode = lite::DriveMode::Hdd;
        CHECK(lite::Run(o, control, parent) == lite::kExitStopped);
        CHECK_EQ(ReadStatus(o.statusPath).Get("state"), "STOPPED");
    }
    // 4. Setup exits: treated as Stop, with its own log line.
    {
        lite::NoControl control;
        CountdownParent parent(0);
        lite::Options o;
        o.list = c.listPath, o.base = c.root;
        o.statusPath = path::Combine(c.root, "p4.txt");
        o.logPath = path::Combine(c.root, "p4.log");
        o.mode = lite::DriveMode::Hdd;
        CHECK(lite::Run(o, control, parent) == lite::kExitStopped);
        CHECK(HasLine(ReadFile(o.logPath), "Stopped because Setup exited. 8 files not checked."));
    }
}

// A big run, paused while hashing: bytes stop moving and worker rows keep their files.
static void TestPauseWhileHashing() {
    const Str root = TempDir();
    Str list;
    const int files = 48;
    for (int i = 0; i < files; ++i) {
        const Str name = "f" + ToStr(i) + ".bin";
        const Str data = Content(static_cast<u32>(i), 8u * 1024 * 1024);
        WriteFile(path::Combine(root, name), data);
        list += ListLine(HashType::MD5, name, DigestOf(HashType::MD5, data)) + "\n";
    }
    WriteFile(path::Combine(root, "big.md5"), list);
    TestControl control;
    lite::NoParentWatch parent;
    RunThread r;
    r.opt.list = path::Combine(root, "big.md5");
    r.opt.statusPath = path::Combine(root, "s.txt");
    r.opt.logPath = path::Combine(root, "v.log");
    r.env.detectHdd = AlwaysHdd; // one worker: about 400 MB of MD5 takes long enough to pause
    r.control = &control, r.parent = &parent;
    r.Start();
    CHECK(WaitForState(r.opt.statusPath, "RUNNING", 5000));
    control.SetPause(true);
    const bool paused = WaitForState(r.opt.statusPath, "PAUSED", 3000);
    const Status a = ReadStatus(r.opt.statusPath);
    if (paused && a.Get("counts") != ToStr(files) + " 0 0 0") { // still running when paused
        lite::plat::SleepMs(700);
        const Status b = ReadStatus(r.opt.statusPath);
        CHECK_EQ(b.Get("state"), "PAUSED");
        CHECK_EQ(b.Get("bytes"), a.Get("bytes"));
        CHECK_EQ(b.Get("counts"), a.Get("counts"));
        CHECK_EQ(b.Get("time"), a.Get("time"));
        CHECK_EQ(b.Get("worker 1"), a.Get("worker 1"));
        console::WriteLine("pause while hashing: exercised");
    } else {
        console::WriteLine("pause while hashing: the run finished first (not exercised)");
    }
    control.SetPause(false);
    CHECK(r.Wait() == lite::kExitOk);
    const Status end = ReadStatus(r.opt.statusPath);
    CHECK_EQ(end.Get("counts"), ToStr(files) + " 0 0 0");
    CHECK_EQ(end.Get("progress"), "1000 48 48");
    for (int i = 0; i < files; ++i) fs::RemoveFile(path::Combine(root, "f" + ToStr(i) + ".bin")); // ~400 MB
}

#ifdef _WIN32
// A file another program holds open without sharing: ERROR, with Windows' reason.
static void TestErrorVerdict() {
    const Corpus c = MakeCorpus(HashType::Crc32, ".sfv");
    HANDLE locked = CreateFileW(text::WStr(path::Combine(c.root, "readme.txt")).C(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(locked != INVALID_HANDLE_VALUE);
    lite::Options o;
    o.list = c.listPath, o.base = c.root;
    o.statusPath = path::Combine(c.root, "s.txt");
    o.logPath = path::Combine(c.root, "v.log");
    o.mode = lite::DriveMode::Hdd;
    lite::NoControl control;
    lite::NoParentWatch parent;
    CHECK(lite::Run(o, control, parent) == lite::kExitProblems);
    CloseHandle(locked);
    CHECK_EQ(ReadStatus(o.statusPath).Get("counts"), "5 1 1 1");
    const Str log = ReadFile(o.logPath);
    CHECK(HasLineStarting(log, "ERROR    readme.txt   cannot open: "));
    CHECK(HasLine(log, "Result:   OK 5   BAD 1   MISSING 1   ERROR 1"));
}

// The named events, as Inno's [Code] would set them.
static void TestEvents() {
    lite::plat::EventControl ctl("SharpSFVLiteTest-" + ToStr(static_cast<i64>(GetCurrentProcessId())));
    CHECK(ctl.StartedOk());
    bool pause = true, stop = true;
    ctl.Poll(pause, stop);
    CHECK(!pause && !stop);
    HANDLE p = CreateEventW(nullptr, TRUE, FALSE,
                            text::WStr("Local\\SharpSFVLiteTest-" + ToStr(static_cast<i64>(GetCurrentProcessId())) + ".pause").C());
    HANDLE s = CreateEventW(nullptr, TRUE, FALSE,
                            text::WStr("Local\\SharpSFVLiteTest-" + ToStr(static_cast<i64>(GetCurrentProcessId())) + ".stop").C());
    SetEvent(p);
    ctl.Poll(pause, stop);
    CHECK(pause && !stop);
    ResetEvent(p);
    SetEvent(s);
    ctl.Poll(pause, stop);
    CHECK(!pause && stop);
    CloseHandle(p);
    CloseHandle(s);
}
#endif

int main() {
    TestHelpers();
    TestArgs();
    TestVerdicts(HashType::Crc32, ".sfv", false, false);
    TestVerdicts(HashType::Crc32, ".sfv", true, true); // reversed byte order (C# SharpSFV style)
    TestVerdicts(HashType::MD5, ".md5", false, true);
    TestVerdicts(HashType::XXHASH3, ".xxh3", false, false);
    TestVerdicts(HashType::XXHASH3, ".xxh3", false, true);
    TestAllOkAndConsole();
    TestFailures();
    TestPauseResumeStop();
    TestPauseWhileHashing();
#ifdef _WIN32
    TestErrorVerdict();
    TestEvents();
#endif
    console::WriteLine(ToStr(g_checks) + " checks, " + ToStr(g_failures) + " failures");
    return g_failures == 0 ? 0 : 1;
}
