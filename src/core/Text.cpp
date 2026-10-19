// SharpSFV (C++ port) - text helpers.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
// Modified 2026-10-03: FormatF1 without printf (smaller executables).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Text.h"

#include <algorithm>
#include <cstdio>
#include <limits>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sfv::text {

char32_t DecodeUtf8(std::string_view s, std::size_t& i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80) { ++i; return b0; }

    int len = 0;
    char32_t cp = 0, min = 0;
    if ((b0 & 0xE0) == 0xC0) { len = 2; cp = b0 & 0x1F; min = 0x80; }
    else if ((b0 & 0xF0) == 0xE0) { len = 3; cp = b0 & 0x0F; min = 0x800; }
    else if ((b0 & 0xF8) == 0xF0) { len = 4; cp = b0 & 0x07; min = 0x10000; }
    else { ++i; return 0xFFFD; }

    if (i + len > s.size()) { ++i; return 0xFFFD; }
    for (int k = 1; k < len; ++k) {
        const auto b = static_cast<unsigned char>(s[i + k]);
        if ((b & 0xC0) != 0x80) { ++i; return 0xFFFD; }
        cp = (cp << 6) | (b & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { ++i; return 0xFFFD; }
    i += len;
    return cp;
}

void AppendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool IsWhiteSpace(char32_t c) {
    // U+0009..U+000D, U+0020, U+0085, U+00A0 and the Zs/Zl/Zp categories.
    if (c == 0x20 || (c >= 0x09 && c <= 0x0D)) return true;
    if (c < 0x85) return false;
    return c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

std::string_view TrimStart(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        std::size_t next = i;
        if (!IsWhiteSpace(DecodeUtf8(s, next))) break;
        i = next;
    }
    return s.substr(i);
}

std::string_view TrimEnd(std::string_view s) {
    // Walk forward remembering where the trailing whitespace run starts.
    std::size_t i = 0, end = 0;
    while (i < s.size()) {
        const char32_t cp = DecodeUtf8(s, i);
        if (!IsWhiteSpace(cp)) end = i;
    }
    return s.substr(0, end);
}

std::string_view Trim(std::string_view s) { return TrimEnd(TrimStart(s)); }

bool IsNullOrWhiteSpace(std::string_view s) { return TrimStart(s).empty(); }

namespace {
bool IsAscii(std::string_view s) {
    for (unsigned char c : s)
        if (c >= 0x80) return false;
    return true;
}
char AsciiUpper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }
} // namespace

std::string FoldCase(std::string_view s) {
    if (IsAscii(s)) {
        std::string r(s);
        for (char& c : r) c = AsciiUpper(c);
        return r;
    }
#ifdef _WIN32
    std::wstring w = Widen(s);
    if (!w.empty()) {
        LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, w.data(), static_cast<int>(w.size()),
                      w.data(), static_cast<int>(w.size()), nullptr, nullptr, 0);
    }
    return Narrow(w);
#else
    std::string r(s);
    for (char& c : r) c = AsciiUpper(c);
    return r;
#endif
}

int CompareIgnoreCase(std::string_view a, std::string_view b) {
    if (IsAscii(a) && IsAscii(b)) {
        const std::size_t n = std::min(a.size(), b.size());
        for (std::size_t i = 0; i < n; ++i) {
            const auto ca = static_cast<unsigned char>(AsciiUpper(a[i]));
            const auto cb = static_cast<unsigned char>(AsciiUpper(b[i]));
            if (ca != cb) return ca < cb ? -1 : 1;
        }
        return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
    }
    const int c = FoldCase(a).compare(FoldCase(b));
    return c == 0 ? 0 : (c < 0 ? -1 : 1);
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
    if (IsAscii(a) && IsAscii(b)) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (AsciiUpper(a[i]) != AsciiUpper(b[i])) return false;
        return true;
    }
    return FoldCase(a) == FoldCase(b);
}

bool StartsWithIgnoreCase(std::string_view s, std::string_view prefix) {
    if (IsAscii(prefix) && IsAscii(s.substr(0, std::min(s.size(), prefix.size())))) {
        return s.size() >= prefix.size() && EqualsIgnoreCase(s.substr(0, prefix.size()), prefix);
    }
    return FoldCase(s).starts_with(FoldCase(prefix));
}

bool EndsWithIgnoreCase(std::string_view s, std::string_view suffix) {
    if (IsAscii(suffix) && s.size() >= suffix.size() && IsAscii(s.substr(s.size() - suffix.size()))) {
        return EqualsIgnoreCase(s.substr(s.size() - suffix.size()), suffix);
    }
    return FoldCase(s).ends_with(FoldCase(suffix));
}

bool ContainsIgnoreCase(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    return FoldCase(haystack).find(FoldCase(needle)) != std::string::npos;
}

std::vector<std::string> Split(std::string_view s, std::string_view separators, bool removeEmpty) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || separators.find(s[i]) != std::string_view::npos) {
            if (!removeEmpty || i > start) parts.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

std::vector<std::string> SplitLines(std::string_view s) {
    std::vector<std::string> lines;
    std::size_t start = 0, i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\r' || c == '\n') {
            lines.emplace_back(s.substr(start, i - start));
            i += (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') ? 2 : 1;
            start = i;
        } else {
            ++i;
        }
    }
    if (start < s.size()) lines.emplace_back(s.substr(start));
    return lines;
}

namespace {
void AppendUtf16(std::string& out, std::string_view bytes, bool bigEndian) {
    auto unit = [&](std::size_t i) -> char16_t {
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = static_cast<unsigned char>(bytes[i + 1]);
        return static_cast<char16_t>(bigEndian ? (a << 8) | b : (b << 8) | a);
    };
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        char32_t cp = unit(i);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < bytes.size()) {
            const char16_t lo = unit(i + 2);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            } else {
                cp = 0xFFFD;
            }
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }
        AppendUtf8(out, cp);
    }
}
} // namespace

std::string DecodeTextFile(std::string_view b) {
    auto starts = [&](std::initializer_list<unsigned char> bom) {
        if (b.size() < bom.size()) return false;
        std::size_t i = 0;
        for (unsigned char c : bom)
            if (static_cast<unsigned char>(b[i++]) != c) return false;
        return true;
    };

    std::string out;
    if (starts({0xFF, 0xFE, 0x00, 0x00})) {
        for (std::size_t i = 4; i + 3 < b.size(); i += 4) {
            char32_t cp = static_cast<unsigned char>(b[i]) | (static_cast<unsigned char>(b[i + 1]) << 8) |
                          (static_cast<unsigned char>(b[i + 2]) << 16) |
                          (static_cast<char32_t>(static_cast<unsigned char>(b[i + 3])) << 24);
            if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
            AppendUtf8(out, cp);
        }
        return out;
    }
    if (starts({0xFF, 0xFE})) { AppendUtf16(out, b.substr(2), false); return out; }
    if (starts({0xFE, 0xFF})) { AppendUtf16(out, b.substr(2), true); return out; }
    if (starts({0xEF, 0xBB, 0xBF})) b.remove_prefix(3);

    // UTF-8: replace invalid sequences with U+FFFD like the .NET decoder.
    out.reserve(b.size());
    std::size_t i = 0;
    while (i < b.size()) {
        const std::size_t start = i;
        const char32_t cp = DecodeUtf8(b, i);
        if (cp == 0xFFFD && !(i - start == 3 && b.substr(start, 3) == "\xEF\xBF\xBD"))
            AppendUtf8(out, 0xFFFD);
        else
            out.append(b.substr(start, i - start));
    }
    return out;
}

bool TryParseInt32(std::string_view s, int& v) {
    v = 0;
    auto isNumWs = [](char c) { return c == ' ' || (c >= 0x09 && c <= 0x0D); };
    std::size_t i = 0, n = s.size();
    while (i < n && isNumWs(s[i])) ++i;
    while (n > i && (isNumWs(s[n - 1]) || s[n - 1] == '\0')) --n;
    bool neg = false;
    if (i < n && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; ++i; }
    if (i >= n) return false;
    long long acc = 0;
    for (; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        acc = acc * 10 + (s[i] - '0');
        if (acc > static_cast<long long>(std::numeric_limits<int>::max()) + 1) return false;
    }
    if (neg) acc = -acc;
    if (acc < std::numeric_limits<int>::min() || acc > std::numeric_limits<int>::max()) return false;
    v = static_cast<int>(acc);
    return true;
}

std::string FormatF1(double v) {
    // One decimal, rounding half away from zero like .NET's "F1". Hand-written instead of
    // snprintf("%.1f"), which pulls MinGW's ~25 KB printf/dtoa engine into the executables.
    if (v != v) return "NaN";
    const bool neg = v < 0;
    const double a = neg ? -v : v;
    if (a >= 9.0e17) return neg ? "-\xE2\x88\x9E" : "\xE2\x88\x9E"; // beyond what the UI ever shows
    const auto tenths = static_cast<unsigned long long>(a * 10.0 + 0.5);
    std::string out = std::to_string(tenths / 10) + "." + static_cast<char>('0' + tenths % 10);
    return (neg && tenths != 0) ? "-" + out : out;
}

#ifdef _WIN32
std::wstring Widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string Narrow(std::wstring_view s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string r(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), n, nullptr, nullptr);
    return r;
}
#endif

} // namespace sfv::text
