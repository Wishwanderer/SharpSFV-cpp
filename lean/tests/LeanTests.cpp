// SharpSFV (lean C++ rewrite) - unit tests for the building blocks and the engine pieces.
// Most cases are ported from tests/test_main.cpp (the standard build's tests), so both
// builds are held to the same expectations.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: tests for parallel runs, SharpSFV.ini writing, drives and consent.
// Modified 2026-10-04: tests for the GUI's logic (list model, jobs, scripts, writers, pause).
// Modified 2026-10-04: the PCLMULQDQ CRC-32 path against the table path.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "base/FileSystem.h"
#include "base/Path.h"
#include "base/StrSet.h"
#include "base/System.h"
#include "base/Text.h"
#include "core/ChecksumFile.h"
#include "core/DriveStore.h"
#include "core/Engine.h"
#include "core/Filter.h"
#include "core/Hash.h"
#include "core/JobStore.h"
#include "core/ListModel.h"
#include "core/Scripts.h"
#include "core/Settings.h"

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

static Str HexOf(HashType t, StrView s) {
    Hasher h;
    h.Init(t);
    h.Update(reinterpret_cast<const u8*>(s.Data()), s.Size());
    Digest d;
    h.Final(d);
    return ToHex(d);
}

static Str OptStr(const Opt<Str>& o) { return o ? *o : Str("<null>"); }

// ------------------------------------------------------------------ building blocks

static void TestStrVec() {
    Str s = "abc";
    s += "def";
    s.Append('g');
    CHECK_EQ(s, "abcdefg");
    CHECK(s.Size() == 7 && s.CStr()[7] == 0);
    CHECK(s.Find("cd") == 2 && s.Find('z') == kNpos && s.RFind('g') == 6);
    CHECK_EQ(s.Sub(2, 3), "cde");
    CHECK_EQ(s.Sub(5), "fg");
    CHECK(s.StartsWith("abc") && s.EndsWith("fg") && !s.StartsWith("b"));
    s.Append(s.View()); // appending itself must survive reallocation
    CHECK_EQ(s, "abcdefgabcdefg");
    CHECK_EQ(ToStr(0), "0");
    CHECK_EQ(ToStr(-1234567890123LL), "-1234567890123");
    CHECK(StrView("abc") < StrView("abd") && StrView("ab") < StrView("abc"));

    Vec<Str> v;
    for (int i = 0; i < 1000; ++i) v.Push(ToStr(i));
    CHECK(v.Size() == 1000);
    CHECK_EQ(v[999], "999");
    v.RemoveAt(0);
    CHECK_EQ(v[0], "1");
    v.RemoveFront(9);
    CHECK_EQ(v[0], "10");
    Vec<Str> copy = v;
    Vec<Str> moved = Move(v);
    CHECK(copy.Size() == moved.Size() && v.Empty());

    StrSet set;
    for (int i = 0; i < 5000; ++i) CHECK(set.Insert(ToStr(i)));
    for (int i = 0; i < 5000; i += 7) CHECK(!set.Insert(ToStr(i)));
    CHECK(set.Size() == 5000);
}

static void TestText() {
    CHECK_EQ(text::Trim("  \t a b \xC2\xA0"), "a b"); // NBSP is whitespace in .NET
    CHECK(text::SplitLines("a\r\nb\rc\nd\n").Size() == 4);
    CHECK(text::SplitLines("a\n\nb").Size() == 3);
    int v = 5;
    CHECK(text::TryParseInt32(" -42 ", v) && v == -42);
    CHECK(!text::TryParseInt32("12x", v) && v == 0);
    CHECK(!text::TryParseInt32("2147483648", v));
    CHECK(text::TryParseInt32("-2147483648", v) && v == -2147483647 - 1);
    CHECK(text::EqualsIgnoreCase("Hello.TXT", "hello.txt"));
    CHECK_EQ(text::FormatF1(12.34), "12.3");
    CHECK_EQ(text::FormatF1(0.25), "0.3");
    CHECK_EQ(text::FormatF1(100), "100.0");
    CHECK_EQ(text::DecodeTextFile(StrView("\xFF\xFE" "a\0\xE9\0", 6)), "a\xC3\xA9"); // UTF-16 LE
    CHECK_EQ(text::DecodeTextFile("\xEF\xBB\xBFx"), "x");
    CHECK_EQ(text::DecodeTextFile("a\xFF" "b"), "a\xEF\xBF\xBD" "b"); // invalid byte -> U+FFFD
    const Vec<Str> parts = text::Split("a;;b,c", ";,", true);
    CHECK(parts.Size() == 3);
    CHECK(text::Split("a;;b", ";", false).Size() == 3);
#ifdef _WIN32
    CHECK(text::EqualsIgnoreCase("\xC3\xA9t\xC3\xA9", "\xC3\x89T\xC3\x89")); // éte == ÉTÉ (invariant upper case)
    const text::WStr w("a\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(w.Size() == 4); // a, é, and a surrogate pair
    CHECK_EQ(text::Narrow(w.C(), w.Size()), "a\xC3\xA9\xF0\x9F\x98\x80");
#endif
}

static void TestHash() {
    CHECK_EQ(HexOf(HashType::Crc32, "123456789"), "CBF43926");
    CHECK_EQ(HexOf(HashType::Crc32, ""), "00000000");
    CHECK_EQ(HexOf(HashType::MD5, ""), "D41D8CD98F00B204E9800998ECF8427E");
    CHECK_EQ(HexOf(HashType::MD5, "abc"), "900150983CD24FB0D6963F7D28E17F72");
    CHECK_EQ(HexOf(HashType::SHA1, "abc"), "A9993E364706816ABA3E25717850C26C9CD0D89D");
    CHECK_EQ(HexOf(HashType::SHA256, "abc"), "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
    CHECK_EQ(HexOf(HashType::XXHASH3, ""), "99AA06D3014798D86001C324468D497F");

    // Streaming in odd-sized chunks equals one-shot hashing; Reset discards an abandoned file.
    Vec<u8> data;
    data.Resize(3 * 1024 * 1024 + 17);
    u32 seed = 42;
    for (u8& c : data) c = static_cast<u8>((seed = seed * 1664525u + 1013904223u) >> 24);
    const HashType all[] = {HashType::XXHASH3, HashType::Crc32, HashType::MD5, HashType::SHA1, HashType::SHA256};
    for (HashType t : all) {
        Hasher h;
        CHECK(h.Init(t));
        Digest a, b, c;
        h.Update(data.Data(), data.Size());
        h.Final(a);
        usize off = 0, step = 1;
        while (off < data.Size()) {
            const usize n = Min(step, data.Size() - off);
            h.Update(data.Data() + off, n);
            off += n;
            step = step * 3 + 7;
        }
        h.Final(b);
        h.Update(reinterpret_cast<const u8*>("junk"), 4);
        h.Reset();
        h.Update(data.Data(), data.Size());
        h.Final(c);
        CHECK_EQ(ToHex(a), ToHex(b));
        CHECK_EQ(ToHex(a), ToHex(c));
        CHECK(static_cast<int>(a.size) == DigestSize(t));
    }

    // Slice-by-16 CRC against the bitwise definition.
    u32 ref = 0xFFFFFFFFu;
    for (u8 ch : data) {
        ref ^= ch;
        for (int k = 0; k < 8; ++k) ref = (ref & 1) ? (ref >> 1) ^ 0xEDB88320u : (ref >> 1);
    }
    ref = ~ref;
    Hasher h;
    h.Init(HashType::Crc32);
    h.Update(data.Data(), data.Size());
    Digest d;
    h.Final(d);
    const u32 got = (u32(d.bytes[0]) << 24) | (u32(d.bytes[1]) << 16) | (u32(d.bytes[2]) << 8) | d.bytes[3];
    CHECK(got == ref);

    // The carry-less-multiplication path (where the CPU has it) equals the table path for every
    // length around the 16/64-byte boundaries, any alignment and any running register.
    CHECK(detail::CrcFastForTest(0xFFFFFFFFu, data.Data(), data.Size()) == ~ref);
    u32 mixed = 0x12345678u;
    for (usize len = 0; len < 600; ++len)
        for (usize align = 0; align < 16; align += 5) {
            const u8* p = data.Data() + 1000 + align;
            const u32 a = detail::CrcTableForTest(mixed, p, len), b = detail::CrcFastForTest(mixed, p, len);
            CHECK(a == b);
            mixed = mixed * 2654435761u + a;
        }
    console::WriteLine(Str("CRC-32 carry-less multiplication: ") + (detail::CrcHasClmulForTest() ? "used" : "not available"));

    // Byte orders.
    Vec<u8> std, rev;
    TryFromHex("CBF43926", std);
    TryFromHex("2639F4CB", rev);
    Digest crc;
    crc.size = 4, crc.bytes[0] = 0xCB, crc.bytes[1] = 0xF4, crc.bytes[2] = 0x39, crc.bytes[3] = 0x26;
    CHECK(CompareDigest(crc, std, true) == DigestMatch::Exact);
    CHECK(CompareDigest(crc, rev, true) == DigestMatch::ByteReversed);
    CHECK(CompareDigest(crc, rev, false) == DigestMatch::Mismatch);
    CHECK_EQ(FormatDigest(crc, Crc32ByteOrder::Standard), "CBF43926");
    CHECK_EQ(FormatDigest(crc, Crc32ByteOrder::Reversed), "2639F4CB");

    Vec<u8> v;
    CHECK(TryFromHex("cbF43926", v));
    CHECK_EQ(ToHex(v), "CBF43926");
    CHECK(!TryFromHex("ABC", v));
    CHECK(!TryFromHex("ZZ", v));
    CHECK(TryFromHex("", v) && v.Empty());
}

static void TestParser() {
    Str h, n;
    CHECK(MatchHashFirst("d41d8cd98f00b204e9800998ecf8427e *dir/file name.bin  ", h, n));
    CHECK_EQ(h, "d41d8cd98f00b204e9800998ecf8427e");
    CHECK_EQ(n, "dir/file name.bin");
    CHECK(MatchHashFirst("  ABCD  file", h, n) && h == "ABCD" && n == "file");
    CHECK(!MatchHashFirst("ABCD", h, n));
    CHECK(!MatchHashFirst("ABCDxyz file", h, n));
    CHECK(MatchHashFirst("ABCD *", h, n) && n.Empty());
    CHECK(MatchNameFirst("my file.iso   CBF43926  ", n, h));
    CHECK_EQ(n, "my file.iso");
    CHECK_EQ(h, "CBF43926");
    CHECK(!MatchNameFirst("file 123456789", n, h));
    CHECK(!MatchNameFirst("CBF43926", n, h));
    CHECK(MatchNameFirst("   CBF43926", n, h) && n.Empty());

    const char* sfv = "; comment\r\n// shown comment \r\nfile1.bin CBF43926\r\n\r\nsub\\file 2.bin 00000000\r\n"
                      "bad line\r\nodd ABC\r\n";
    const ParsedChecksumFile p = ParseChecksumText(sfv, HashType::Crc32);
    CHECK(p.entries.Size() == 2);
    if (p.entries.Size() == 2) {
        CHECK_EQ(p.entries[0].fileName, "file1.bin");
        CHECK_EQ(ToHex(p.entries[0].expectedHash), "CBF43926");
        CHECK_EQ(p.entries[1].fileName, "sub\\file 2.bin");
    }
    CHECK_EQ(p.comments, Str("shown comment") + fs::kNewLine);
    const ParsedChecksumFile p2 = ParseChecksumText("CBF43926 *a.bin\n", HashType::Crc32);
    CHECK(p2.entries.Size() == 1 && p2.entries[0].fileName == "a.bin");
    CHECK(DetectAlgoFromExtension("x.SFV", HashType::MD5) == HashType::Crc32);
    CHECK(DetectAlgoFromExtension("x.txt", HashType::MD5) == HashType::MD5);
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
    CHECK(FitsMask("x(1).txt", "x(?).txt"));
    CHECK(IsFileAllowed("a.iso", "*.iso;*.mkv", ""));
    CHECK(!IsFileAllowed("a.txt", "*.iso;*.mkv", ""));
    CHECK(!IsFileAllowed("a.iso", "*.iso", "a.*"));
    CHECK(IsFileAllowed("a.txt", " ; ", ""));
}

static void TestPaths() {
#ifdef _WIN32
    CHECK_EQ(path::Combine("C:\\a", "b.txt"), "C:\\a\\b.txt");
    CHECK_EQ(path::Combine("C:\\a\\", "b.txt"), "C:\\a\\b.txt");
    CHECK_EQ(path::Combine("C:\\a", "D:\\x"), "D:\\x");
    CHECK_EQ(path::Combine("C:", "a.txt"), "C:\\a.txt");
    CHECK_EQ(OptStr(path::GetDirectoryName("C:\\a\\b.txt")), "C:\\a");
    CHECK_EQ(OptStr(path::GetDirectoryName("C:/a/b/c.txt")), "C:\\a\\b");
    CHECK_EQ(OptStr(path::GetDirectoryName("C:\\")), "<null>");
    CHECK_EQ(OptStr(path::GetDirectoryName("C:\\x")), "C:\\");
    CHECK_EQ(OptStr(path::GetDirectoryName("\\\\srv\\share\\f")), "\\\\srv\\share");
    CHECK_EQ(path::GetFileName("C:foo"), "foo");
    CHECK_EQ(path::GetPathRoot("\\\\srv\\share\\dir"), "\\\\srv\\share");
    CHECK_EQ(OptStr(path::GetRelativePath("C:\\a\\b", "C:\\a\\b\\c\\d.txt")), "c\\d.txt");
    CHECK_EQ(OptStr(path::GetRelativePath("C:\\a\\b", "C:\\a\\x.txt")), "..\\x.txt");
    CHECK_EQ(OptStr(path::GetRelativePath("c:\\A", "C:\\a\\x.txt")), "x.txt");
    CHECK_EQ(OptStr(path::GetRelativePath("C:\\a", "D:\\x.txt")), "D:\\x.txt");
    {
        Vec<Str> a;
        a.Push("C:\\data\\a"), a.Push("C:\\Data\\b\\c");
        CHECK_EQ(path::FindCommonBasePath(a), "C:\\data");
        Vec<Str> b;
        b.Push("C:\\"), b.Push("C:\\x");
        CHECK_EQ(path::FindCommonBasePath(b), "C:");
        Vec<Str> c;
        c.Push("C:\\a"), c.Push("D:\\a");
        CHECK_EQ(path::FindCommonBasePath(c), "");
        Vec<Str> d;
        d.Push("\\\\srv\\share\\a"), d.Push("\\\\srv\\share\\b");
        CHECK_EQ(path::FindCommonBasePath(d), "\\\\srv\\share");
    }
    CHECK_EQ(path::SanitizeFileName("a:b*c?"), "a_b_c_");

    // Command line: the C runtime's quoting rules.
    const Vec<Str> args = ParseCommandLine(LR"("C:\Program Files\x.exe" a "b c" d\"e "f\\" g\\h "" "i""j")");
    CHECK(args.Size() == 7);
    if (args.Size() == 7) {
        CHECK_EQ(args[0], "a");
        CHECK_EQ(args[1], "b c");
        CHECK_EQ(args[2], "d\"e");
        CHECK_EQ(args[3], "f\\");
        CHECK_EQ(args[4], "g\\\\h");
        CHECK_EQ(args[5], "");
        CHECK_EQ(args[6], "i\"j");
    }
    const Vec<Str> uni = ParseCommandLine(L"x.exe \u00e9\u4e2d \U0001F600");
    CHECK(uni.Size() == 2 && uni[0] == "\xC3\xA9\xE4\xB8\xAD" && uni[1] == "\xF0\x9F\x98\x80");
#else
    CHECK_EQ(path::Combine("/a", "b.txt"), "/a/b.txt");
    CHECK_EQ(path::Combine("/a/", "b.txt"), "/a/b.txt");
    CHECK_EQ(path::Combine("/a", "/x"), "/x");
    CHECK_EQ(OptStr(path::GetDirectoryName("/a/b.txt")), "/a");
    CHECK_EQ(OptStr(path::GetDirectoryName("/")), "<null>");
    CHECK_EQ(OptStr(path::GetDirectoryName("a.txt")), "");
    CHECK_EQ(OptStr(path::GetRelativePath("/a/b", "/a/b/c/d.txt")), "c/d.txt");
    CHECK_EQ(OptStr(path::GetRelativePath("/a/b", "/a/x.txt")), "../x.txt");
    CHECK_EQ(OptStr(path::GetRelativePath("/a/b", "/a/b")), ".");
    Vec<Str> a;
    a.Push("/data/a"), a.Push("/data/b/c");
    CHECK_EQ(path::FindCommonBasePath(a), "/data");
#endif
    CHECK_EQ(path::GetExtension("a.b/c"), "");
    CHECK_EQ(path::GetExtension("file.tar.gz"), ".gz");
    CHECK_EQ(path::GetExtension("file."), "");
    CHECK_EQ(path::GetFileNameWithoutExtension("dir/file.tar.gz"), "file.tar");
}

static void TestSettings() {
    AppSettings s("");
    s.ParseText("[SharpSFV]\r\nTimeTab=1\r\nprocessingmode=ssd\r\nPathStorageMode=1\r\nDefaultAlgo=sha256\r\n"
                "ColumnOrder=Name:2,Hash:0,Bad,Status:x,Name:1\r\nWindowSizeW=abc\r\nWindowPosX=10\r\n"
                "WindowPosY=20\r\nSplitterPos=bad\r\nSignature= Me \r\nCrc32ByteOrder=QuickSFV\r\nnot a setting\r\n");
    CHECK(s.showTimeTab);
    CHECK(s.processingMode == ProcessingMode::SSD);
    CHECK(s.pathStorageMode == PathStorageMode::Absolute);
    CHECK(s.defaultAlgo == HashType::SHA256);
    CHECK(s.columnOrder.Size() == 2 && s.columnOrder[0].tag == "Name" && s.columnOrder[0].displayIndex == 1);
    CHECK(s.windowWidth == 0 && s.hasCustomLocation && s.windowX == 10 && s.windowY == 20);
    CHECK(s.splitterDistance == -1);
    CHECK_EQ(s.customSignature, "Me");
    CHECK(s.crc32ByteOrder == Crc32ByteOrder::Standard);
}

// ------------------------------------------------------------------ engine, end to end

static Str TempDir() {
#ifdef _WIN32
    wchar_t base[MAX_PATH];
    GetTempPathW(MAX_PATH, base);
    // A fresh folder every time (Wine reuses process ids, so the id alone is not unique).
    static int counter = 0;
    for (;;) {
        Str dir = path::Combine(text::Narrow(base), "sfvlean_" + ToStr(static_cast<i64>(GetCurrentProcessId())) + "_" +
                                                        ToStr(static_cast<i64>(GetTickCount64())) + "_" + ToStr(++counter));
        if (CreateDirectoryW(text::WStr(dir).C(), nullptr)) return dir;
    }
#else
    char tmpl[] = "/tmp/sfvlean_XXXXXX";
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

static void TestEngine() {
    const Str root = TempDir();
    MakeDir(path::Combine(root, "sub"));
    MakeDir(path::Combine(root, "sub/deeper"));
    WriteFile(path::Combine(root, "a.txt"), "hello");
    WriteFile(path::Combine(root, "b.bin"), "123456789");
    WriteFile(path::Combine(root, "sub/c.txt"), "");
    WriteFile(path::Combine(root, "sub/deeper/d.txt"), "abc");
    WriteFile(path::Combine(root, ".hidden"), "x"); // skipped on Linux (hidden), seen on Windows

    // Create, save, verify: every entry must come back OK.
    FileStore store;
    ProgressThrottle throttle;
    Engine engine(store, throttle);
    HashRunOptions opt;
    opt.algo = HashType::Crc32;
    Vec<Str> paths;
    paths.Push(root);
    int callbacks = 0;
    const RunResult r = engine.HashFiles(paths, root, opt, [&](const Counters&) { ++callbacks; });
#ifdef _WIN32
    const int expected = 5;
#else
    const int expected = 4;
#endif
    CHECK(r.completed == expected && r.ok == expected && callbacks == expected);
    bool sawB = false;
    for (int i = 0; i < store.Count(); ++i)
        if (store.RelativePath(i) == "b.bin") {
            sawB = true;
            CHECK_EQ(store.GetCalculatedHashString(i, Crc32ByteOrder::Standard), "CBF43926");
        }
    CHECK(sawB);

    AppSettings s("");
    const Str sfv = path::Combine(root, "checksums.sfv");
    Str err;
    CHECK(WriteSaveAs(sfv, store, s, HashType::Crc32, &err));
    Str written;
    fs::ReadAllBytes(sfv, written);
    CHECK(written.Find(Str("CBF43926 *b.bin") + fs::kNewLine) != kNpos);
    CHECK(written.Find(Str("sub") + path::kSep + "deeper" + path::kSep + "d.txt") != kNpos);

    ParsedChecksumFile parsed;
    CHECK(ParseChecksumFile(sfv, HashType::Crc32, parsed, &err));
    FileStore vstore;
    Engine vengine(vstore, throttle);
    const Vec<int> order = PopulateVerificationStore(vstore, parsed, root);
    const RunResult vr = vengine.Verify(order, opt, [](const Counters&) {});
    CHECK(vr.ok == expected && vr.bad == 0 && vr.missing == 0 && vr.crcStandardMatches == expected);

    // A reversed-order file verifies too; a damaged and a missing entry are reported.
    WriteFile(sfv, "b.bin 2639F4CB\r\na.txt 00000000\r\ngone.txt 12345678\r\n");
    CHECK(ParseChecksumFile(sfv, HashType::Crc32, parsed, &err));
    FileStore v2;
    Engine e2(v2, throttle);
    const RunResult r2 = e2.Verify(PopulateVerificationStore(v2, parsed, root), opt, [](const Counters&) {});
    CHECK(r2.ok == 1 && r2.bad == 1 && r2.missing == 1 && r2.crcReversedMatches == 1);
    CHECK(v2.Status(2) == ItemStatus::Missing && v2.Status(1) == ItemStatus::Bad);

    // skipPath and filters.
    HashRunOptions f;
    f.algo = HashType::MD5;
    f.applyFilters = true;
    f.includePattern = "*.txt";
    f.skipPath = path::Combine(root, "A.TXT"); // compared case-insensitively, as the standard build does
    FileStore fstore;
    Engine fe(fstore, throttle);
    fe.HashFiles(paths, root, f, [](const Counters&) {});
    CHECK(fstore.Count() == 2); // sub/c.txt, sub/deeper/d.txt (a.txt skipped)

    // Cancellation leaves the run marked cancelled.
    FileStore cstore;
    Engine ce(cstore, throttle);
    ce.Cancel();
    CHECK(ce.HashFiles(paths, root, opt, [](const Counters&) {}).cancelled);
}

// Parallel runs (SSD mode) must give exactly what the one-worker run gives.
static Str SaveAsText(FileStore& store, HashType algo, const Str& f) {
    AppSettings s("");
    Str err, text;
    WriteSaveAs(f, store, s, algo, &err);
    fs::ReadAllBytes(f, text);
    return text;
}

static void TestParallelEngine() {
    const Str root = path::Combine(TempDir(), "par");
    MakeDir(root);
    u32 seed = 12345;
    auto rnd = [&] { return seed = seed * 1103515245u + 12345u, seed >> 8; };
    for (int d = 0; d < 6; ++d) {
        const Str dir = path::Combine(root, "d" + ToStr(d));
        MakeDir(dir);
        for (int i = 0; i < 40; ++i) {
            // Mostly small files; every 13th spans several 512 KB reads.
            const usize size = i % 13 == 0 ? 600000 + rnd() % 1500000 : rnd() % 5000;
            Str content;
            content.Reserve(size);
            for (usize k = 0; k < size; ++k) content.Append(static_cast<char>(rnd()));
            WriteFile(path::Combine(dir, "f" + ToStr(i) + ".bin"), content);
        }
    }
    Vec<Str> paths;
    paths.Push(root);
    const Str file = path::Combine(root, "p.out"); // the checksum file: skipped when hashing
    const HashType algos[] = {HashType::Crc32, HashType::XXHASH3, HashType::MD5, HashType::SHA1, HashType::SHA256};
    for (const HashType algo : algos) {
        Str reference;
        static const int kCounts[] = {1, 2, 8, 0}; // 0 = ProcessorCount()
        for (const int workers : kCounts) {
            FileStore store;
            ProgressThrottle throttle;
            int frames = 0;
            throttle.SetPresenter([](void* ctx, const ProgressInfo&) { ++*static_cast<int*>(ctx); }, &frames);
            Engine engine(store, throttle);
            engine.SetWorkerCountOverride(workers);
            HashRunOptions opt;
            opt.algo = algo;
            opt.isHdd = workers == 1;
            opt.skipPath = file;
            int last = 0;
            const RunResult r = engine.HashFiles(paths, root, opt, [&](const Counters& c) {
                throttle.Update(c.completed, c.total, c.ok, c.bad, 0);
                __atomic_fetch_add(&last, 1, __ATOMIC_RELAXED);
            });
            CHECK(r.completed == 240 && r.ok == 240 && !r.cancelled && last == 240 && frames >= 1);
            const Str text = SaveAsText(store, algo, file);
            if (workers == 1) reference = text;
            else CHECK(text == reference);

            // Verify the saved file with the same number of workers, with one entry damaged.
            Str damaged = reference;
            damaged[0] = damaged[0] == '0' ? '1' : '0';
            WriteFile(file, damaged);
            ParsedChecksumFile parsed;
            Str err;
            CHECK(ParseChecksumFile(file, algo, parsed, &err));
            FileStore vstore;
            Engine ve(vstore, throttle);
            ve.SetWorkerCountOverride(workers);
            const RunResult vr = ve.Verify(PopulateVerificationStore(vstore, parsed, root), opt, [](const Counters&) {});
            CHECK_EQ(ToStr(vr.completed) + "/" + ToStr(vr.ok) + "/" + ToStr(vr.bad) + "/" + ToStr(vr.missing), "240/239/1/0");
        }
    }

    // Cancelling in the middle stops every worker and the producer (no hang), at any count.
    static const int kCancelCounts[] = {1, 3, 8};
    for (const int workers : kCancelCounts) {
        FileStore store;
        ProgressThrottle throttle;
        Engine engine(store, throttle);
        engine.SetWorkerCountOverride(workers);
        HashRunOptions opt;
        opt.algo = HashType::SHA256;
        opt.isHdd = workers == 1;
        const RunResult r = engine.HashFiles(paths, root, opt, [&](const Counters& c) {
            if (c.completed == 20) engine.Cancel();
        });
        CHECK(r.cancelled && r.completed < 240);
    }
}

// ------------------------------------------------------------------ SharpSFV.ini writing

static void TestSettingsWrite() {
    const Str dir = TempDir();
    AppSettings fresh(dir);
    fresh.Load(); // nothing there: defaults
    CHECK(fresh.CreateIfMissing());
    Str text;
    CHECK(fs::ReadAllBytes(fresh.IniPath(), text));
    const Str nl = fs::kNewLine;
    CHECK(text.StartsWith("[SharpSFV]" + nl + "TimeTab=0" + nl + "ShowThroughputStats=0" + nl));
    CHECK(text.Find("DefaultAlgo=XXHASH3" + nl) != kNpos && text.Find("ProcessingMode=Auto" + nl) != kNpos);
    CHECK(text.Find("WindowPosX") == kNpos);
    CHECK(text.Find("Crc32ByteOrder=Standard" + nl + "DataStorageConsent=NotAsked" + nl) != kNpos);
    CHECK(!fresh.CreateIfMissing()); // never rewritten
    WriteFile(fresh.IniPath(), "[SharpSFV]\r\nDefaultAlgo=MD5\r\n; mine\r\n");
    CHECK(!fresh.CreateIfMissing());
    Str kept;
    fs::ReadAllBytes(fresh.IniPath(), kept);
    CHECK_EQ(kept, "[SharpSFV]\r\nDefaultAlgo=MD5\r\n; mine\r\n");

    // Round trip of every value.
    AppSettings w(dir);
    w.showTimeTab = true, w.pathStorageMode = PathStorageMode::Absolute, w.processingMode = ProcessingMode::SSD;
    w.includePattern = "*.iso;*.bin", w.customSignature = "Me", w.defaultAlgo = HashType::SHA256;
    w.crc32ByteOrder = Crc32ByteOrder::Reversed, w.dataStorageConsent = DataConsent::Declined;
    w.windowX = 10, w.windowY = -5, w.hasCustomLocation = true, w.splitterDistance = 300;
    ColumnOrderEntry c;
    c.tag = "Hash", c.displayIndex = 2;
    w.columnOrder.Push(c);
    CHECK(w.SaveCurrent());
    AppSettings r(dir);
    r.Load();
    CHECK(r.showTimeTab && r.pathStorageMode == PathStorageMode::Absolute && r.processingMode == ProcessingMode::SSD);
    CHECK(r.includePattern == "*.iso;*.bin" && r.customSignature == "Me" && r.defaultAlgo == HashType::SHA256);
    CHECK(r.crc32ByteOrder == Crc32ByteOrder::Reversed && r.dataStorageConsent == DataConsent::Declined);
    CHECK(r.hasCustomLocation && r.windowX == 10 && r.windowY == -5 && r.splitterDistance == 300);
    CHECK(r.columnOrder.Size() == 1 && r.columnOrder[0].tag == "Hash" && r.columnOrder[0].displayIndex == 2);
    CHECK_EQ(r.Serialize(), w.Serialize());
}

// ------------------------------------------------------------------ consent + DriveType.ini

namespace {
struct FakeVolume {
    Str path;
    drive::VolumeInfo volume;
    bool hasResult = false;
    drive::Detection result;
};
Vec<FakeVolume>* g_fake = nullptr;
int g_detections = 0;

drive::VolumeInfo FakeIdentify(StrView p) {
    drive::VolumeInfo v;
    for (const FakeVolume& f : *g_fake)
        if (f.path == p) v = f.volume;
    v.probePath = p;
    return v;
}
drive::Detection FakeDetect(const drive::VolumeInfo& v) {
    __atomic_fetch_add(&g_detections, 1, __ATOMIC_RELAXED);
    for (const FakeVolume& f : *g_fake)
        if (f.path == v.probePath && f.hasResult) return f.result;
    drive::Detection d;
    d.isHdd = true, d.failed = true, d.method = "detection failed (defaulted to HDD)";
    return d;
}
void AddVol(Vec<FakeVolume>& vs, StrView path, StrView guid, StrView serial, StrView mount, int result = -1,
            StrView method = {}) {
    FakeVolume f;
    f.path = path;
    f.volume.guid = guid, f.volume.serial = serial, f.volume.mountPoint = mount;
    f.volume.fileSystem = "NTFS", f.volume.label = "Label";
    if (result >= 0) f.hasResult = true, f.result.isHdd = result == 1, f.result.method = method;
    vs.Push(Move(f));
}
} // namespace

static void TestDataStorageAndDrives() {
    using Src = DriveDecision::Source;
    const Str dir = TempDir();
    const Str appDir = path::Combine(dir, "app");
    const Str dataDir = path::Combine(path::Combine(dir, "appdata"), "SharpSFV");
    MakeDir(appDir);
    AppSettings settings(appDir);
    DataStorage storage(settings, dataDir);

    Vec<FakeVolume> fake;
    AddVol(fake, "ssd", "{SSD-GUID}", "1111-1111", "D:\\", 0, "volume reports no seek penalty");
    AddVol(fake, "vc1", "{GUID-OF-E}", "AAAA-AAAA", "E:\\"); // VeraCrypt container A at E:
    AddVol(fake, "vc2", "{GUID-OF-E}", "BBBB-BBBB", "E:\\"); // container B, same letter -> same GUID
    AddVol(fake, "vc1f", "{GUID-OF-F}", "AAAA-AAAA", "F:\\"); // container A again, now at F:
    AddVol(fake, "raw", "", "", "G:\\");                       // no identity at all
    AddVol(fake, "net", "", "", "\\\\nas\\media", 1, "network drive");
    fake[fake.Size() - 1].volume.network = true;
    g_fake = &fake;
    g_detections = 0;
    DriveResolver drives(&storage, FakeIdentify, FakeDetect);

    // --- Not asked: detect every time, store nothing.
    CHECK(storage.Consent() == DataConsent::NotAsked);
    DriveDecision d = drives.Resolve(ProcessingMode::Auto, "ssd");
    CHECK(!d.isHdd && d.source == Src::DetectedNotSaved);
    CHECK_EQ(d.note, "local data storage not allowed yet");
    drives.Resolve(ProcessingMode::Auto, "ssd");
    CHECK(g_detections == 2);
    CHECK(!fs::DirectoryExists(dataDir));
    Str err;
    CHECK(!drives.SetDrive("ssd", true, nullptr, &err) && !err.Empty());

    // Manual overrides that never need consent.
    CHECK(drives.Resolve(ProcessingMode::HDD, "ssd").source == Src::Setting);
    CHECK(drives.Resolve(ProcessingMode::HDD, "ssd").isHdd);
    CHECK(drives.Resolve(ProcessingMode::SSD, "vc1", true).source == Src::CommandLine);
    CHECK(g_detections == 2);

    // --- Allowed: decision recorded in SharpSFV.ini, detection happens once per drive.
    CHECK(storage.SetConsent(DataConsent::Allowed, nullptr, &err));
    {
        AppSettings reread(appDir);
        reread.Load();
        CHECK(reread.dataStorageConsent == DataConsent::Allowed);
    }
    d = drives.Resolve(ProcessingMode::Auto, "ssd");
    CHECK(d.source == Src::DetectedSaved);
    CHECK(fs::FileExists(storage.DriveTypeFile()));
    const int afterFirst = g_detections;
    for (int i = 0; i < 3; ++i) {
        d = drives.Resolve(ProcessingMode::Auto, "ssd");
        CHECK(d.source == Src::Stored && !d.isHdd);
        CHECK_EQ(d.detectedBy, "volume reports no seek penalty");
    }
    CHECK(g_detections == afterFirst);
    CHECK(drives.Resolve(ProcessingMode::HDD, "ssd").isHdd);

    // --- VeraCrypt-style: detection fails, user sets SSD; it sticks.
    d = drives.Resolve(ProcessingMode::Auto, "vc1");
    CHECK(d.isHdd && d.detectionFailed && d.source == Src::DetectedSaved);
    DriveDecision set;
    CHECK(drives.SetDrive("vc1", false, &set, &err));
    CHECK(set.source == Src::UserOverride && !set.isHdd);
    d = drives.Resolve(ProcessingMode::Auto, "vc1");
    CHECK(d.source == Src::UserOverride && !d.isHdd);
    d = drives.Resolve(ProcessingMode::Auto, "vc2"); // same GUID, other serial: its own entry
    CHECK(d.source == Src::DetectedSaved && d.isHdd);
    d = drives.Resolve(ProcessingMode::Auto, "vc1f"); // new GUID, same serial: matched
    CHECK(d.source == Src::UserOverride && !d.isHdd);

    // Back to automatic: detected again and stored as Auto.
    CHECK(drives.SetDrive("vc1", Opt<bool>(), &set, &err));
    CHECK(set.source == Src::DetectedSaved && set.isHdd);
    CHECK(drives.Resolve(ProcessingMode::Auto, "vc1").source == Src::Stored);

    // No identity: cannot be stored or set.
    d = drives.Resolve(ProcessingMode::Auto, "raw");
    CHECK(d.source == Src::DetectedNotSaved);
    CHECK(!drives.SetDrive("raw", false, nullptr, &err));

    // Network share: stored under its UNC root, overridable.
    CHECK(drives.Resolve(ProcessingMode::Auto, "net").isHdd);
    CHECK(drives.SetDrive("net", false, nullptr, &err));
    CHECK(!drives.Resolve(ProcessingMode::Auto, "net").isHdd);

    // File contents and round trip.
    CHECK(drives.List().Size() == 4);
    Str ini;
    fs::ReadAllBytes(storage.DriveTypeFile(), ini);
    CHECK(ini.Find("[Volume{SSD-GUID}/1111-1111]") != kNpos);
    CHECK(ini.Find("[Volume{GUID-OF-E}/BBBB-BBBB]") != kNpos);
    CHECK(ini.Find("[Network:\\\\nas\\media]") != kNpos);
    CHECK(ini.Find("DetectionFailed=1") != kNpos && ini.Find("Source=User") != kNpos);
    CHECK_EQ(DriveTypeStore::Serialize(DriveTypeStore::Parse(ini)), ini);

    // Hand edits: lower-case keys work; a section without a valid Type is ignored.
    const Vec<DriveRecord> edited = DriveTypeStore::Parse("[x]\nvolumeguid={g}\nserialnumber=1\ntype=ssd\nsource=user\n[y]\nType=fast\n");
    CHECK(edited.Size() == 1);
    if (edited.Size() == 1) CHECK(!edited[0].isHdd && edited[0].userSet && edited[0].guid == "{g}");

    // Several resolvers discovering drives at once: no entry is lost (cross-process lock).
    {
        Vec<FakeVolume> many;
        for (int i = 0; i < 8; ++i)
            AddVol(many, "m" + ToStr(i), "{M" + ToStr(i) + "}", "000" + ToStr(i) + "-0000", "M:\\", 0,
                   "volume reports no seek penalty");
        g_fake = &many;
        struct Job {
            DataStorage* storage;
            int i;
            void operator()() {
                DriveResolver r(storage, FakeIdentify, FakeDetect);
                r.Resolve(ProcessingMode::Auto, "m" + ToStr(i));
            }
        };
        Job jobs[8];
        Thread threads[8];
        for (int i = 0; i < 8; ++i) jobs[i] = Job{&storage, i}, threads[i].Start(jobs[i]);
        for (Thread& t : threads) t.Join();
        g_fake = &fake;
        CHECK(drives.List().Size() == 12);
    }

    // --- Withdraw: the file is deleted and nothing is read or written any more.
    Vec<Str> removed;
    CHECK(storage.SetConsent(DataConsent::Declined, &removed, &err));
    CHECK(removed.Size() == 1);
    CHECK(!fs::FileExists(storage.DriveTypeFile()));
    CHECK(!fs::DirectoryExists(dataDir));
    MakeDir(path::GetDirectoryName(dataDir).ValueOr(Str()));
    MakeDir(dataDir);
    WriteFile(storage.DriveTypeFile(), "[Volume{SSD-GUID}/1111-1111]\nType=HDD\nSource=User\n");
    d = drives.Resolve(ProcessingMode::Auto, "ssd");
    CHECK(d.source == Src::DetectedNotSaved);
    CHECK_EQ(d.note, "local data storage declined");
    CHECK(drives.List().Size() == 0);
    CHECK(storage.ConsentMessage().Find(storage.DriveTypeFile()) != kNpos);

    // Decision texts.
    DriveDecision t;
    t.isHdd = false, t.source = Src::Stored, t.recorded = "2026-01-02 03:04", t.detectedBy = "x";
    CHECK_EQ(DescribeDecision(t), "SSD (Parallel) - detected 2026-01-02 03:04: x");
    t.source = Src::DetectedNotSaved, t.note = "why";
    CHECK_EQ(DescribeDecision(t), "SSD (Parallel) - detected now: x (not saved: why)");

    // The real probe on this machine: never fails hard.
    const drive::VolumeInfo real = drive::IdentifyVolume(dir);
    CHECK(!real.probePath.Empty() && !real.mountPoint.Empty());
    CHECK(!drive::Detect(real).method.Empty());
    CHECK(drive::GetDebugInfo(dir).Find(">>> DETECTION: ") != kNpos);
}

// ------------------------------------------------------------------ the GUI's logic

static Str Join(const Vec<int>& v) {
    Str s;
    for (const int i : v) s += ToStr(i);
    return s;
}

static void TestAppLogic() {
    // --- List model: sort by column kind, filters, duplicates (tests/test_main.cpp TestAppLogic).
    FileStore store;
    auto add = [&](const char* name, ItemStatus st, const char* data, i64 ms) {
        const int i = store.Add(name, name, "/base", store.Count(), st);
        if (data) {
            Hasher h;
            h.Init(HashType::MD5);
            h.Update(reinterpret_cast<const u8*>(data), strlen(data));
            Digest d;
            h.Final(d);
            store.SetResult(i, &d, ms, st);
        } else {
            store.SetResult(i, nullptr, ms, st);
        }
        return i;
    };
    add("beta.txt", ItemStatus::OK, "same", 100);    // 0
    add("Alpha.TXT", ItemStatus::Bad, "x", 20);      // 1
    add("gamma.bin", ItemStatus::OK, "same", 5);     // 2
    add("delta.bin", ItemStatus::Error, nullptr, 7); // 3: no hash
    add("eps.bin", ItemStatus::Error, nullptr, 9);   // 4: no hash
    store.SetStatus(add("zeta.bin", ItemStatus::OK, "z", 1), ItemStatus::Queued);              // 5
    store.SetStatus(add("omega.bin", ItemStatus::Missing, nullptr, -1), ItemStatus::Missing); // 6

    auto all = [&] {
        Vec<int> v;
        for (int i = 0; i < store.Count(); ++i) v.Push(i);
        return v;
    };
    Vec<int> v = all();
    SortIndices(v, store, ColumnKind::Name, SortOrder::Ascending);
    CHECK_EQ(Join(v), "1034265");
    SortIndices(v, store, ColumnKind::Name, SortOrder::Descending);
    CHECK_EQ(Join(v), "5624301");
    SortIndices(v, store, ColumnKind::Time, SortOrder::Ascending);
    CHECK_EQ(Join(v), "6523410");
    SortIndices(v, store, ColumnKind::Status, SortOrder::Ascending);
    CHECK(v[0] == 5);
    SortIndices(v, store, ColumnKind::Hash, SortOrder::Ascending);
    CHECK(store.Calculated(v[0]).Empty());
    SortIndices(v, store, ColumnKind::Name, SortOrder::None);
    CHECK_EQ(Join(v), "0123456");

    FilterSpec f;
    CHECK(!FilterActive(f));
    f.text = "ALPHA";
    CHECK_EQ(Join(FilterIndices(store, f, ColumnKind::Name, SortOrder::None)), "1");
    f = FilterSpec();
    f.status = "BAD";
    CHECK_EQ(Join(FilterIndices(store, f, ColumnKind::Name, SortOrder::None)), "1346");
    f.status = "Waiting...";
    CHECK_EQ(Join(FilterIndices(store, f, ColumnKind::Name, SortOrder::None)), "5");
    f.status = "OK";
    f.text = ".bin";
    CHECK_EQ(Join(FilterIndices(store, f, ColumnKind::Name, SortOrder::Descending)), "2");
    CHECK(MatchesFilter(store, 2, f) && !MatchesFilter(store, 0, f));
    f = FilterSpec();
    f.duplicates = true;
    CHECK_EQ(Join(FilterIndices(store, f, ColumnKind::Name, SortOrder::None)), "02");
    CHECK(DuplicateGroups(store).Size() == 1);

    Vec<int> disp;
    static const int kDisp[] = {6, 0, 3, 5, 1};
    for (const int i : kDisp) disp.Push(i);
    Vec<int> removed;
    removed.Push(1), removed.Push(3);
    RemapAfterRemoval(disp, removed);
    CHECK_EQ(Join(disp), "403");
    removed.Push(3);
    store.RemoveMany(removed);
    CHECK(store.Count() == 5);
    CHECK_EQ(store.FileName(1), "gamma.bin");
    CHECK_EQ(store.FileName(4), "omega.bin");
    CHECK_EQ(store.GetCalculatedHashString(1), store.GetCalculatedHashString(0));
    CHECK_EQ(store.TimeStr(1), "5 ms");
    CHECK_EQ(store.TimeStr(4), "");

    // A larger stable sort (merge passes): equal keys keep their order.
    {
        FileStore big;
        for (int i = 0; i < 1000; ++i) big.Add(ToStr(i % 7), "x", "/b", i);
        Vec<int> order;
        for (int i = 0; i < 1000; ++i) order.Push(999 - i);
        SortIndices(order, big, ColumnKind::Name, SortOrder::Ascending);
        bool sorted = true;
        for (usize k = 1; k < order.Size(); ++k) {
            const int a = order[k - 1], b = order[k];
            const int ka = a % 7, kb = b % 7;
            if (ka > kb || (ka == kb && a < b)) sorted = false; // stable: input was descending
        }
        CHECK(sorted);
    }

    // --- Batch scripts.
    CHECK_EQ(BatchQuote("C:\\100% done.txt"), "\"C:\\100%% done.txt\"");
    Vec<Str> bad;
    bad.Push("C:\\a b\\x.bin"), bad.Push("C:\\\xC3\xA9.txt");
    CHECK_EQ(DeleteBadFilesScript(bad), "@echo off\r\nchcp 65001 >nul\r\necho Deleting BAD files...\r\ndel \"C:\\a b\\x.bin\"\r\n"
                                        "del \"C:\\\xC3\xA9.txt\"\r\npause\r\n");
    Vec<Vec<Str>> groups;
    {
        Vec<Str> g1, g2;
        g1.Push("C:\\keep"), g1.Push("C:\\d1"), g1.Push("C:\\d2");
        g2.Push("C:\\k2"), g2.Push("C:\\e1");
        groups.Push(Move(g1)), groups.Push(Move(g2));
    }
    const Str cp = CopyDuplicatesScript(groups);
    CHECK(cp.Find("1st file -^> Dest") != kNpos);
    CHECK(cp.Find("copy /y \"C:\\keep\" \"C:\\d2\"\r\n") != kNpos);
    CHECK(cp.Find("copy /y \"C:\\k2\" \"C:\\e1\"\r\n") != kNpos);
    const Str dd = DeleteDuplicatesScript(groups);
    CHECK(dd.Find("del \"C:\\d1\"") != kNpos && dd.Find("del \"C:\\keep\"") == kNpos);

    // --- PATH list editing.
    CHECK_EQ(PathListAdd("", "C:\\Tools"), "C:\\Tools");
    CHECK_EQ(PathListAdd("C:\\a;C:\\b;", "C:\\Tools"), "C:\\a;C:\\b;C:\\Tools");
    CHECK_EQ(PathListAdd("C:\\a;\"c:\\tools\\\"", "C:\\Tools"), "C:\\a;\"c:\\tools\\\"");
    CHECK(!PathListContains("C:\\Tools2", "C:\\Tools"));
    ExpandFn expand = [](StrView s) {
        Str r(s);
        const usize p = r.Find("%LOCALAPPDATA%");
        if (p != kNpos) r = Str(r.Sub(0, p)) + "C:\\Users\\me\\AppData\\Local" + r.Sub(p + 14);
        return r;
    };
    CHECK(PathListContains("%LOCALAPPDATA%\\SharpSFV;C:\\x", "C:\\Users\\me\\AppData\\Local\\SharpSFV", expand));
    CHECK_EQ(PathListRemove("C:\\x;%LOCALAPPDATA%\\SharpSFV;;C:\\y", "C:\\Users\\me\\AppData\\Local\\SharpSFV", expand),
             "C:\\x;C:\\y");

    // --- Drive record edits by section (drive dialog).
    const DriveTypeStore ds(path::Combine(path::Combine(TempDir(), "drvdlg"), "DriveType.ini"));
    Str err;
    CHECK(ds.Update([](Vec<DriveRecord>& r) {
        DriveRecord a;
        a.section = "Volume{A}/1", a.guid = "{A}", a.serial = "1", a.isHdd = true;
        r.Push(a);
        DriveRecord b = a;
        b.section = "Volume{B}/2";
        r.Push(b);
        return true;
    }, &err));
    CHECK(ds.SetType("volume{a}/1", false, &err));
    CHECK(!ds.SetType("missing", false, &err));
    Vec<DriveRecord> recs = ds.Load();
    CHECK(recs.Size() == 2 && !recs[0].isHdd && recs[0].userSet && !recs[0].changed.Empty());
    CHECK(ds.Remove("Volume{B}/2", &err));
    CHECK(ds.Load().Size() == 1);
}

namespace {
struct RecordingObserver : EngineObserver {
    Mutex m;
    Vec<int> discovered;
    int begins = 0, ends = 0, progresses = 0;
    void OnItemsDiscovered(Vec<int>& idx) override {
        LockGuard g(m);
        for (const int i : idx) discovered.Push(i);
    }
    void OnLargeFileBegin(int, StrView) override {
        LockGuard g(m);
        ++begins;
    }
    void OnLargeFileProgress(int, double) override {
        LockGuard g(m);
        ++progresses;
    }
    void OnLargeFileEnd(int) override {
        LockGuard g(m);
        ++ends;
    }
};
} // namespace

static void TestEngineForGui() {
    const Str dir = TempDir();
    const Str root = path::Combine(dir, "data");
    MakeDir(root);
    MakeDir(path::Combine(root, "sub"));
    WriteFile(path::Combine(root, "a.txt"), "123456789");
    WriteFile(path::Combine(root, "b.bin"), "abc");
    WriteFile(path::Combine(root, path::Combine("sub", "c.txt")), "");
    Str bigData;
    bigData.Resize(52u * 1024 * 1024, 'x'); // over the 50 MB "large file" threshold
    WriteFile(path::Combine(root, "big.dat"), bigData);
    bigData = Str();

    FileStore store;
    ProgressThrottle throttle;
    RecordingObserver obs;
    Engine engine(store, throttle, &obs);
    Vec<Str> paths;
    paths.Push(root);

    // Observer: every item is reported once; the big file announces itself.
    HashRunOptions opt;
    opt.algo = HashType::MD5;
    opt.updateFileList = true;
    opt.reportLargeFiles = true;
    opt.isHdd = false;
    const RunResult r = engine.HashFiles(paths, root, opt, [](const Counters&) {});
    CHECK(r.completed == 4 && r.ok == 4);
    CHECK(obs.discovered.Size() == 4 && obs.begins == 1 && obs.ends == 1);
    CHECK(engine.CountFiles(paths, opt) == 4);

    // Create mode: saved in the folder -> plain names; saved in the parent -> folder name added.
    AppSettings settings(dir);
    Str err;
    const Str inRoot = path::Combine(root, "create.md5");
    const Str inParent = path::Combine(dir, "create_parent.md5");
    CHECK(WriteCreateMode(inRoot, store, settings, HashType::MD5, &err));
    CHECK(WriteCreateMode(inParent, store, settings, HashType::MD5, &err));
    Str a, b;
    fs::ReadAllBytes(inRoot, a);
    fs::ReadAllBytes(inParent, b);
    CHECK(a.Find(Str("25F9E794323B453885F5181F1B624D0B *a.txt") + fs::kNewLine) != kNpos);
    CHECK(b.Find(Str("25F9E794323B453885F5181F1B624D0B *data") + path::kSep + "a.txt" + fs::kNewLine) != kNpos);
    fs::RemoveFile(inRoot);

    // GUI verification order: sorted by full path.
    WriteFile(path::Combine(root, "list.md5"), "900150983CD24FB0D6963F7D28E17F72 *b.bin\r\n"
                                               "25F9E794323B453885F5181F1B624D0B *A.txt\r\n");
    ParsedChecksumFile parsed;
    CHECK(ParseChecksumFile(path::Combine(root, "list.md5"), HashType::MD5, parsed, &err));
    FileStore vs;
    const Vec<int> order = PopulateVerificationStore(vs, parsed, root, true);
    CHECK_EQ(Join(order), "10");

    // Job queue: writes "<id>.<name>.xxh3" in the root, relative paths inside.
    JobStore jobs;
    Vec<Str> in;
    in.Push(root);
    jobs.Add("my:job", dir, in);
    int changes = 0;
    JobRunner::Hooks hooks;
    hooks.ctx = &changes;
    hooks.jobsChanged = [](void* c) { ++*static_cast<int*>(c); };
    const DriveResolver noStorage(nullptr);
    JobRunner::Run(jobs, engine, settings, HashType::XXHASH3, false, noStorage, hooks);
    JobRef job = jobs.Get(0);
    CHECK(job && job->Status() == JobStatus::Done);
    if (job) {
        const Str outputPath = job->OutputPath();
        CHECK_EQ(path::GetFileName(outputPath), Str("1.my") + (path::kSep == '\\' ? "_" : ":") + "job.xxh3");
        Str jobFile;
        fs::ReadAllBytes(outputPath, jobFile);
        CHECK(jobFile.Find(Str("data") + path::kSep + "a.txt") != kNpos);
        CHECK(job->Progress() == 100.0);
        CHECK(job->TimeStr().EndsWith(" ms"));
    }
    CHECK(changes >= 3);
    jobs.RemoveCompleted();
    CHECK(jobs.Count() == 0);
    CHECK(jobs.Add("next", dir, Vec<Str>()) == 0);
    CHECK(jobs.Get(0)->id == 2); // IDs never repeat
    CHECK(!jobs.Get(5));
    CHECK(job->CompareExchangeStatus(JobStatus::Done, JobStatus::Error) && job->Status() == JobStatus::Error);

    // Pause blocks progress; cancel releases everything.
    store.Clear();
    engine.Pause();
    struct Run {
        Engine* e;
        Vec<Str>* paths;
        const Str* root;
        void operator()() {
            HashRunOptions o;
            o.algo = HashType::SHA256;
            e->HashFiles(*paths, *root, o, [](const Counters&) {});
        }
    } run{&engine, &paths, &root};
    Thread t;
    t.Start(run);
#ifdef _WIN32
    Sleep(200);
#else
    usleep(200000);
#endif
    CHECK(store.Count() == 0); // the producer is parked at the pause gate
    engine.Cancel();
    t.Join();
    CHECK(engine.IsCancelled());
    engine.Resume();
    engine.ResetCancel();
    CHECK(!engine.IsCancelled());
    fs::RemoveFile(path::Combine(root, "big.dat"));
}

int main() {
    TestStrVec();
    TestText();
    TestHash();
    TestParser();
    TestFilters();
    TestPaths();
    TestSettings();
    TestEngine();
    TestParallelEngine();
    TestSettingsWrite();
    TestDataStorageAndDrives();
    TestAppLogic();
    TestEngineForGui();
    console::WriteLine(ToStr(g_checks) + " checks, " + ToStr(g_failures) + " failures");
    return g_failures == 0 ? 0 : 1;
}
