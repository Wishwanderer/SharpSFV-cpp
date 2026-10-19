// SharpSFV (lean C++ rewrite) - text helpers with .NET semantics (see Text.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Text.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace sfv::text {

u32 DecodeUtf8(StrView s, usize& i) {
    const u8 b0 = static_cast<u8>(s[i]);
    if (b0 < 0x80) {
        ++i;
        return b0;
    }
    int len = 0;
    u32 cp = 0, min = 0;
    if ((b0 & 0xE0) == 0xC0) len = 2, cp = b0 & 0x1F, min = 0x80;
    else if ((b0 & 0xF0) == 0xE0) len = 3, cp = b0 & 0x0F, min = 0x800;
    else if ((b0 & 0xF8) == 0xF0) len = 4, cp = b0 & 0x07, min = 0x10000;
    else {
        ++i;
        return 0xFFFD;
    }
    if (i + len > s.Size()) {
        ++i;
        return 0xFFFD;
    }
    for (int k = 1; k < len; ++k) {
        const u8 b = static_cast<u8>(s[i + k]);
        if ((b & 0xC0) != 0x80) {
            ++i;
            return 0xFFFD;
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        ++i;
        return 0xFFFD;
    }
    i += len;
    return cp;
}

void AppendUtf8(Str& out, u32 cp) {
    if (cp < 0x80) {
        out.Append(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.Append(static_cast<char>(0xC0 | (cp >> 6)));
        out.Append(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.Append(static_cast<char>(0xE0 | (cp >> 12)));
        out.Append(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.Append(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.Append(static_cast<char>(0xF0 | (cp >> 18)));
        out.Append(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.Append(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.Append(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool IsWhiteSpace(u32 c) {
    // U+0009..U+000D, U+0020, U+0085, U+00A0 and the Zs/Zl/Zp categories.
    if (c == 0x20 || (c >= 0x09 && c <= 0x0D)) return true;
    if (c < 0x85) return false;
    return c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
           c == 0x202F || c == 0x205F || c == 0x3000;
}

StrView TrimStart(StrView s) {
    usize i = 0;
    while (i < s.Size()) {
        usize next = i;
        if (!IsWhiteSpace(DecodeUtf8(s, next))) break;
        i = next;
    }
    return s.Sub(i);
}

StrView TrimEnd(StrView s) {
    usize i = 0, end = 0;
    while (i < s.Size()) {
        const u32 cp = DecodeUtf8(s, i);
        if (!IsWhiteSpace(cp)) end = i;
    }
    return s.Sub(0, end);
}

StrView Trim(StrView s) { return TrimEnd(TrimStart(s)); }

bool IsNullOrWhiteSpace(StrView s) { return TrimStart(s).Empty(); }

namespace {
bool IsAscii(StrView s) {
    for (char c : s)
        if (static_cast<u8>(c) >= 0x80) return false;
    return true;
}
char AsciiUpper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }
} // namespace

Str FoldCase(StrView s) {
    if (IsAscii(s)) {
        Str r(s);
        for (usize i = 0; i < r.Size(); ++i) r[i] = AsciiUpper(r[i]);
        return r;
    }
#ifdef _WIN32
    WStr w(s);
    if (w.Size() > 0)
        LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, w.Data(), static_cast<int>(w.Size()), w.Data(),
                      static_cast<int>(w.Size()), nullptr, nullptr, 0);
    return Narrow(w.C(), w.Size());
#else
    Str r(s);
    for (usize i = 0; i < r.Size(); ++i) r[i] = AsciiUpper(r[i]);
    return r;
#endif
}

int CompareIgnoreCase(StrView a, StrView b) {
    if (IsAscii(a) && IsAscii(b)) {
        const usize n = Min(a.Size(), b.Size());
        for (usize i = 0; i < n; ++i) {
            const u8 ca = static_cast<u8>(AsciiUpper(a[i]));
            const u8 cb = static_cast<u8>(AsciiUpper(b[i]));
            if (ca != cb) return ca < cb ? -1 : 1;
        }
        return a.Size() == b.Size() ? 0 : (a.Size() < b.Size() ? -1 : 1);
    }
    return FoldCase(a).View().Compare(FoldCase(b).View());
}

bool EqualsIgnoreCase(StrView a, StrView b) {
    if (IsAscii(a) && IsAscii(b)) {
        if (a.Size() != b.Size()) return false;
        for (usize i = 0; i < a.Size(); ++i)
            if (AsciiUpper(a[i]) != AsciiUpper(b[i])) return false;
        return true;
    }
    return FoldCase(a) == FoldCase(b);
}

bool StartsWithIgnoreCase(StrView s, StrView prefix) {
    if (IsAscii(prefix) && IsAscii(s.Sub(0, Min(s.Size(), prefix.Size()))))
        return s.Size() >= prefix.Size() && EqualsIgnoreCase(s.Sub(0, prefix.Size()), prefix);
    return FoldCase(s).StartsWith(FoldCase(prefix));
}

bool EndsWithIgnoreCase(StrView s, StrView suffix) {
    if (IsAscii(suffix) && s.Size() >= suffix.Size() && IsAscii(s.Sub(s.Size() - suffix.Size())))
        return EqualsIgnoreCase(s.Sub(s.Size() - suffix.Size()), suffix);
    return FoldCase(s).EndsWith(FoldCase(suffix));
}

bool ContainsIgnoreCase(StrView haystack, StrView needle) {
    if (needle.Empty()) return true;
    return FoldCase(haystack).Find(FoldCase(needle)) != kNpos;
}

Vec<Str> Split(StrView s, StrView separators, bool removeEmpty) {
    Vec<Str> parts;
    usize start = 0;
    for (usize i = 0; i <= s.Size(); ++i) {
        if (i == s.Size() || separators.Contains(s[i])) {
            if (!removeEmpty || i > start) parts.Emplace(s.Sub(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

Vec<Str> SplitLines(StrView s) {
    Vec<Str> lines;
    usize start = 0, i = 0;
    while (i < s.Size()) {
        const char c = s[i];
        if (c == '\r' || c == '\n') {
            lines.Emplace(s.Sub(start, i - start));
            i += (c == '\r' && i + 1 < s.Size() && s[i + 1] == '\n') ? 2 : 1;
            start = i;
        } else {
            ++i;
        }
    }
    if (start < s.Size()) lines.Emplace(s.Sub(start));
    return lines;
}

namespace {
void AppendUtf16(Str& out, StrView bytes, bool bigEndian) {
    auto unit = [&](usize i) -> u32 {
        const u8 a = static_cast<u8>(bytes[i]);
        const u8 b = static_cast<u8>(bytes[i + 1]);
        return bigEndian ? (static_cast<u32>(a) << 8) | b : (static_cast<u32>(b) << 8) | a;
    };
    for (usize i = 0; i + 1 < bytes.Size(); i += 2) {
        u32 cp = unit(i);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < bytes.Size()) {
            const u32 lo = unit(i + 2);
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

bool StartsWithBytes(StrView b, const u8* bom, usize n) {
    if (b.Size() < n) return false;
    for (usize i = 0; i < n; ++i)
        if (static_cast<u8>(b[i]) != bom[i]) return false;
    return true;
}
} // namespace

Str DecodeTextFile(StrView b) {
    static const u8 kUtf32Le[] = {0xFF, 0xFE, 0x00, 0x00}, kUtf16Le[] = {0xFF, 0xFE}, kUtf16Be[] = {0xFE, 0xFF},
                    kUtf8[] = {0xEF, 0xBB, 0xBF};
    Str out;
    if (StartsWithBytes(b, kUtf32Le, 4)) {
        for (usize i = 4; i + 3 < b.Size(); i += 4) {
            u32 cp = static_cast<u8>(b[i]) | (static_cast<u32>(static_cast<u8>(b[i + 1])) << 8) |
                     (static_cast<u32>(static_cast<u8>(b[i + 2])) << 16) |
                     (static_cast<u32>(static_cast<u8>(b[i + 3])) << 24);
            if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
            AppendUtf8(out, cp);
        }
        return out;
    }
    if (StartsWithBytes(b, kUtf16Le, 2)) {
        AppendUtf16(out, b.Sub(2), false);
        return out;
    }
    if (StartsWithBytes(b, kUtf16Be, 2)) {
        AppendUtf16(out, b.Sub(2), true);
        return out;
    }
    if (StartsWithBytes(b, kUtf8, 3)) b = b.Sub(3);

    // UTF-8: replace invalid sequences with U+FFFD like the .NET decoder.
    out.Reserve(b.Size());
    usize i = 0;
    while (i < b.Size()) {
        const usize start = i;
        const u32 cp = DecodeUtf8(b, i);
        if (cp == 0xFFFD && !(i - start == 3 && b.Sub(start, 3) == StrView("\xEF\xBF\xBD")))
            AppendUtf8(out, 0xFFFD);
        else
            out.Append(b.Sub(start, i - start));
    }
    return out;
}

bool TryParseInt32(StrView s, int& v) {
    v = 0;
    auto isNumWs = [](char c) { return c == ' ' || (c >= 0x09 && c <= 0x0D); };
    usize i = 0, n = s.Size();
    while (i < n && isNumWs(s[i])) ++i;
    while (n > i && (isNumWs(s[n - 1]) || s[n - 1] == '\0')) --n;
    bool neg = false;
    if (i < n && (s[i] == '+' || s[i] == '-')) {
        neg = s[i] == '-';
        ++i;
    }
    if (i >= n) return false;
    i64 acc = 0;
    for (; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        acc = acc * 10 + (s[i] - '0');
        if (acc > 2147483648LL) return false;
    }
    if (neg) acc = -acc;
    if (acc < -2147483648LL || acc > 2147483647LL) return false;
    v = static_cast<int>(acc);
    return true;
}

Str FormatF1(double v) {
    if (v != v) return "NaN";
    const bool neg = v < 0;
    const double a = neg ? -v : v;
    if (a >= 9.0e17) return neg ? "-\xE2\x88\x9E" : "\xE2\x88\x9E";
    const u64 tenths = static_cast<u64>(a * 10.0 + 0.5);
    Str out;
    if (neg && tenths != 0) out.Append('-');
    out.AppendInt(static_cast<i64>(tenths / 10)).Append('.').Append(static_cast<char>('0' + tenths % 10));
    return out;
}

#ifdef _WIN32
WStr::WStr(StrView s) {
    const int n = s.Empty() ? 0 : MultiByteToWideChar(CP_UTF8, 0, s.Data(), static_cast<int>(s.Size()), nullptr, 0);
    v_.Resize(static_cast<usize>(n) + 1);
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.Data(), static_cast<int>(s.Size()), v_.Data(), n);
    v_[static_cast<usize>(n)] = 0;
}
void WStr::Append(const wchar_t* z) {
    v_.PopBack();
    while (*z) v_.Push(*z++);
    v_.Push(0);
}
void WStr::Append(wchar_t c) {
    v_.Back() = c;
    v_.Push(0);
}
void WStr::Resize(usize n) {
    v_.Resize(n + 1);
    v_[n] = 0;
}

Str Narrow(const wchar_t* s, usize n) {
    if (n == 0) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
    Str r;
    r.Resize(static_cast<usize>(len));
    WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(n), r.MutableData(), len, nullptr, nullptr);
    return r;
}
#endif

} // namespace sfv::text
