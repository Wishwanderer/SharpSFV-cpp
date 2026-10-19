// SharpSFV (C++ port) - probe tool for comparing the port against the C# reference oracle
// (scripts/compare_with_oracle.sh). Same commands and output format as the oracle.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
// Modified 2026-10-03: fuzz command for the lean build's differential tests.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "AppSettings.h"
#include "ChecksumParser.h"
#include "ChecksumWriter.h"
#include "Engine.h"
#include "FileFilter.h"
#include "FileSystem.h"
#include "Hasher.h"
#include "PathUtil.h"
#include "Text.h"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <iostream>
#include <string>
#include <vector>

using namespace sfv;

static void Out(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stdout); }

static int Hash(const std::vector<std::string>& a) {
    const HashType t = *ParseHashType(a[1]);
    auto h = Hasher::Create(t);
    std::vector<std::uint8_t> buf(512 * 1024);
    for (std::size_t i = 2; i < a.size(); ++i) {
        fs::FileReader r;
        std::string hex = "ERROR";
        if (r.Open(a[i])) {
            h->Reset();
            std::int64_t n;
            while ((n = r.Read(buf.data(), buf.size())) > 0) h->Update(buf.data(), static_cast<std::size_t>(n));
            Digest d;
            if (n == 0 && h->Final(d)) hex = FormatDigest(d, Crc32ByteOrder::Reversed); // .NET prints CRC-32 reversed
        }
        Out(hex + "\t" + a[i] + "\n");
    }
    return 0;
}

static int Parse(const std::vector<std::string>& a) {
    std::string err;
    auto p = ParseChecksumFile(a[2], *ParseHashType(a[1]), &err);
    if (!p) return 1;
    for (const auto& e : p->entries) Out(ToHex(e.expectedHash) + "|" + e.fileName + "|\n");
    std::string c = p->comments;
    std::string esc;
    for (std::size_t i = 0; i < c.size(); ++i) {
        if (c[i] == '\r' && i + 1 < c.size() && c[i + 1] == '\n') continue;
        if (c[i] == '\n') esc += "\\n";
        else esc += c[i];
    }
    Out("COMMENTS:" + esc + "\n");
    return 0;
}

static int Mask() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto p = text::Split(line, "\t", false);
        Out(IsFileAllowed(p[0], p[1], p[2]) ? "True\n" : "False\n");
    }
    return 0;
}

static int Ini(const std::vector<std::string>& a) {
    AppSettings s("");
    s.ParseText(text::DecodeTextFile(fs::ReadAllBytes(a[1]).value_or("")));
    auto B = [](bool b) { return b ? "1" : "0"; };
    std::string o;
    o += std::string("tt=") + B(s.showTimeTab) + " fp=" + B(s.showFilterPanel) + " ts=" + B(s.showThroughputStats) +
         " ab=" + B(s.showAdvancedBar) + " full=" + B(s.showFullPaths) + " hc=" + B(s.showHashCol) +
         " ehc=" + B(s.showExpectedHashCol) + " lock=" + B(s.lockColumns) + "\n";
    o += std::string("psm=") + ToString(s.pathStorageMode) + " pm=" + ToString(s.processingMode) +
         " cmt=" + B(s.enableChecksumComments) + " rec=" + B(s.scanRecursive) + " algo=" + ToString(s.defaultAlgo) + "\n";
    o += "prefix=[" + s.pathPrefix + "] inc=[" + s.includePattern + "] exc=[" + s.excludePattern + "] sig=[" +
         s.customSignature + "]\n";
    o += "w=" + std::to_string(s.windowWidth) + " h=" + std::to_string(s.windowHeight) +
         " x=" + std::to_string(s.hasCustomLocation ? s.windowX : 0) + " y=" + std::to_string(s.hasCustomLocation ? s.windowY : 0) +
         " custom=" + B(s.hasCustomLocation) + " split=" + std::to_string(s.splitterDistance) + "\n";
    o += "cols=";
    for (std::size_t i = 0; i < s.columnOrder.size(); ++i)
        o += (i ? "," : "") + s.columnOrder[i].first + ":" + std::to_string(s.columnOrder[i].second);
    Out(o + "\n");
    return 0;
}

// create <out> <algo> <base> <recursive> <advanced> <include> <exclude> <prefix> <comments> <absolute> <paths...>
// "create" writes CRC-32 in Reversed order (what the C# SharpSFV writes) so output can be
// compared byte-for-byte; "create-standard" writes Standard (QuickSFV-compatible) order.
static int Create(const std::vector<std::string>& a, Crc32ByteOrder order) {
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
    opt.isHdd = false; // parallel: output order must still match the enumeration order
    opt.applyFilters = s.showAdvancedBar;
    opt.includePattern = s.includePattern;
    opt.excludePattern = s.excludePattern;
    opt.recursive = a[4] == "1";

    FileStore store;
    ProgressThrottle throttle;
    Engine engine(store, throttle);
    const std::vector<std::string> paths(a.begin() + 11, a.end());
    engine.HashFiles(paths, a[3], opt, nullptr);
    std::string err;
    return WriteSaveAs(a[1], store, s, opt.algo, &err) ? 0 : 1;
}

static int Verify(const std::vector<std::string>& a) {
    const HashType algo = DetectAlgoFromExtension(a[1], *ParseHashType(a[2]));
    std::string err;
    auto parsed = ParseChecksumFile(a[1], algo, &err);
    if (!parsed) return 1;
    FileStore store;
    ProgressThrottle throttle;
    Engine engine(store, throttle);
    const auto order = PopulateVerificationStore(store, *parsed, path::GetDirectoryName(a[1]).value_or(""), false);
    HashRunOptions opt;
    opt.algo = algo;
    opt.isHdd = false;
    const RunResult r = engine.Verify(order, opt, nullptr);
    for (int i = 0; i < store.Count(); ++i) Out(std::string(ToString(store.Status(i))) + "|" + store.RelativePath(i) + "\n");
    // The C# app's "legacy" flag means: matched only after reversing its (Reversed-order)
    // digest, i.e. the file used Standard order. A palindromic CRC matched exactly there.
    bool legacy = false;
    for (int i = 0; i < store.Count(); ++i) {
        const Digest& d = store.Calculated(i);
        const bool palindrome = d.size == 4 && d.bytes[0] == d.bytes[3] && d.bytes[1] == d.bytes[2];
        if (store.CrcMatchOf(i) == CrcMatch::Standard && !palindrome) legacy = true;
    }
    Out("OK=" + std::to_string(r.ok) + " BAD=" + std::to_string(r.bad) + " MISSING=" + std::to_string(r.missing) +
        " LEGACY=" + (legacy ? "True" : "False") + "\n");
    return 0;
}


// ------------------------------------------------------------------ fuzz (differential tests)
// Reads lines "op<TAB>hex(arg1)<TAB>hex(arg2)..." and prints the hex-encoded results, so the
// lean build's probe (lean/tests/LeanProbe.cpp) can be compared with this one on generated
// inputs, byte for byte. Added 2026-10-03.
static std::string FromHexStr(const std::string& h) {
    std::string r;
    for (std::size_t i = 0; i + 1 < h.size(); i += 2) r.push_back(static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16)));
    return r;
}
static std::string Hx(std::string_view s) { return ToHex(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); }
static std::string OptHx(const std::optional<std::string>& o) { return o ? Hx(*o) : std::string("NULL"); }

static std::string FuzzOne(const std::string& op, const std::vector<std::string>& a) {
    auto arg = [&](std::size_t i) -> const std::string& {
        static const std::string empty;
        return i < a.size() ? a[i] : empty;
    };
    if (op == "trim") return Hx(text::Trim(arg(0))) + "\t" + Hx(text::TrimStart(arg(0))) + "\t" + Hx(text::TrimEnd(arg(0)));
    if (op == "fold") return Hx(text::FoldCase(arg(0)));
    if (op == "eqic") return std::string(text::EqualsIgnoreCase(arg(0), arg(1)) ? "1" : "0") +
                             (text::StartsWithIgnoreCase(arg(0), arg(1)) ? "1" : "0") +
                             (text::EndsWithIgnoreCase(arg(0), arg(1)) ? "1" : "0") +
                             (text::ContainsIgnoreCase(arg(0), arg(1)) ? "1" : "0") + std::to_string(text::CompareIgnoreCase(arg(0), arg(1)));
    if (op == "split") {
        std::string r;
        for (const auto& x : text::Split(arg(0), arg(1), true)) r += Hx(x) + ",";
        r += "\t";
        for (const auto& x : text::Split(arg(0), arg(1), false)) r += Hx(x) + ",";
        return r;
    }
    if (op == "lines") {
        std::string r;
        for (const auto& x : text::SplitLines(arg(0))) r += Hx(x) + ",";
        return r;
    }
    if (op == "match") {
        std::string h1, n1, h2, n2;
        const bool a1 = MatchHashFirst(arg(0), h1, n1);
        const bool b1 = MatchNameFirst(arg(0), n2, h2);
        return std::string(a1 ? "1" : "0") + Hx(h1) + "," + Hx(n1) + "\t" + (b1 ? "1" : "0") + Hx(n2) + "," + Hx(h2);
    }
    if (op == "parse") {
        const auto p = ParseChecksumText(arg(1), *ParseHashType(arg(0)));
        std::string r;
        for (const auto& e : p.entries) r += ToHex(e.expectedHash) + ":" + Hx(e.fileName) + ",";
        return r + "\t" + Hx(p.comments);
    }
    if (op == "mask") return std::string(FitsMask(arg(0), arg(1)) ? "1" : "0") + (IsFileAllowed(arg(0), arg(1), arg(2)) ? "1" : "0");
    if (op == "combine") return Hx(path::Combine(arg(0), arg(1)));
    if (op == "pathparts")
        return OptHx(path::GetDirectoryName(arg(0))) + "\t" + Hx(path::GetFileName(arg(0))) + "\t" + Hx(path::GetExtension(arg(0))) +
               "\t" + Hx(path::GetFileNameWithoutExtension(arg(0))) + "\t" + Hx(path::GetPathRoot(arg(0))) + "\t" +
               (path::IsPathRooted(arg(0)) ? "1" : "0") + std::to_string(path::GetRootLength(arg(0))) + "\t" +
               Hx(path::SanitizeFileName(arg(0)));
    if (op == "rel") return OptHx(path::GetRelativePath(arg(0), arg(1))) + "\t" + Hx(path::GetFullPath(arg(0)));
    if (op == "common") return Hx(path::FindCommonBasePath(a));
    if (op == "decode") return Hx(text::DecodeTextFile(arg(0)));
    if (op == "int") {
        int v = 7;
        const bool ok = text::TryParseInt32(arg(0), v);
        return std::string(ok ? "1" : "0") + std::to_string(v);
    }
    if (op == "f1") return text::FormatF1(std::strtod(arg(0).c_str(), nullptr));
    if (op == "enum") {
        std::string r;
        auto H = ParseHashType(arg(0));
        r += H ? ToString(*H) : "-";
        auto P = ParseProcessingMode(arg(0));
        r += std::string(",") + (P ? ToString(*P) : "-");
        auto C = ParseCrc32ByteOrder(arg(0));
        r += std::string(",") + (C ? ToString(*C) : "-");
        return r;
    }
    return "?";
}

static int Fuzz() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto f = text::Split(line, "\t", false);
        std::vector<std::string> args;
        for (std::size_t i = 1; i < f.size(); ++i) args.push_back(FromHexStr(f[i]));
        Out(FuzzOne(f[0], args) + "\n");
    }
    return 0;
}

int main(int argc, char** argv) {
    std::vector<std::string> a(argv + 1, argv + argc);
    if (a.empty()) return 2;
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
