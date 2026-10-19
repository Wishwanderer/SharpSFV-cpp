// SharpSFV (C++ port) - include/exclude filename filters.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "FileFilter.h"

#include "Text.h"

#include <vector>

namespace sfv {

namespace {

std::u32string ToCodePoints(std::string_view s) {
    std::u32string r;
    r.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) r.push_back(text::DecodeUtf8(s, i));
    return r;
}

// Equivalent of the original's Regex fallback: "^" + Escape(mask) with \* -> .* and
// \? -> . + "$", RegexOptions.IgnoreCase. Iterative wildcard match with backtracking.
bool WildcardMatch(std::string_view name, std::string_view mask) {
    const std::u32string s = ToCodePoints(text::FoldCase(name));
    const std::u32string p = ToCodePoints(text::FoldCase(mask));
    std::size_t si = 0, pi = 0, starP = std::u32string::npos, starS = 0;
    while (si < s.size()) {
        if (pi < p.size() && p[pi] == U'*') {
            starP = pi++;
            starS = si;
        } else if (pi < p.size() && ((p[pi] == U'?' && s[si] != U'\n') || p[pi] == s[si])) {
            ++pi;
            ++si;
        } else if (starP != std::u32string::npos && s[starS] != U'\n') {
            pi = starP + 1;
            si = ++starS;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == U'*') ++pi;
    return pi == p.size();
}

} // namespace

bool FitsMask(std::string_view fileName, std::string_view mask) {
    if (mask.empty() || mask == "*.*" || mask == "*") return true;

    // Fast path for a simple suffix such as "*.txt".
    if (mask.starts_with('*') && mask.find('?') == std::string_view::npos &&
        mask.find('*', 1) == std::string_view::npos) {
        return text::EndsWithIgnoreCase(fileName, mask.substr(1));
    }
    return WildcardMatch(fileName, mask);
}

bool IsFileAllowed(std::string_view fileName, std::string_view includePattern, std::string_view excludePattern) {
    if (!text::IsNullOrWhiteSpace(excludePattern)) {
        for (const auto& p : text::Split(excludePattern, ";,", true))
            if (FitsMask(fileName, text::Trim(p))) return false;
    }

    if (!text::IsNullOrWhiteSpace(includePattern)) {
        const auto patterns = text::Split(includePattern, ";,", true);
        if (patterns.empty()) return true;
        bool match = false;
        for (const auto& p : patterns) {
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
