// SharpSFV (lean C++ rewrite) - probe for scripts/compare_with_oracle.sh.
// Same commands and output as tests/probe_main.cpp (the standard build's probe), so the lean
// engine is compared case by case with the C# reference code (tools/oracle).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "base/FileSystem.h"
#include "base/Path.h"
#include "base/System.h"
#include "base/Text.h"
#include "core/ChecksumFile.h"
#include "core/Engine.h"
#include "core/Filter.h"
#include "core/Hash.h"
#include "core/Settings.h"

#include <stdlib.h>
#include <unistd.h>

using namespace sfv;

static void Out(StrView s) { console::Write(s); }


static int Hash(const Vec<Str>& a) {
    const HashType t = *ParseHashType(a[1]);
    Hasher h;
    h.Init(t);
    Vec<u8> buf;
    buf.Resize(512 * 1024);
    for (usize i = 2; i < a.Size(); ++i) {
        fs::FileReader r;
        Str hex = "ERROR";
        if (r.Open(a[i])) {
            h.Reset();
            i64 n;
            while ((n = r.Read(buf.Data(), buf.Size())) > 0) h.Update(buf.Data(), static_cast<usize>(n));
            Digest d;
            if (n == 0 && h.Final(d)) hex = FormatDigest(d, Crc32ByteOrder::Reversed); // .NET prints CRC-32 reversed
        }
        Out(hex + "\t" + a[i] + "\n");
    }
    return 0;
}

static int Parse(const Vec<Str>& a) {
    Str err;
    ParsedChecksumFile p;
    if (!ParseChecksumFile(a[2], *ParseHashType(a[1]), p, &err)) return 1;
    for (const ChecksumEntry& e : p.entries) Out(ToHex(e.expectedHash) + "|" + e.fileName + "|\n");
    const Str& c = p.comments;
    Str esc;
    for (usize i = 0; i < c.Size(); ++i) {
        if (c[i] == '\r' && i + 1 < c.Size() && c[i + 1] == '\n') continue;
        if (c[i] == '\n') esc += "\\n";
        else esc += c[i];
    }
    Out("COMMENTS:" + esc + "\n");
    return 0;
}

static int Mask() {
    Str input;
    char buf[65536];
    for (;;) {
        const ssize_t n = read(0, buf, sizeof buf);
        if (n <= 0) break;
        input.Append(StrView(buf, static_cast<usize>(n)));
    }
    // std::getline semantics: lines split on '\n' only, a trailing '\r' removed.
    usize start = 0;
    while (start < input.Size()) {
        usize nl = input.Find('\n', start);
        if (nl == kNpos) nl = input.Size();
        Str line(input.Sub(start, nl - start));
        start = nl + 1;
        if (!line.Empty() && line.Back() == '\r') line.PopBack();
        const Vec<Str> p = text::Split(line, "\t", false);
        Out(IsFileAllowed(p[0], p[1], p[2]) ? "True\n" : "False\n");
    }
    return 0;
}

static int Ini(const Vec<Str>& a) {
    AppSettings s("");
    Str bytes;
    fs::ReadAllBytes(a[1], bytes);
    s.ParseText(text::DecodeTextFile(bytes));
    auto B = [](bool b) { return b ? "1" : "0"; };
    Str o;
    o += Str("tt=") + B(s.showTimeTab) + " fp=" + B(s.showFilterPanel) + " ts=" + B(s.showThroughputStats) +
         " ab=" + B(s.showAdvancedBar) + " full=" + B(s.showFullPaths) + " hc=" + B(s.showHashCol) +
         " ehc=" + B(s.showExpectedHashCol) + " lock=" + B(s.lockColumns) + "\n";
    o += Str("psm=") + ToString(s.pathStorageMode) + " pm=" + ToString(s.processingMode) +
         " cmt=" + B(s.enableChecksumComments) + " rec=" + B(s.scanRecursive) + " algo=" + ToString(s.defaultAlgo) + "\n";
    o += "prefix=[" + s.pathPrefix + "] inc=[" + s.includePattern + "] exc=[" + s.excludePattern + "] sig=[" +
         s.customSignature + "]\n";
    o += "w=" + ToStr(s.windowWidth) + " h=" + ToStr(s.windowHeight) + " x=" + ToStr(s.hasCustomLocation ? s.windowX : 0) +
         " y=" + ToStr(s.hasCustomLocation ? s.windowY : 0) + " custom=" + B(s.hasCustomLocation) +
         " split=" + ToStr(s.splitterDistance) + "\n";
    o += "cols=";
    for (usize i = 0; i < s.columnOrder.Size(); ++i)
        o += Str(i ? "," : "") + s.columnOrder[i].tag + ":" + ToStr(s.columnOrder[i].displayIndex);
    Out(o + "\n");
    return 0;
}

static int Create(const Vec<Str>& a, Crc32ByteOrder order) {
    AppSettings s("");
    s.crc32ByteOrder = order;
    s.showAdvancedBar = a[5] == "1";
    s.includePattern = a[6];
    s.excludePattern = a[7];
    s.pathPrefix = a[8];
    s.enableChecksumComments = a[9] == "1";
    s.pathStorageMode = a[10] == "1" ? PathStorageMode::Absolute : PathStorageMode::Relative;

    HashRunOptions opt;
    opt.algo = *ParseHashType(a[2]);
    opt.applyFilters = s.showAdvancedBar;
    opt.includePattern = s.includePattern;
    opt.excludePattern = s.excludePattern;
    opt.recursive = a[4] == "1";

    FileStore store;
    ProgressThrottle throttle;
    Engine engine(store, throttle);
    Vec<Str> paths;
    for (usize i = 11; i < a.Size(); ++i) paths.Push(a[i]);
    engine.HashFiles(paths, a[3], opt, [](const Counters&) {});
    Str err;
    return WriteSaveAs(a[1], store, s, opt.algo, &err) ? 0 : 1;
}

static int Verify(const Vec<Str>& a) {
    const HashType algo = DetectAlgoFromExtension(a[1], *ParseHashType(a[2]));
    Str err;
    ParsedChecksumFile parsed;
    if (!ParseChecksumFile(a[1], algo, parsed, &err)) return 1;
    FileStore store;
    ProgressThrottle throttle;
    Engine engine(store, throttle);
    const Vec<int> order = PopulateVerificationStore(store, parsed, path::GetDirectoryName(a[1]).ValueOr(Str()));
    HashRunOptions opt;
    opt.algo = algo;
    const RunResult r = engine.Verify(order, opt, [](const Counters&) {});
    for (int i = 0; i < store.Count(); ++i) Out(Str(ToString(store.Status(i))) + "|" + store.RelativePath(i) + "\n");
    // The C# "legacy" flag: matched only after reversing its (Reversed-order) digest, i.e.
    // the file used Standard order. A palindromic CRC matched exactly there.
    bool legacy = false;
    for (int i = 0; i < store.Count(); ++i) {
        const Digest& d = store.Calculated(i);
        const bool palindrome = d.size == 4 && d.bytes[0] == d.bytes[3] && d.bytes[1] == d.bytes[2];
        if (store.CrcMatchOf(i) == CrcMatch::Standard && !palindrome) legacy = true;
    }
    Out("OK=" + ToStr(r.ok) + " BAD=" + ToStr(r.bad) + " MISSING=" + ToStr(r.missing) +
        " LEGACY=" + (legacy ? "True" : "False") + "\n");
    return 0;
}


// ------------------------------------------------------------------ fuzz (differential tests)
// Same input and output as the "fuzz" command of tests/probe_main.cpp (standard build).
static Str FromHexStr(StrView h) {
    Vec<u8> b;
    TryFromHex(h, b);
    return Str(reinterpret_cast<const char*>(b.Data()), b.Size());
}
static Str Hx(StrView s) { return ToHex(reinterpret_cast<const u8*>(s.Data()), s.Size()); }
static Str OptHx(const Opt<Str>& o) { return o ? Hx(*o) : Str("NULL"); }

static Str FuzzOne(const Str& op, const Vec<Str>& a) {
    static const Str empty;
    auto arg = [&](usize i) -> const Str& { return i < a.Size() ? a[i] : empty; };
    if (op == "trim") return Hx(text::Trim(arg(0))) + "\t" + Hx(text::TrimStart(arg(0))) + "\t" + Hx(text::TrimEnd(arg(0)));
    if (op == "fold") return Hx(text::FoldCase(arg(0)));
    if (op == "eqic") return Str(text::EqualsIgnoreCase(arg(0), arg(1)) ? "1" : "0") +
                             (text::StartsWithIgnoreCase(arg(0), arg(1)) ? "1" : "0") +
                             (text::EndsWithIgnoreCase(arg(0), arg(1)) ? "1" : "0") +
                             (text::ContainsIgnoreCase(arg(0), arg(1)) ? "1" : "0") + ToStr(text::CompareIgnoreCase(arg(0), arg(1)));
    if (op == "split") {
        Str r;
        for (const Str& x : text::Split(arg(0), arg(1), true)) r += Hx(x) + ",";
        r += "\t";
        for (const Str& x : text::Split(arg(0), arg(1), false)) r += Hx(x) + ",";
        return r;
    }
    if (op == "lines") {
        Str r;
        for (const Str& x : text::SplitLines(arg(0))) r += Hx(x) + ",";
        return r;
    }
    if (op == "match") {
        Str h1, n1, h2, n2;
        const bool a1 = MatchHashFirst(arg(0), h1, n1);
        const bool b1 = MatchNameFirst(arg(0), n2, h2);
        return Str(a1 ? "1" : "0") + Hx(h1) + "," + Hx(n1) + "\t" + (b1 ? "1" : "0") + Hx(n2) + "," + Hx(h2);
    }
    if (op == "parse") {
        const ParsedChecksumFile p = ParseChecksumText(arg(1), *ParseHashType(arg(0)));
        Str r;
        for (const ChecksumEntry& e : p.entries) r += ToHex(e.expectedHash) + ":" + Hx(e.fileName) + ",";
        return r + "\t" + Hx(p.comments);
    }
    if (op == "mask") return Str(FitsMask(arg(0), arg(1)) ? "1" : "0") + (IsFileAllowed(arg(0), arg(1), arg(2)) ? "1" : "0");
    if (op == "combine") return Hx(path::Combine(arg(0), arg(1)));
    if (op == "pathparts")
        return OptHx(path::GetDirectoryName(arg(0))) + "\t" + Hx(path::GetFileName(arg(0))) + "\t" + Hx(path::GetExtension(arg(0))) +
               "\t" + Hx(path::GetFileNameWithoutExtension(arg(0))) + "\t" + Hx(path::GetPathRoot(arg(0))) + "\t" +
               (path::IsPathRooted(arg(0)) ? "1" : "0") + ToStr(static_cast<i64>(path::GetRootLength(arg(0)))) + "\t" +
               Hx(path::SanitizeFileName(arg(0)));
    if (op == "rel") return OptHx(path::GetRelativePath(arg(0), arg(1))) + "\t" + Hx(path::GetFullPath(arg(0)));
    if (op == "common") return Hx(path::FindCommonBasePath(a));
    if (op == "decode") return Hx(text::DecodeTextFile(arg(0)));
    if (op == "int") {
        int v = 7;
        const bool ok = text::TryParseInt32(arg(0), v);
        return Str(ok ? "1" : "0") + ToStr(v);
    }
    if (op == "f1") return text::FormatF1(strtod(arg(0).CStr(), nullptr));
    if (op == "enum") {
        Str r;
        Opt<HashType> H = ParseHashType(arg(0));
        r += H ? ToString(*H) : "-";
        Opt<ProcessingMode> P = ParseProcessingMode(arg(0));
        r += Str(",") + (P ? ToString(*P) : "-");
        Opt<Crc32ByteOrder> C = ParseCrc32ByteOrder(arg(0));
        r += Str(",") + (C ? ToString(*C) : "-");
        return r;
    }
    return "?";
}

static int Fuzz() {
    Str input;
    char buf[65536];
    for (;;) {
        const ssize_t n = read(0, buf, sizeof buf);
        if (n <= 0) break;
        input.Append(StrView(buf, static_cast<usize>(n)));
    }
    Str out;
    usize start = 0;
    while (start < input.Size()) {
        usize nl = input.Find('\n', start);
        if (nl == kNpos) nl = input.Size();
        Str line(input.Sub(start, nl - start));
        start = nl + 1;
        if (!line.Empty() && line.Back() == '\r') line.PopBack();
        const Vec<Str> f = text::Split(line, "\t", false);
        Vec<Str> args;
        for (usize i = 1; i < f.Size(); ++i) args.Push(FromHexStr(f[i]));
        out += FuzzOne(f[0], args) + "\n";
        if (out.Size() > (1 << 20)) {
            Out(out);
            out.Clear();
        }
    }
    Out(out);
    return 0;
}

int main(int argc, char** argv) {
    Vec<Str> a;
    for (int i = 1; i < argc; ++i) a.Push(Str(argv[i]));
    if (a.Empty()) return 2;
    if (a[0] == "hash") return Hash(a);
    if (a[0] == "parse") return Parse(a);
    if (a[0] == "mask") return Mask();
    if (a[0] == "ini") return Ini(a);
    if (a[0] == "create") return Create(a, Crc32ByteOrder::Reversed);
    if (a[0] == "create-standard") return Create(a, Crc32ByteOrder::Standard);
    if (a[0] == "verify") return Verify(a);
    if (a[0] == "fuzz") return Fuzz();
    return 2;
}
