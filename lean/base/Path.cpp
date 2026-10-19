// SharpSFV (lean C++ rewrite) - System.IO.Path semantics (see Path.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Path.h"

#include "Text.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace sfv::path {

bool IsSep(char c) {
#ifdef _WIN32
    return c == '\\' || c == '/';
#else
    return c == '/';
#endif
}

namespace {

#ifdef _WIN32
bool IsDriveChar(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

bool IsDevice(StrView p) {
    if (p.Size() < 4) return false;
    if (p[0] == '\\' && (p[1] == '\\' || p[1] == '?') && p[2] == '?' && p[3] == '\\') return true;
    return IsSep(p[0]) && IsSep(p[1]) && (p[2] == '.' || p[2] == '?') && IsSep(p[3]);
}

bool IsDeviceUnc(StrView p) {
    return p.Size() >= 8 && IsDevice(p) && IsSep(p[7]) && p[4] == 'U' && p[5] == 'N' && p[6] == 'C';
}
#endif

Str NormalizeSeparators(StrView p) {
    Str r;
    r.Reserve(p.Size());
    usize start = 0;
#ifdef _WIN32
    if (!p.Empty() && IsSep(p[0])) {
        r.Append(kSep);
        start = 1;
    }
#endif
    for (usize i = start; i < p.Size(); ++i) {
        char c = p[i];
        if (IsSep(c)) {
            if (i + 1 < p.Size() && IsSep(p[i + 1])) continue;
            c = kSep;
        }
        r.Append(c);
    }
    return r;
}

bool IsEffectivelyEmpty(StrView p) {
#ifdef _WIN32
    for (char c : p)
        if (c != ' ') return false;
    return true;
#else
    return p.Empty();
#endif
}

bool CharsEqual(char a, char b, bool ignoreCase) {
    if (a == b) return true;
    if (!ignoreCase) return false;
    auto up = [](char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; };
    return up(a) == up(b);
}

bool EndsInSep(StrView p) { return !p.Empty() && IsSep(p.Back()); }

usize EqualStartingCharacterCount(StrView a, StrView b, bool ignoreCase) {
    usize n = 0;
    while (n < a.Size() && n < b.Size() && CharsEqual(a[n], b[n], ignoreCase)) ++n;
    return n;
}

usize GetCommonPathLength(StrView first, StrView second, bool ignoreCase) {
    usize common = EqualStartingCharacterCount(first, second, ignoreCase);
    if (common == 0) return 0;
    if (common == first.Size() && (common == second.Size() || IsSep(second[common]))) return common;
    if (common == second.Size() && IsSep(first[common])) return common;
    while (common > 0 && !IsSep(first[common - 1])) --common;
    return common;
}

} // namespace

bool IsPathRooted(StrView p) {
#ifdef _WIN32
    return (!p.Empty() && IsSep(p[0])) || (p.Size() >= 2 && IsDriveChar(p[0]) && p[1] == ':');
#else
    return !p.Empty() && p[0] == '/';
#endif
}

usize GetRootLength(StrView p) {
#ifdef _WIN32
    const usize len = p.Size();
    usize i = 0;
    const bool device = IsDevice(p);
    const bool deviceUnc = device && IsDeviceUnc(p);
    if ((!device || deviceUnc) && len > 0 && IsSep(p[0])) {
        if (deviceUnc || (len > 1 && IsSep(p[1]))) {
            i = deviceUnc ? 8 : 2;
            int n = 2;
            while (i < len && (!IsSep(p[i]) || --n > 0)) ++i;
        } else {
            i = 1;
        }
    } else if (device) {
        i = 4;
        while (i < len && !IsSep(p[i])) ++i;
        if (i < len && i > 4 && IsSep(p[i])) ++i;
    } else if (len >= 2 && p[1] == ':' && IsDriveChar(p[0])) {
        i = 2;
        if (len > 2 && IsSep(p[2])) ++i;
    }
    return i;
#else
    return (!p.Empty() && p[0] == '/') ? 1 : 0;
#endif
}

Str GetPathRoot(StrView p) {
    if (IsEffectivelyEmpty(p)) return {};
    return NormalizeSeparators(p.Sub(0, GetRootLength(p)));
}

Str Combine(StrView a, StrView b) {
    if (b.Empty()) return Str(a);
    if (a.Empty()) return Str(b);
    if (IsPathRooted(b)) return Str(b);
    Str r;
    r.Reserve(a.Size() + b.Size() + 1);
    r.Append(a);
    if (!IsSep(a.Back()) && !IsSep(b.Front())) r.Append(kSep);
    r.Append(b);
    return r;
}

StrView GetFileName(StrView p) {
    const usize root = IsEffectivelyEmpty(p) ? 0 : GetRootLength(p);
    for (usize i = p.Size(); i-- > 0;)
        if (i < root || IsSep(p[i])) return p.Sub(i + 1);
    return p;
}

Opt<Str> GetDirectoryName(StrView p) {
    if (IsEffectivelyEmpty(p)) return {};
    const usize root = GetRootLength(p);
    usize end = p.Size();
    if (end <= root) return {};
    while (end > root && !IsSep(p[--end])) {
    }
    while (end > root && IsSep(p[end - 1])) --end;
    return NormalizeSeparators(p.Sub(0, end));
}

StrView GetExtension(StrView p) {
    for (usize i = p.Size(); i-- > 0;) {
        const char c = p[i];
        if (c == '.') return i != p.Size() - 1 ? p.Sub(i) : StrView();
        if (IsSep(c)) break;
    }
    return {};
}

StrView GetFileNameWithoutExtension(StrView p) {
    const StrView name = GetFileName(p);
    const usize dot = name.RFind('.');
    return dot == kNpos ? name : name.Sub(0, dot);
}

Str GetFullPath(StrView p) {
    if (p.Empty() || p.Contains('\0')) return {};
#ifdef _WIN32
    const text::WStr w(p);
    DWORD n = GetFullPathNameW(w.C(), 0, nullptr, nullptr);
    if (n == 0) return {};
    text::WStr buf;
    buf.Resize(n);
    n = GetFullPathNameW(w.C(), n, buf.Data(), nullptr);
    if (n == 0) return {};
    return text::Narrow(buf.C(), n);
#else
    Str combined;
    if (p[0] == '/') {
        combined = p;
    } else {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof cwd)) return {};
        combined = Str(cwd) + "/" + p;
    }
    const bool trailing = combined.Size() > 1 && combined.Back() == '/';
    Vec<Str> parts;
    for (const Str& seg : text::Split(combined, "/", true)) {
        if (seg == ".") continue;
        if (seg == "..") {
            if (!parts.Empty()) parts.PopBack();
            continue;
        }
        parts.Push(seg);
    }
    Str r = "/";
    for (usize i = 0; i < parts.Size(); ++i) {
        if (i) r.Append('/');
        r.Append(parts[i]);
    }
    if (trailing && r.Size() > 1) r.Append('/');
    return r;
#endif
}

Opt<Str> GetRelativePath(StrView relativeToIn, StrView pathIn) {
    if (relativeToIn.Empty() || pathIn.Empty()) return {};
    const Str relativeTo = GetFullPath(relativeToIn);
    const Str p = GetFullPath(pathIn);
    if (relativeTo.Empty() || p.Empty()) return {};

    const usize rootA = GetRootLength(relativeTo), rootB = GetRootLength(p);
    if (rootA != rootB || EqualStartingCharacterCount(relativeTo.Sub(0, rootA), p.Sub(0, rootB), kIgnoreCase) != rootA)
        return p;

    usize common = GetCommonPathLength(relativeTo, p, kIgnoreCase);
    if (common == 0) return p;

    usize relLen = relativeTo.Size();
    if (EndsInSep(relativeTo)) --relLen;
    const bool pathEndsInSep = EndsInSep(p);
    usize pathLen = p.Size();
    if (pathEndsInSep) --pathLen;

    if (relLen == pathLen && common >= relLen) return Str(".");

    Str sb;
    if (common < relLen) {
        sb.Append("..");
        for (usize i = common + 1; i < relLen; ++i) {
            if (IsSep(relativeTo[i])) {
                sb.Append(kSep);
                sb.Append("..");
            }
        }
    } else if (IsSep(p[common])) {
        ++common;
    }

    usize diff = pathLen - common;
    if (pathEndsInSep) ++diff;
    if (diff > 0) {
        if (!sb.Empty()) sb.Append(kSep);
        sb.Append(p.Sub(common, diff));
    }
    return sb;
}

Str SanitizeFileName(StrView name) {
    Str r(name);
    for (usize i = 0; i < r.Size(); ++i) {
        const char c = r[i];
        const u8 u = static_cast<u8>(c);
#ifdef _WIN32
        if (u < 32 || c == '"' || c == '<' || c == '>' || c == '|' || c == ':' || c == '*' || c == '?' || c == '\\' ||
            c == '/')
            r[i] = '_';
#else
        if (u == 0 || c == '/') r[i] = '_';
#endif
    }
    return r;
}

Str FindCommonBasePath(const Vec<Str>& paths) {
    if (paths.Empty()) return {};
    if (paths.Size() == 1) return paths[0];

    // Split on the primary separator only, removing empty entries (as the original does).
    const char sepStr[2] = {kSep, 0};
    auto splitParts = [&](StrView s) { return text::Split(s, StrView(sepStr, 1), true); };

    // OrderBy(p => p.Length).First(): stable, so the first of the shortest wins.
    const Str* shortest = &paths[0];
    for (const Str& p : paths)
        if (p.Size() < shortest->Size()) shortest = &p;

    Vec<Vec<Str>> allParts;
    allParts.Reserve(paths.Size());
    for (const Str& p : paths) allParts.Push(splitParts(p));
    const Vec<Str> shortestParts = splitParts(*shortest);

    const bool isRooted = IsPathRooted(paths[0]);

    // Keep a leading separator run shared by every path (UNC "\\server", POSIX "/home").
    Str lead;
    for (char c : paths[0].View()) {
        if (c != kSep) break;
        lead.Append(c);
    }
    for (const Str& p : paths)
        if (!p.StartsWith(lead)) lead.Clear();

    Str common;
    for (usize i = 0; i < shortestParts.Size(); ++i) {
        const Str& seg = shortestParts[i];
        bool all = true;
        for (const Vec<Str>& parts : allParts) {
            if (!(i < parts.Size() && text::EqualsIgnoreCase(parts[i], seg))) {
                all = false;
                break;
            }
        }
        if (!all) break;

        if (i == 0 && isRooted && seg.Find(':') != kNpos)
            common = seg + kSep;
        else if (i == 0 && !lead.Empty())
            common = lead + seg;
        else
            common = Combine(common, seg);
    }
    while (!common.Empty() && common.Back() == kSep) common.PopBack();
    return common;
}

} // namespace sfv::path
