// SharpSFV (lean C++ rewrite) - include/exclude file-name patterns (see Filter.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Filter.h"

#include "../base/Text.h"

namespace sfv {

namespace {

Vec<u32> ToCodePoints(StrView s) {
    Vec<u32> r;
    r.Reserve(s.Size());
    usize i = 0;
    while (i < s.Size()) r.Push(text::DecodeUtf8(s, i));
    return r;
}

// The C# regex: "^" + Regex.Escape(mask).Replace("\*", ".*").Replace("\?", ".") + "$" with
// IgnoreCase: '?' is any character except '\n' and '*' any run of them.
bool WildcardMatch(StrView name, StrView mask) {
    const Vec<u32> s = ToCodePoints(text::FoldCase(name));
    const Vec<u32> p = ToCodePoints(text::FoldCase(mask));
    usize si = 0, pi = 0, starP = kNpos, starS = 0;
    while (si < s.Size()) {
        if (pi < p.Size() && p[pi] == '*') {
            starP = pi++;
            starS = si;
        } else if (pi < p.Size() && ((p[pi] == '?' && s[si] != '\n') || p[pi] == s[si])) {
            ++pi;
            ++si;
        } else if (starP != kNpos && s[starS] != '\n') {
            pi = starP + 1;
            si = ++starS;
        } else {
            return false;
        }
    }
    while (pi < p.Size() && p[pi] == '*') ++pi;
    return pi == p.Size();
}

} // namespace

bool FitsMask(StrView fileName, StrView mask) {
    if (mask.Empty() || mask == "*.*" || mask == "*") return true;
    // Fast path for a simple suffix such as "*.txt".
    if (mask.StartsWith('*') && !mask.Contains('?') && mask.Find('*', 1) == kNpos)
        return text::EndsWithIgnoreCase(fileName, mask.Sub(1));
    return WildcardMatch(fileName, mask);
}

bool IsFileAllowed(StrView fileName, StrView includePattern, StrView excludePattern) {
    if (!text::IsNullOrWhiteSpace(excludePattern)) {
        for (const Str& p : text::Split(excludePattern, ";,", true))
            if (FitsMask(fileName, text::Trim(p))) return false;
    }
    if (!text::IsNullOrWhiteSpace(includePattern)) {
        const Vec<Str> patterns = text::Split(includePattern, ";,", true);
        if (patterns.Empty()) return true;
        bool match = false;
        for (const Str& p : patterns) {
            if (FitsMask(fileName, text::Trim(p))) {
                match = true;
                break;
            }
        }
        if (!match) return false;
    }
    return true;
}

} // namespace sfv
