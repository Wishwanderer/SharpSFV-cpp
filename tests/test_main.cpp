// SharpSFV (C++ port) - unit tests.
// Self-contained (no framework). Run: sharpsfv_tests  -> exit code 0 when all pass.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "AppSettings.h"
#include "ChecksumParser.h"
#include "ChecksumWriter.h"
#include "Engine.h"
#include "FileFilter.h"
#include "FileStore.h"
#include "FileSystem.h"
#include "Hasher.h"
#include "DataStorage.h"
#include "DriveClassifier.h"
#include "EnvPath.h"
#include "JobStore.h"
#include "ListModel.h"
#include "Scripts.h"
#include "PathUtil.h"
#include "Text.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <tuple>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace sfv;

static int g_failures = 0, g_checks = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++g_failures;                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                \
        }                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                                     \
    do {                                                                                                   \
        ++g_checks;                                                                                        \
        const auto va = (a);                                                                               \
        const auto vb = (b);                                                                               \
        if (!(va == vb)) {                                                                                 \
            ++g_failures;                                                                                  \
            std::printf("FAIL %s:%d: %s == %s\n   got: [%s]\n  want: [%s]\n", __FILE__, __LINE__, #a, #b, \
                        Str(va).c_str(), Str(vb).c_str());                                                 \
        }                                                                                                  \
    } while (0)

static std::string Str(const std::string& s) { return s; }
static std::string Str(std::string_view s) { return std::string(s); }
static std::string Str(const char* s) { return s; }
static std::string Str(bool b) { return b ? "true" : "false"; }
static std::string Str(int v) { return std::to_string(v); }
static std::string Str(long long v) { return std::to_string(v); }
static std::string Str(long v) { return std::to_string(v); }
static std::string Str(std::size_t v) { return std::to_string(v); }
static std::string Str(const std::optional<std::string>& s) { return s ? "\"" + *s + "\"" : "(null)"; }
template <typename E>
    requires std::is_enum_v<E>
static std::string Str(E e) { return ToString(e); }

static std::string HexOf(HashType t, std::string_view data) {
    Digest d;
    if (!HashBuffer(t, data.data(), data.size(), d)) return "<unavailable>";
    return ToHex(d);
}

// ------------------------------------------------------------------ temp dir helpers

static std::string MakeTempDir() {
#ifdef _WIN32
    wchar_t base[MAX_PATH];
    GetTempPathW(MAX_PATH, base);
    std::wstring dir = std::wstring(base) + L"sharpsfv_test_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                       std::to_wstring(GetTickCount64());
    CreateDirectoryW(dir.c_str(), nullptr);
    return text::Narrow(dir);
#else
    char tmpl[] = "/tmp/sharpsfv_test_XXXXXX";
    return mkdtemp(tmpl);
#endif
}

static void RemoveTree(const std::string& p) {
    std::error_code ec;
#ifdef _WIN32
    std::filesystem::remove_all(std::filesystem::path(text::Widen(p)), ec);
#else
    std::filesystem::remove_all(std::filesystem::path(p), ec);
#endif
}

static void MakeDir(const std::string& p) {
#ifdef _WIN32
    CreateDirectoryW(text::Widen(p).c_str(), nullptr);
#else
    mkdir(p.c_str(), 0755);
#endif
}

static void WriteFile(const std::string& p, std::string_view content) {
    fs::TextWriter w;
    w.Open(p);
    w.Write(content);
    w.Close();
}

static std::string ReadText(const std::string& p) { return fs::ReadAllBytes(p).value_or("<missing>"); }

// ------------------------------------------------------------------ tests

static void TestHashVectors() {
    // Reference values. CRC-32 is SharpSFV's little-endian order (System.IO.Hashing.Crc32,
    // confirmed against the .NET source): standard CBF43926 reversed.
    // CRC-32 is held in Standard order (CBF43926); Reversed is what the C# SharpSFV wrote.
    CHECK_EQ(HexOf(HashType::Crc32, "123456789"), "CBF43926");
    CHECK_EQ(HexOf(HashType::Crc32, ""), "00000000");
    CHECK_EQ(HexOf(HashType::MD5, ""), "D41D8CD98F00B204E9800998ECF8427E");
    CHECK_EQ(HexOf(HashType::MD5, "abc"), "900150983CD24FB0D6963F7D28E17F72");
    CHECK_EQ(HexOf(HashType::SHA1, "abc"), "A9993E364706816ABA3E25717850C26C9CD0D89D");
    CHECK_EQ(HexOf(HashType::SHA256, "abc"), "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
    CHECK_EQ(HexOf(HashType::XXHASH3, ""), "99AA06D3014798D86001C324468D497F");

    // Streaming in odd-sized chunks must equal one-shot hashing (and reuse must reset).
    std::mt19937 rng(42);
    std::string data(3 * 1024 * 1024 + 17, '\0');
    for (char& c : data) c = static_cast<char>(rng());
    for (HashType t : {HashType::XXHASH3, HashType::Crc32, HashType::MD5, HashType::SHA1, HashType::SHA256}) {
        auto h = Hasher::Create(t);
        CHECK(h != nullptr);
        if (!h) continue;
        Digest a, b, c;
        h->Update(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        h->Final(a);
        std::size_t off = 0, step = 1;
        while (off < data.size()) {
            const std::size_t n = std::min(step, data.size() - off);
            h->Update(reinterpret_cast<const std::uint8_t*>(data.data() + off), n);
            off += n;
            step = step * 3 + 7;
        }
        h->Final(b);
        h->Update(reinterpret_cast<const std::uint8_t*>("junk"), 4); // abandoned file...
        h->Reset();                                                   // ...must not leak into the next
        h->Update(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        h->Final(c);
        CHECK_EQ(ToHex(a), ToHex(b));
        CHECK_EQ(ToHex(a), ToHex(c));
        CHECK_EQ(static_cast<int>(a.size), DigestSize(t));
    }

    // Slice-by-16 CRC vs bitwise reference.
    std::uint32_t ref = 0xFFFFFFFFu;
    for (unsigned char ch : data) {
        ref ^= ch;
        for (int k = 0; k < 8; ++k) ref = (ref & 1) ? (ref >> 1) ^ 0xEDB88320u : (ref >> 1);
    }
    ref = ~ref;
    Digest d;
    HashBuffer(HashType::Crc32, data.data(), data.size(), d);
    const std::uint32_t got = (std::uint32_t(d.bytes[0]) << 24) | (d.bytes[1] << 16) | (d.bytes[2] << 8) | d.bytes[3];
    CHECK(got == ref);
}

static void TestHex() {
    std::vector<std::uint8_t> v;
    CHECK(TryFromHex("cbF43926", v));
    CHECK_EQ(ToHex(v), "CBF43926");
    CHECK(!TryFromHex("ABC", v)); // odd length -> FormatException in .NET
    CHECK(!TryFromHex("ZZ", v));
    CHECK(TryFromHex("", v) && v.empty());
}

static void TestText() {
    CHECK_EQ(text::Trim("  \t a b \xC2\xA0"), "a b"); // NBSP is whitespace in .NET
    CHECK_EQ(text::SplitLines("a\r\nb\rc\nd\n").size(), std::size_t(4));
    CHECK_EQ(text::SplitLines("a\n\nb").size(), std::size_t(3));
    int v = 5;
    CHECK(text::TryParseInt32(" -42 ", v) && v == -42);
    CHECK(!text::TryParseInt32("12x", v) && v == 0);
    CHECK(!text::TryParseInt32("2147483648", v));
    CHECK(text::TryParseInt32("-2147483648", v) && v == -2147483647 - 1);
    CHECK(text::EqualsIgnoreCase("Hello.TXT", "hello.txt"));
    CHECK_EQ(text::FormatF1(12.34), "12.3");
    // UTF-16 LE BOM file decodes to UTF-8
    const std::string utf16("\xFF\xFE" "a\0\xE9\0", 6);
    CHECK_EQ(text::DecodeTextFile(utf16), "a\xC3\xA9");
    CHECK_EQ(text::DecodeTextFile("\xEF\xBB\xBFx"), "x");
}

static void TestParser() {
    std::string h, n;
    CHECK(MatchHashFirst("d41d8cd98f00b204e9800998ecf8427e *dir/file name.bin  ", h, n));
    CHECK_EQ(h, "d41d8cd98f00b204e9800998ecf8427e");
    CHECK_EQ(n, "dir/file name.bin");
    CHECK(MatchHashFirst("  ABCD  file", h, n) && h == "ABCD" && n == "file");
    CHECK(!MatchHashFirst("ABCD", h, n));       // needs whitespace after the hash
    CHECK(!MatchHashFirst("ABCDxyz file", h, n)); // hash must be followed by whitespace
    CHECK(MatchHashFirst("ABCD *", h, n) && n.empty());

    CHECK(MatchNameFirst("my file.iso   CBF43926  ", n, h));
    CHECK_EQ(n, "my file.iso");
    CHECK_EQ(h, "CBF43926");
    CHECK(!MatchNameFirst("file 123456789", n, h)); // 9 hex digits: not a CRC
    CHECK(!MatchNameFirst("CBF43926", n, h));
    CHECK(MatchNameFirst("   CBF43926", n, h) && n.empty());

    const std::string sfv = "; comment\r\n// shown comment \r\nfile1.bin CBF43926\r\n\r\nsub\\file 2.bin 00000000\r\n"
                            "bad line\r\nodd ABC\r\n";
    const auto p = ParseChecksumText(sfv, HashType::Crc32);
    CHECK_EQ(p.entries.size(), std::size_t(2));
    if (p.entries.size() == 2) {
        CHECK_EQ(p.entries[0].fileName, "file1.bin");
        CHECK_EQ(ToHex(p.entries[0].expectedHash), "CBF43926");
        CHECK_EQ(p.entries[1].fileName, "sub\\file 2.bin");
    }
    CHECK_EQ(p.comments, std::string("shown comment") + fs::kNewLine);

    // Hash-first lines inside an .sfv still parse (pattern A fallback).
    const auto p2 = ParseChecksumText("CBF43926 *a.bin\n", HashType::Crc32);
    CHECK(p2.entries.size() == 1 && p2.entries[0].fileName == "a.bin");

    CHECK_EQ(DetectAlgoFromExtension("x.SFV", HashType::MD5), HashType::Crc32);
    CHECK_EQ(DetectAlgoFromExtension("x.txt", HashType::MD5), HashType::MD5);
    CHECK(IsVerificationExtension("C:/a/b.Sha256"));
    CHECK(!IsVerificationExtension("b.iso"));
}

static void TestFilters() {
    CHECK(FitsMask("Movie.MKV", "*.mkv"));
    CHECK(FitsMask("data1.dat", "data?.dat"));
    CHECK(!FitsMask("data12.dat", "data?.dat"));
    CHECK(FitsMask("anything", "*.*"));
    CHECK(FitsMask("a.b.c", "a*c"));
    CHECK(!FitsMask("a.b.d", "a*c"));
    CHECK(FitsMask("x(1).txt", "x(?).txt")); // regex metacharacters are literal
    CHECK(IsFileAllowed("a.iso", "*.iso;*.mkv", ""));
    CHECK(!IsFileAllowed("a.txt", "*.iso;*.mkv", ""));
    CHECK(!IsFileAllowed("a.iso", "*.iso", "a.*")); // exclusion wins
    CHECK(IsFileAllowed("a.txt", " ; ", ""));        // effectively empty include list
}

static void TestPaths() {
#ifdef _WIN32
    CHECK_EQ(path::Combine("C:\\a", "b.txt"), "C:\\a\\b.txt");
    CHECK_EQ(path::Combine("C:\\a\\", "b.txt"), "C:\\a\\b.txt");
    CHECK_EQ(path::Combine("C:\\a", "D:\\x"), "D:\\x");
    CHECK_EQ(path::Combine("C:", "a.txt"), "C:\\a.txt");
    CHECK_EQ(path::GetDirectoryName("C:\\a\\b.txt"), std::optional<std::string>("C:\\a"));
    CHECK_EQ(path::GetDirectoryName("C:/a/b/c.txt"), std::optional<std::string>("C:\\a\\b"));
    CHECK_EQ(path::GetDirectoryName("C:\\"), std::optional<std::string>());
    CHECK_EQ(path::GetDirectoryName("C:\\x"), std::optional<std::string>("C:\\"));
    CHECK_EQ(path::GetDirectoryName("\\\\srv\\share\\f"), std::optional<std::string>("\\\\srv\\share"));
    CHECK_EQ(path::GetFileName("C:foo"), "foo");
    CHECK_EQ(path::GetPathRoot("\\\\srv\\share\\dir"), "\\\\srv\\share");
    CHECK_EQ(path::GetRelativePath("C:\\a\\b", "C:\\a\\b\\c\\d.txt"), std::optional<std::string>("c\\d.txt"));
    CHECK_EQ(path::GetRelativePath("C:\\a\\b", "C:\\a\\x.txt"), std::optional<std::string>("..\\x.txt"));
    CHECK_EQ(path::GetRelativePath("c:\\A", "C:\\a\\x.txt"), std::optional<std::string>("x.txt"));
    CHECK_EQ(path::GetRelativePath("C:\\a", "D:\\x.txt"), std::optional<std::string>("D:\\x.txt"));
    CHECK_EQ(path::FindCommonBasePath({"C:\\data\\a", "C:\\Data\\b\\c"}), "C:\\data");
    CHECK_EQ(path::FindCommonBasePath({"C:\\", "C:\\x"}), "C:");
    CHECK_EQ(path::FindCommonBasePath({"C:\\a", "D:\\a"}), "");
    CHECK_EQ(path::FindCommonBasePath({"\\\\srv\\share\\a", "\\\\srv\\share\\b"}), "\\\\srv\\share");
    CHECK_EQ(path::SanitizeFileName("a:b*c?"), "a_b_c_");
#else
    CHECK_EQ(path::Combine("/a", "b.txt"), "/a/b.txt");
    CHECK_EQ(path::Combine("/a/", "b.txt"), "/a/b.txt");
    CHECK_EQ(path::Combine("/a", "/x"), "/x");
    CHECK_EQ(path::GetDirectoryName("/a/b.txt"), std::optional<std::string>("/a"));
    CHECK_EQ(path::GetDirectoryName("/"), std::optional<std::string>());
    CHECK_EQ(path::GetDirectoryName("a.txt"), std::optional<std::string>(""));
    CHECK_EQ(path::GetRelativePath("/a/b", "/a/b/c/d.txt"), std::optional<std::string>("c/d.txt"));
    CHECK_EQ(path::GetRelativePath("/a/b", "/a/x.txt"), std::optional<std::string>("../x.txt"));
    CHECK_EQ(path::GetRelativePath("/a/b", "/a/b"), std::optional<std::string>("."));
    CHECK_EQ(path::FindCommonBasePath({"/data/a", "/data/b/c"}), "/data");
#endif
    CHECK_EQ(path::GetExtension("a.b/c"), "");
    CHECK_EQ(path::GetExtension("file.tar.gz"), ".gz");
    CHECK_EQ(path::GetExtension("file."), "");
    CHECK_EQ(path::GetFileNameWithoutExtension("dir/file.tar.gz"), "file.tar");
}

static void TestSettings(const std::string& dir) {
    AppSettings s(dir);
    s.ParseText("[SharpSFV]\r\nTimeTab=1\r\nprocessingmode=ssd\r\nPathStorageMode=1\r\nDefaultAlgo=sha256\r\n"
                "ColumnOrder=Name:2,Hash:0,Bad,Status:x,Name:1\r\nWindowSizeW=abc\r\nWindowPosX=10\r\n"
                "WindowPosY=20\r\nSplitterPos=bad\r\nSignature= Me \r\nnot a setting\r\n");
    CHECK(s.showTimeTab);
    CHECK_EQ(s.processingMode, ProcessingMode::SSD);
    CHECK_EQ(s.pathStorageMode, PathStorageMode::Absolute);
    CHECK_EQ(s.defaultAlgo, HashType::SHA256);
    CHECK_EQ(s.windowWidth, 0); // int.TryParse failure writes 0, like the original
    CHECK(s.hasCustomLocation && s.windowX == 10 && s.windowY == 20);
    CHECK_EQ(s.splitterDistance, -1);
    CHECK_EQ(s.customSignature, "Me");
    CHECK_EQ(s.columnOrder.size(), std::size_t(2));
    if (s.columnOrder.size() == 2) CHECK(s.columnOrder[0].first == "Name" && s.columnOrder[0].second == 1);

    AppSettings::SaveState st;
    st.timeTabEnabled = true;
    st.currentAlgo = HashType::Crc32;
    st.bounds = {5, 6, 1024, 768};
    st.columnOrder = {{"Name", 0}, {"Hash", 1}};
    CHECK(s.Save(st));
    AppSettings r(dir);
    r.Load();
    CHECK_EQ(r.defaultAlgo, HashType::Crc32);
    CHECK_EQ(r.windowWidth, 1024);
    CHECK_EQ(r.processingMode, ProcessingMode::SSD);
    CHECK_EQ(r.customSignature, "Me");
    const std::string ini = ReadText(r.IniPath());
    CHECK(ini.starts_with(std::string("[SharpSFV]") + fs::kNewLine + "TimeTab=1" + fs::kNewLine));
    CHECK(ini.find(std::string("ColumnOrder=Name:0,Hash:1") + fs::kNewLine) != std::string::npos);
    r.ResetToDefaults();
    CHECK(!fs::FileExists(r.IniPath()));
}

static void TestFileStoreConcurrency() {
    FileStore store;
    std::atomic<bool> done{false};
    std::thread reader([&] {
        while (!done.load()) {
            const int n = store.Count();
            for (int i = 0; i < n; i += 997) {
                volatile std::size_t len = store.FileName(i).size() + store.GetFullPath(i).size();
                (void)len;
            }
        }
    });
    const int total = FileStore::kChunkSize * 3 + 5; // crosses chunk boundaries
    for (int i = 0; i < total; ++i) store.Add("f" + std::to_string(i % 100), "dir/f", "/base", i);
    done.store(true);
    reader.join();
    CHECK_EQ(store.Count(), total);
    CHECK_EQ(store.FileName(total - 1), "f" + std::to_string((total - 1) % 100));
    store.RemoveAt(0);
    CHECK_EQ(store.Count(), total - 1);
    CHECK_EQ(store.OriginalIndex(0), 1);
    store.Clear();
    CHECK_EQ(store.Count(), 0);
}

static void TestEngine(const std::string& dir) {
    const std::string root = path::Combine(dir, "data");
    MakeDir(root);
    MakeDir(path::Combine(root, "sub"));
    MakeDir(path::Combine(root, "sub/deeper"));
    MakeDir(path::Combine(root, ".hidden"));
    WriteFile(path::Combine(root, "a.txt"), "123456789");
    WriteFile(path::Combine(root, "b.bin"), "abc");
    WriteFile(path::Combine(root, path::Combine("sub", "c.txt")), "");
    WriteFile(path::Combine(root, path::Combine("sub", path::Combine("deeper", "d.txt"))), "deep");
    WriteFile(path::Combine(root, path::Combine(".hidden", "x.txt")), "hidden"); // skipped on POSIX (dot dir)

    FileStore store;
    ProgressThrottle throttle;
    Engine engine(store, throttle);

    for (bool hdd : {true, false}) {
        store.Clear();
        HashRunOptions opt;
        opt.algo = HashType::MD5;
        opt.isHdd = hdd;
        std::atomic<int> callbacks{0};
        const RunResult r = engine.HashFiles({root}, root, opt, [&](const Counters&) { callbacks.fetch_add(1); });
#ifdef _WIN32
        const int expectedFiles = 5; // dot-directories are not hidden on Windows
#else
        const int expectedFiles = 4;
#endif
        CHECK_EQ(r.completed, expectedFiles);
        CHECK_EQ(r.ok, expectedFiles);
        CHECK_EQ(callbacks.load(), expectedFiles);
        bool sawA = false;
        for (int i = 0; i < store.Count(); ++i) {
            if (store.RelativePath(i) == "a.txt") {
                sawA = true;
                CHECK_EQ(store.GetCalculatedHashString(i), "25F9E794323B453885F5181F1B624D0B");
                CHECK_EQ(store.Status(i), ItemStatus::OK);
            }
        }
        CHECK(sawA);
    }

    // Save As (relative) then verify the written file.
    const std::string md5Path = path::Combine(root, "checksums.md5");
    AppSettings settings(dir);
    std::string err;
    CHECK(WriteSaveAs(md5Path, store, settings, HashType::MD5, &err));
    const std::string written = ReadText(md5Path);
    CHECK(written.find(std::string("25F9E794323B453885F5181F1B624D0B *a.txt") + fs::kNewLine) != std::string::npos);
    CHECK(written.find(std::string("sub") + path::kSep + "deeper" + path::kSep + "d.txt") != std::string::npos);

    // Create mode: saved in the selection's folder -> same paths as before; saved in the parent
    // folder -> paths gain the folder name, so the file still verifies where it is.
    {
        const std::string inRoot = path::Combine(root, "create.md5");
        const std::string inParent = path::Combine(path::GetDirectoryName(root).value_or(""), "create_parent.md5");
        CHECK(WriteCreateMode(inRoot, store, settings, HashType::MD5, &err));
        CHECK(WriteCreateMode(inParent, store, settings, HashType::MD5, &err));
        const std::string a = ReadText(inRoot), b = ReadText(inParent);
        CHECK(a.find(std::string("25F9E794323B453885F5181F1B624D0B *a.txt") + fs::kNewLine) != std::string::npos);
        const std::string rootName(path::GetFileName(root));
        CHECK(b.find("25F9E794323B453885F5181F1B624D0B *" + rootName + path::kSep + "a.txt" + fs::kNewLine) !=
              std::string::npos);
        fs::RemoveFile(inRoot);
        fs::RemoveFile(inParent);
    }

    auto parsed = ParseChecksumFile(md5Path, HashType::MD5, &err);
    CHECK(parsed.has_value());
    store.Clear();
    auto order = PopulateVerificationStore(store, *parsed, root, true);
    HashRunOptions vopt;
    vopt.algo = HashType::MD5;
    vopt.isHdd = false;
    RunResult vr = engine.Verify(order, vopt, nullptr);
    CHECK_EQ(vr.ok, static_cast<int>(parsed->entries.size()));
    CHECK_EQ(vr.bad + vr.missing, 0);

    // Conventional (big-endian) SFV from another tool: accepted through the endian fallback.
    const std::string sfvPath = path::Combine(root, "other_tool.sfv");
    WriteFile(sfvPath, "; made by QuickSFV\r\na.txt CBF43926\r\nb.bin 352441C2\r\nmissing.bin 00000000\r\n");
    parsed = ParseChecksumFile(sfvPath, HashType::Crc32, &err);
    store.Clear();
    order = PopulateVerificationStore(store, *parsed, root, false);
    vopt.algo = HashType::Crc32;
    vr = engine.Verify(order, vopt, nullptr);
    CHECK_EQ(vr.ok, 2);
    CHECK_EQ(vr.missing, 1);
    CHECK_EQ(vr.crcStandardMatches, 2); // written by another tool: standard order
    CHECK_EQ(vr.crcReversedMatches, 0);

    // A corrupted file is BAD.
    WriteFile(path::Combine(root, "a.txt"), "123456780");
    store.Clear();
    order = PopulateVerificationStore(store, *parsed, root, false);
    vr = engine.Verify(order, vopt, nullptr);
    CHECK_EQ(vr.bad, 1);
    CHECK_EQ(vr.ok, 1);

    // Filters (Advanced bar): only *.txt, non-recursive.
    store.Clear();
    HashRunOptions fopt;
    fopt.algo = HashType::Crc32;
    fopt.applyFilters = true;
    fopt.includePattern = "*.txt";
    fopt.recursive = false;
    RunResult fr = engine.HashFiles({root}, root, fopt, nullptr);
    CHECK_EQ(fr.completed, 1); // a.txt only
    CHECK_EQ(engine.CountFiles({root}, fopt), 1);

    // Job queue: writes "<id>.<name>.xxh3" in the root, relative paths inside.
    JobStore jobs;
    jobs.Add("my:job", dir, {root});
    AppSettings js(dir);
    int changes = 0;
    JobRunner::Hooks hooks;
    hooks.jobsChanged = [&] { ++changes; };
    const DriveResolver noStorage(nullptr);
    JobRunner::Run(jobs, engine, js, HashType::XXHASH3, false, noStorage, hooks);
    auto job = jobs.Get(0);
    CHECK(job && job->status.load() == JobStatus::Done);
    if (job) {
        const std::string outputPath = job->OutputPath(); // keep alive: GetFileName returns a view
        CHECK_EQ(path::GetFileName(outputPath), std::string("1.my") + (path::kSep == '\\' ? "_" : ":") + "job.xxh3");
        const std::string jobFile = ReadText(outputPath);
        CHECK(jobFile.find(std::string("data") + path::kSep + "a.txt") != std::string::npos);
        CHECK(job->progress.load() == 100.0);
    }
    CHECK(changes >= 3);
    jobs.RemoveCompleted();
    CHECK_EQ(jobs.Count(), 0);
    CHECK_EQ(jobs.Add("next", dir, {}), 0);
    CHECK_EQ(jobs.Get(0)->id, 2); // IDs never repeat

    // Pause blocks progress; cancel releases everything.
    store.Clear();
    engine.Pause();
    std::thread t([&] {
        HashRunOptions o;
        o.algo = HashType::SHA256;
        engine.HashFiles({root}, root, o, nullptr);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK_EQ(store.Count(), 0); // producer is parked at the pause gate
    engine.Cancel();
    t.join();
    CHECK(engine.IsCancelled());
    engine.Resume();
}

// ------------------------------------------------------------------ CRC-32 byte orders

static void TestCrcByteOrders(const std::string& dir) {
    Digest d;
    HashBuffer(HashType::Crc32, "123456789", 9, d);
    CHECK_EQ(FormatDigest(d, Crc32ByteOrder::Standard), "CBF43926");
    CHECK_EQ(FormatDigest(d, Crc32ByteOrder::Reversed), "2639F4CB");
    Digest m;
    HashBuffer(HashType::MD5, "abc", 3, m);
    CHECK_EQ(FormatDigest(m, Crc32ByteOrder::Reversed), "900150983CD24FB0D6963F7D28E17F72"); // not a CRC: unchanged

    std::vector<std::uint8_t> std_, rev, bad, md5rev;
    TryFromHex("CBF43926", std_);
    TryFromHex("2639F4CB", rev);
    TryFromHex("CBF43927", bad);
    CHECK(CompareDigest(d, std_, true) == DigestMatch::Exact);
    CHECK(CompareDigest(d, rev, true) == DigestMatch::ByteReversed);
    CHECK(CompareDigest(d, rev, false) == DigestMatch::Mismatch);
    CHECK(CompareDigest(d, bad, true) == DigestMatch::Mismatch);
    md5rev.assign(m.data(), m.data() + m.size);
    std::reverse(md5rev.begin(), md5rev.end());
    CHECK(CompareDigest(m, md5rev, false) == DigestMatch::Mismatch);

    CHECK(ParseCrc32ByteOrder("standard") == Crc32ByteOrder::Standard);
    CHECK(ParseCrc32ByteOrder("BigEndian") == Crc32ByteOrder::Standard);
    CHECK(ParseCrc32ByteOrder("QuickSFV") == Crc32ByteOrder::Standard);
    CHECK(ParseCrc32ByteOrder("reversed") == Crc32ByteOrder::Reversed);
    CHECK(ParseCrc32ByteOrder("LittleEndian") == Crc32ByteOrder::Reversed);
    CHECK(ParseCrc32ByteOrder("1") == Crc32ByteOrder::Reversed);
    CHECK(!ParseCrc32ByteOrder("legacy").has_value()); // ambiguous on purpose

    // Create in both orders, then verify each with the engine.
    const std::string root = path::Combine(dir, "crc");
    MakeDir(root);
    WriteFile(path::Combine(root, "a.txt"), "123456789");
    WriteFile(path::Combine(root, "b.bin"), "abc");
    FileStore store;
    ProgressThrottle throttle;
    Engine engine(store, throttle);
    HashRunOptions opt;
    opt.algo = HashType::Crc32;
    engine.HashFiles({path::Combine(root, "a.txt"), path::Combine(root, "b.bin")}, root, opt, nullptr);

    AppSettings s(dir);
    std::string err;
    const std::string stdPath = path::Combine(root, "standard.sfv"), revPath = path::Combine(root, "reversed.sfv");
    s.crc32ByteOrder = Crc32ByteOrder::Standard;
    CHECK(WriteSaveAs(stdPath, store, s, HashType::Crc32, &err));
    s.crc32ByteOrder = Crc32ByteOrder::Reversed;
    CHECK(WriteSaveAs(revPath, store, s, HashType::Crc32, &err));
    const std::string nl = fs::kNewLine;
    CHECK_EQ(ReadText(stdPath), "CBF43926 *a.txt" + nl + "352441C2 *b.bin" + nl);
    CHECK_EQ(ReadText(revPath), "2639F4CB *a.txt" + nl + "C2412435 *b.bin" + nl);

    HashRunOptions vopt;
    vopt.algo = HashType::Crc32;
    for (const auto& [file, wantStd, wantRev] :
         {std::tuple{stdPath, 2, 0}, std::tuple{revPath, 0, 2}}) {
        auto parsed = ParseChecksumFile(file, HashType::Crc32, &err);
        FileStore vs;
        Engine ve(vs, throttle);
        const auto order = PopulateVerificationStore(vs, *parsed, root, false);
        const RunResult r = ve.Verify(order, vopt, nullptr);
        CHECK_EQ(r.ok, 2);
        CHECK_EQ(r.crcStandardMatches, wantStd);
        CHECK_EQ(r.crcReversedMatches, wantRev);
    }

    // A mixed file (one of each order, one corrupt) and per-entry match reporting.
    const std::string mixed = path::Combine(root, "mixed.sfv");
    WriteFile(mixed, "a.txt CBF43926\r\nb.bin C2412435\r\na.txt 00000000\r\n");
    auto parsed = ParseChecksumFile(mixed, HashType::Crc32, &err);
    FileStore vs;
    Engine ve(vs, throttle);
    const auto order = PopulateVerificationStore(vs, *parsed, root, false);
    const RunResult r = ve.Verify(order, vopt, nullptr);
    CHECK_EQ(r.ok, 2);
    CHECK_EQ(r.bad, 1);
    CHECK_EQ(r.crcStandardMatches, 1);
    CHECK_EQ(r.crcReversedMatches, 1);
    CHECK(vs.CrcMatchOf(0) == CrcMatch::Standard);
    CHECK(vs.CrcMatchOf(1) == CrcMatch::Reversed);
    CHECK(vs.CrcMatchOf(2) == CrcMatch::None);
    CHECK_EQ(vs.Status(2), ItemStatus::Bad);
    CHECK_EQ(vs.GetCalculatedHashString(1), "352441C2"); // display order (Standard by default)
    vs.SetDisplayCrcOrder(Crc32ByteOrder::Reversed);
    CHECK_EQ(vs.GetCalculatedHashString(1), "C2412435");

    // "Save As" after verifying a mixed file normalizes everything to the chosen order.
    s.crc32ByteOrder = Crc32ByteOrder::Standard;
    const std::string normalized = path::Combine(root, "normalized.sfv");
    CHECK(WriteSaveAs(normalized, vs, s, HashType::Crc32, &err));
    CHECK_EQ(ReadText(normalized), "CBF43926 *a.txt" + nl + "352441C2 *b.bin" + nl + "CBF43926 *a.txt" + nl);

    // Settings round trip (new keys) and no bogus window position from the command line.
    AppSettings w(root);
    w.crc32ByteOrder = Crc32ByteOrder::Reversed;
    w.dataStorageConsent = DataConsent::Declined;
    CHECK(w.SaveCurrent());
    const std::string ini = ReadText(w.IniPath());
    CHECK(ini.find("Crc32ByteOrder=Reversed") != std::string::npos);
    CHECK(ini.find("DataStorageConsent=Declined") != std::string::npos);
    CHECK(ini.find("WindowPosX") == std::string::npos);
    AppSettings rd(root);
    rd.Load();
    CHECK_EQ(rd.crc32ByteOrder, Crc32ByteOrder::Reversed);
    CHECK_EQ(rd.dataStorageConsent, DataConsent::Declined);
    CHECK(!rd.hasCustomLocation);
}

// ------------------------------------------------------------------ consent + DriveType.ini

namespace {
struct FakeDrives {
    std::map<std::string, drive::VolumeInfo> volumes; // path -> identity
    std::map<std::string, drive::Detection> results;  // path -> what detection says
    mutable std::atomic<int> detections{0};

    DriveResolver::IdentifyFn Identify() const {
        return [this](const std::string& p) {
            auto it = volumes.find(p);
            drive::VolumeInfo v = it == volumes.end() ? drive::VolumeInfo{} : it->second;
            v.probePath = p;
            return v;
        };
    }
    DriveResolver::DetectFn Detect() const {
        return [this](const drive::VolumeInfo& v) {
            detections.fetch_add(1);
            auto it = results.find(v.probePath);
            return it == results.end() ? drive::Detection{true, true, "detection failed (defaulted to HDD)"} : it->second;
        };
    }
};

drive::VolumeInfo Vol(std::string guid, std::string serial, std::string mount, std::string fs = "NTFS") {
    drive::VolumeInfo v;
    v.guid = std::move(guid);
    v.serial = std::move(serial);
    v.mountPoint = std::move(mount);
    v.fileSystem = std::move(fs);
    v.label = "Label";
    return v;
}
} // namespace

static void TestDataStorageAndDrives(const std::string& dir) {
    using Src = DriveDecision::Source;
    const std::string appDir = path::Combine(dir, "app");
    const std::string dataDir = path::Combine(path::Combine(dir, "appdata"), "SharpSFV");
    MakeDir(appDir);
    AppSettings settings(appDir);
    DataStorage storage(settings, dataDir);

    FakeDrives fake;
    fake.volumes["ssd"] = Vol("{SSD-GUID}", "1111-1111", "D:\\");
    fake.results["ssd"] = {false, false, "volume reports no seek penalty"};
    fake.volumes["vc1"] = Vol("{GUID-OF-E}", "AAAA-AAAA", "E:\\"); // VeraCrypt container A at E:
    fake.volumes["vc2"] = Vol("{GUID-OF-E}", "BBBB-BBBB", "E:\\"); // container B, same letter -> same GUID
    fake.volumes["vc1f"] = Vol("{GUID-OF-F}", "AAAA-AAAA", "F:\\"); // container A again, now at F:
    fake.volumes["raw"] = Vol("", "", "G:\\");                       // no identity at all
    drive::VolumeInfo net;
    net.network = true;
    net.mountPoint = "\\\\nas\\media";
    fake.volumes["net"] = net;
    fake.results["net"] = {true, false, "network drive"};
    DriveResolver drives(&storage, fake.Identify(), fake.Detect());

    // --- Not asked: detect every time, store nothing.
    CHECK_EQ(storage.Consent(), DataConsent::NotAsked);
    auto d = drives.Resolve(ProcessingMode::Auto, "ssd");
    CHECK(!d.isHdd);
    CHECK(d.source == Src::DetectedNotSaved);
    CHECK_EQ(d.note, "local data storage not allowed yet");
    drives.Resolve(ProcessingMode::Auto, "ssd");
    CHECK_EQ(fake.detections.load(), 2);
    CHECK(!fs::DirectoryExists(dataDir)); // nothing created without consent
    std::string err;
    CHECK(!drives.SetDrive("ssd", true, nullptr, &err));
    CHECK(!err.empty());

    // Manual overrides that never need consent.
    CHECK(drives.Resolve(ProcessingMode::HDD, "ssd").source == Src::Setting);
    CHECK(drives.Resolve(ProcessingMode::HDD, "ssd").isHdd);
    CHECK(drives.Resolve(ProcessingMode::SSD, "vc1", true).source == Src::CommandLine);
    CHECK_EQ(fake.detections.load(), 2); // forced modes do not probe

    // --- Allowed: decision recorded in SharpSFV.ini, detection happens once per drive.
    CHECK(storage.SetConsent(DataConsent::Allowed, nullptr, &err));
    {
        AppSettings reread(appDir);
        reread.Load();
        CHECK_EQ(reread.dataStorageConsent, DataConsent::Allowed);
    }
    d = drives.Resolve(ProcessingMode::Auto, "ssd");
    CHECK(d.source == Src::DetectedSaved);
    CHECK(fs::FileExists(storage.DriveTypeFile()));
    const int afterFirst = fake.detections.load();
    for (int i = 0; i < 3; ++i) {
        d = drives.Resolve(ProcessingMode::Auto, "ssd");
        CHECK(d.source == Src::Stored);
        CHECK(!d.isHdd);
        CHECK_EQ(d.detectedBy, "volume reports no seek penalty");
    }
    CHECK_EQ(fake.detections.load(), afterFirst); // never detected again
    CHECK(drives.Resolve(ProcessingMode::HDD, "ssd").isHdd); // global setting still beats the stored value

    // --- VeraCrypt-style: detection fails, user sets SSD; it sticks.
    d = drives.Resolve(ProcessingMode::Auto, "vc1");
    CHECK(d.isHdd && d.detectionFailed && d.source == Src::DetectedSaved);
    DriveDecision set;
    CHECK(drives.SetDrive("vc1", false, &set, &err));
    CHECK(set.source == Src::UserOverride && !set.isHdd);
    d = drives.Resolve(ProcessingMode::Auto, "vc1");
    CHECK(d.source == Src::UserOverride);
    CHECK(!d.isHdd);
    // A different container mounted at the same letter shares the GUID but not the serial:
    // it gets its own entry and does NOT inherit container A's setting.
    d = drives.Resolve(ProcessingMode::Auto, "vc2");
    CHECK(d.source == Src::DetectedSaved);
    CHECK(d.isHdd);
    // Container A mounted at another letter has a new GUID; matched by serial number.
    d = drives.Resolve(ProcessingMode::Auto, "vc1f");
    CHECK(d.source == Src::UserOverride);
    CHECK(!d.isHdd);

    // Back to automatic: detected again and stored as Auto.
    CHECK(drives.SetDrive("vc1", std::nullopt, &set, &err));
    CHECK(set.source == Src::DetectedSaved && set.isHdd);
    CHECK(drives.Resolve(ProcessingMode::Auto, "vc1").source == Src::Stored);

    // No identity: cannot be stored or set.
    d = drives.Resolve(ProcessingMode::Auto, "raw");
    CHECK(d.source == Src::DetectedNotSaved);
    CHECK(!drives.SetDrive("raw", false, nullptr, &err));

    // Network share: stored under its UNC root, overridable (fast NAS -> parallel).
    CHECK(drives.Resolve(ProcessingMode::Auto, "net").isHdd);
    CHECK(drives.SetDrive("net", false, nullptr, &err));
    CHECK(!drives.Resolve(ProcessingMode::Auto, "net").isHdd);

    // File contents and round trip.
    const auto records = drives.List();
    CHECK_EQ(records.size(), std::size_t(4)); // ssd, vc1/A, vc2/B, net
    const std::string ini = ReadText(storage.DriveTypeFile());
    CHECK(ini.find("[Volume{SSD-GUID}/1111-1111]") != std::string::npos);
    CHECK(ini.find("[Volume{GUID-OF-E}/BBBB-BBBB]") != std::string::npos);
    CHECK(ini.find("[Network:\\\\nas\\media]") != std::string::npos);
    CHECK(ini.find("DetectionFailed=1") != std::string::npos);
    CHECK(ini.find("Source=User") != std::string::npos);
    CHECK_EQ(DriveTypeStore::Serialize(DriveTypeStore::Parse(ini)), ini);

    // Hand edits: lower-case keys work; a section without a valid Type is ignored.
    const auto edited = DriveTypeStore::Parse("[x]\nvolumeguid={g}\nserialnumber=1\ntype=ssd\nsource=user\n[y]\nType=fast\n");
    CHECK_EQ(edited.size(), std::size_t(1));
    if (!edited.empty()) CHECK(!edited[0].isHdd && edited[0].userSet && edited[0].guid == "{g}");

    // Many processes discovering drives at once: no entry is lost.
    {
        FakeDrives many;
        for (int i = 0; i < 8; ++i) {
            const std::string k = "m" + std::to_string(i);
            many.volumes[k] = Vol("{M" + std::to_string(i) + "}", "000" + std::to_string(i) + "-0000", "M:\\");
            many.results[k] = {false, false, "volume reports no seek penalty"};
        }
        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i)
            threads.emplace_back([&, i] {
                DriveResolver r(&storage, many.Identify(), many.Detect());
                r.Resolve(ProcessingMode::Auto, "m" + std::to_string(i));
            });
        for (auto& t : threads) t.join();
        CHECK_EQ(drives.List().size(), std::size_t(12));
    }

    // --- Withdraw: the file is deleted and nothing is read or written any more.
    std::vector<std::string> removed;
    CHECK(storage.SetConsent(DataConsent::Declined, &removed, &err));
    CHECK_EQ(removed.size(), std::size_t(1));
    CHECK(!fs::FileExists(storage.DriveTypeFile()));
    CHECK(!fs::DirectoryExists(dataDir)); // emptied folder removed too
    MakeDir(path::GetDirectoryName(dataDir).value_or(""));
    MakeDir(dataDir);
    WriteFile(storage.DriveTypeFile(), "[Volume{SSD-GUID}/1111-1111]\nType=HDD\nSource=User\n");
    d = drives.Resolve(ProcessingMode::Auto, "ssd");
    CHECK(d.source == Src::DetectedNotSaved); // stored file ignored without consent
    CHECK_EQ(d.note, "local data storage declined");
    CHECK(drives.List().empty());

    // Consent text names the real file.
    CHECK(storage.ConsentMessage().find(storage.DriveTypeFile()) != std::string::npos);

    // The real probe on this machine: never fails hard.
    const drive::VolumeInfo real = drive::IdentifyVolume(dir);
    CHECK(!real.probePath.empty());
    CHECK(!real.mountPoint.empty());
    CHECK(!drive::Detect(real).method.empty());
#ifdef _WIN32
    CHECK(!real.guid.empty() || !real.serial.empty());
#endif
}

// ------------------------------------------------------------------ GUI-facing logic

static void TestAppLogic(const std::string& dir) {
    // --- List model: sort by column kind, filters, duplicates.
    FileStore store;
    auto add = [&](const char* name, ItemStatus st, const char* data, std::int64_t ms) {
        const int i = store.Add(name, name, "/base", store.Count(), st);
        if (data) {
            Digest d;
            HashBuffer(HashType::MD5, data, std::strlen(data), d);
            store.SetResult(i, &d, ms, st);
        } else {
            store.SetResult(i, nullptr, ms, st);
        }
        return i;
    };
    add("beta.txt", ItemStatus::OK, "same", 100);   // 0
    add("Alpha.TXT", ItemStatus::Bad, "x", 20);     // 1
    add("gamma.bin", ItemStatus::OK, "same", 5);    // 2
    add("delta.bin", ItemStatus::Error, nullptr, 7); // 3: no hash
    add("eps.bin", ItemStatus::Error, nullptr, 9);  // 4: no hash
    store.SetStatus(add("zeta.bin", ItemStatus::OK, "z", 1), ItemStatus::Queued); // 5
    store.SetStatus(add("omega.bin", ItemStatus::Missing, nullptr, -1), ItemStatus::Missing); // 6

    auto all = [&] {
        std::vector<int> v(static_cast<std::size_t>(store.Count()));
        for (int i = 0; i < store.Count(); ++i) v[static_cast<std::size_t>(i)] = i;
        return v;
    };
    auto join = [](const std::vector<int>& v) {
        std::string s;
        for (int i : v) s += std::to_string(i);
        return s;
    };
    std::vector<int> v = all();
    SortIndices(v, store, ColumnKind::Name, SortOrder::Ascending);
    CHECK_EQ(join(v), "1034265"); // Alpha, beta, delta, eps, gamma, omega, zeta (case-insensitive)
    SortIndices(v, store, ColumnKind::Name, SortOrder::Descending);
    CHECK_EQ(join(v), "5624301");
    SortIndices(v, store, ColumnKind::Time, SortOrder::Ascending);
    CHECK_EQ(join(v), "6523410"); // numeric: -1,1,5,7,9,20,100 (text order would put 100 before 20)
    SortIndices(v, store, ColumnKind::Status, SortOrder::Ascending);
    CHECK_EQ(v.front(), 5); // Queued first
    SortIndices(v, store, ColumnKind::Hash, SortOrder::Ascending);
    CHECK(store.Calculated(v[0]).empty()); // no-hash items first
    SortIndices(v, store, ColumnKind::Name, SortOrder::None);
    CHECK_EQ(join(v), "0123456"); // load order restored

    FilterSpec f;
    CHECK(!FilterActive(f));
    f.text = "ALPHA";
    CHECK_EQ(join(FilterIndices(store, f, ColumnKind::Name, SortOrder::None)), "1");
    f = {};
    f.status = "BAD"; // Bad + Error + Missing
    CHECK_EQ(join(FilterIndices(store, f, ColumnKind::Name, SortOrder::None)), "1346");
    f.status = "Waiting...";
    CHECK_EQ(join(FilterIndices(store, f, ColumnKind::Name, SortOrder::None)), "5");
    f.status = "OK";
    f.text = ".bin";
    CHECK_EQ(join(FilterIndices(store, f, ColumnKind::Name, SortOrder::Descending)), "2");
    CHECK(MatchesFilter(store, 2, f) && !MatchesFilter(store, 0, f));
    f = {};
    f.duplicates = true;
    CHECK_EQ(join(FilterIndices(store, f, ColumnKind::Name, SortOrder::None)), "02"); // hash-less items are not duplicates
    const auto groups = DuplicateGroups(store);
    CHECK_EQ(groups.size(), std::size_t(1));

    std::vector<int> disp = {6, 0, 3, 5, 1};
    RemapAfterRemoval(disp, {1, 3});
    CHECK_EQ(join(disp), "403"); // 6->4, 0->0, 5->3; 3 and 1 dropped
    store.RemoveMany({1, 3, 3});
    CHECK_EQ(store.Count(), 5);
    CHECK_EQ(store.FileName(1), "gamma.bin");
    CHECK_EQ(store.FileName(4), "omega.bin");
    CHECK_EQ(store.GetCalculatedHashString(1), store.GetCalculatedHashString(0)); // digests moved with their rows

    // --- Batch scripts.
    CHECK_EQ(BatchQuote("C:\\100% done.txt"), "\"C:\\100%% done.txt\"");
    const std::string del = DeleteBadFilesScript({"C:\\a b\\x.bin", "C:\\\xC3\xA9.txt"});
    CHECK_EQ(del, "@echo off\r\nchcp 65001 >nul\r\necho Deleting BAD files...\r\ndel \"C:\\a b\\x.bin\"\r\n"
                  "del \"C:\\\xC3\xA9.txt\"\r\npause\r\n");
    const std::string cp = CopyDuplicatesScript({{"C:\\keep", "C:\\d1", "C:\\d2"}, {"C:\\k2", "C:\\e1"}});
    CHECK(cp.find("1st file -^> Dest") != std::string::npos); // '>' escaped
    CHECK(cp.find("copy /y \"C:\\keep\" \"C:\\d2\"\r\n") != std::string::npos);
    CHECK(cp.find("copy /y \"C:\\k2\" \"C:\\e1\"\r\n") != std::string::npos);
    const std::string dd = DeleteDuplicatesScript({{"C:\\keep", "C:\\d1"}});
    CHECK(dd.find("del \"C:\\d1\"") != std::string::npos && dd.find("C:\\keep") == std::string::npos);

    // --- PATH list editing.
    CHECK_EQ(PathListAdd("", "C:\\Tools"), "C:\\Tools");
    CHECK_EQ(PathListAdd("C:\\a;C:\\b;", "C:\\Tools"), "C:\\a;C:\\b;C:\\Tools");
    CHECK_EQ(PathListAdd("C:\\a;\"c:\\tools\\\"", "C:\\Tools"), "C:\\a;\"c:\\tools\\\""); // already there
    CHECK(!PathListContains("C:\\Tools2", "C:\\Tools")); // no substring match
    const ExpandFn expand = [](std::string_view s) {
        std::string r(s);
        const std::string var = "%LOCALAPPDATA%";
        if (auto p = r.find(var); p != std::string::npos) r.replace(p, var.size(), "C:\\Users\\me\\AppData\\Local");
        return r;
    };
    CHECK(PathListContains("%LOCALAPPDATA%\\SharpSFV;C:\\x", "C:\\Users\\me\\AppData\\Local\\SharpSFV", expand));
    CHECK_EQ(PathListRemove("C:\\x;%LOCALAPPDATA%\\SharpSFV;;C:\\y", "C:\\Users\\me\\AppData\\Local\\SharpSFV", expand),
             "C:\\x;C:\\y");

    // --- Drive record edits by section (drive-settings dialog).
    const DriveTypeStore ds(path::Combine(path::Combine(dir, "drvdlg"), "DriveType.ini"));
    std::string err;
    CHECK(ds.Update([](std::vector<DriveRecord>& r) {
        DriveRecord a;
        a.section = "Volume{A}/1";
        a.guid = "{A}";
        a.serial = "1";
        a.isHdd = true;
        r.push_back(a);
        DriveRecord b = a;
        b.section = "Volume{B}/2";
        r.push_back(b);
        return true;
    }, &err));
    CHECK(ds.SetType("volume{a}/1", false, &err));
    CHECK(!ds.SetType("missing", false, &err));
    auto recs = ds.Load();
    CHECK(recs.size() == 2 && !recs[0].isHdd && recs[0].userSet && !recs[0].changed.empty());
    CHECK(ds.Remove("Volume{B}/2", &err));
    CHECK_EQ(ds.Load().size(), std::size_t(1));
}

int main() {
    const std::string dir = MakeTempDir();
    TestHashVectors();
    TestHex();
    TestText();
    TestParser();
    TestFilters();
    TestPaths();
    TestSettings(dir);
    TestFileStoreConcurrency();
    TestEngine(dir);
    TestCrcByteOrders(dir);
    TestDataStorageAndDrives(dir);
    TestAppLogic(dir);
    RemoveTree(dir);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
