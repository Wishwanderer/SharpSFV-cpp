// SharpSFV (C++ port) - path helpers.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "PathUtil.h"

#include "Text.h"

#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <climits>
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

bool IsDevice(std::string_view p) {
    if (p.size() < 4) return false;
    if (p[0] == '\\' && (p[1] == '\\' || p[1] == '?') && p[2] == '?' && p[3] == '\\') return true; // \\?\ and \??\ //
    return IsSep(p[0]) && IsSep(p[1]) && (p[2] == '.' || p[2] == '?') && IsSep(p[3]);
}

bool IsDeviceUnc(std::string_view p) {
    return p.size() >= 8 && IsDevice(p) && IsSep(p[7]) && p[4] == 'U' && p[5] == 'N' && p[6] == 'C';
}
#endif

std::string NormalizeSeparators(std::string_view p) {
    std::string r;
    r.reserve(p.size());
    std::size_t start = 0;
#ifdef _WIN32
    if (!p.empty() && IsSep(p[0])) {
        r.push_back(kSep);
        start = 1;
    }
#endif
    for (std::size_t i = start; i < p.size(); ++i) {
        char c = p[i];
        if (IsSep(c)) {
            if (i + 1 < p.size() && IsSep(p[i + 1])) continue;
            c = kSep;
        }
        r.push_back(c);
    }
    return r;
}

bool IsEffectivelyEmpty(std::string_view p) {
#ifdef _WIN32
    return std::all_of(p.begin(), p.end(), [](char c) { return c == ' '; });
#else
    return p.empty();
#endif
}

bool CharsEqual(char a, char b, bool ignoreCase) {
    if (a == b) return true;
    if (!ignoreCase) return false;
    auto up = [](char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; };
    return up(a) == up(b);
}

bool EndsInSep(std::string_view p) { return !p.empty() && IsSep(p.back()); }

std::size_t EqualStartingCharacterCount(std::string_view a, std::string_view b, bool ignoreCase) {
    std::size_t n = 0;
    while (n < a.size() && n < b.size() && CharsEqual(a[n], b[n], ignoreCase)) ++n;
    return n;
}

std::size_t GetCommonPathLength(std::string_view first, std::string_view second, bool ignoreCase) {
    std::size_t common = EqualStartingCharacterCount(first, second, ignoreCase);
    if (common == 0) return 0;
    if (common == first.size() && (common == second.size() || IsSep(second[common]))) return common;
    if (common == second.size() && IsSep(first[common])) return common;
    while (common > 0 && !IsSep(first[common - 1])) --common;
    return common;
}

} // namespace

bool IsPathRooted(std::string_view p) {
#ifdef _WIN32
    return (!p.empty() && IsSep(p[0])) || (p.size() >= 2 && IsDriveChar(p[0]) && p[1] == ':');
#else
    return !p.empty() && p[0] == '/';
#endif
}

std::size_t GetRootLength(std::string_view p) {
#ifdef _WIN32
    const std::size_t len = p.size();
    std::size_t i = 0;
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
    return (!p.empty() && p[0] == '/') ? 1 : 0;
#endif
}

std::string GetPathRoot(std::string_view p) {
    if (IsEffectivelyEmpty(p)) return {};
    return NormalizeSeparators(p.substr(0, GetRootLength(p)));
}

std::string Combine(std::string_view a, std::string_view b) {
    if (b.empty()) return std::string(a);
    if (a.empty()) return std::string(b);
    if (IsPathRooted(b)) return std::string(b);
    std::string r;
    r.reserve(a.size() + b.size() + 1);
    r.append(a);
    if (!IsSep(a.back()) && !IsSep(b.front())) r.push_back(kSep);
    r.append(b);
    return r;
}

std::string_view GetFileName(std::string_view p) {
    const std::size_t root = IsEffectivelyEmpty(p) ? 0 : GetRootLength(p);
    for (std::size_t i = p.size(); i-- > 0;) {
        if (i < root || IsSep(p[i])) return p.substr(i + 1);
    }
    return p;
}

std::optional<std::string> GetDirectoryName(std::string_view p) {
    if (IsEffectivelyEmpty(p)) return std::nullopt;
    const std::size_t root = GetRootLength(p);
    std::size_t end = p.size();
    if (end <= root) return std::nullopt;
    while (end > root && !IsSep(p[--end])) {
    }
    while (end > root && IsSep(p[end - 1])) --end;
    return NormalizeSeparators(p.substr(0, end));
}

std::string_view GetExtension(std::string_view p) {
    for (std::size_t i = p.size(); i-- > 0;) {
        const char c = p[i];
        if (c == '.') return i != p.size() - 1 ? p.substr(i) : std::string_view{};
        if (IsSep(c)) break;
    }
    return {};
}

std::string_view GetFileNameWithoutExtension(std::string_view p) {
    const std::string_view name = GetFileName(p);
    const std::size_t dot = name.rfind('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

std::string GetFullPath(std::string_view p) {
    if (p.empty() || p.find('\0') != std::string_view::npos) return {};
#ifdef _WIN32
    const std::wstring w = text::Widen(p);
    DWORD n = GetFullPathNameW(w.c_str(), 0, nullptr, nullptr);
    if (n == 0) return {};
    std::wstring buf(n, L'\0');
    n = GetFullPathNameW(w.c_str(), n, buf.data(), nullptr);
    if (n == 0) return {};
    buf.resize(n);
    return text::Narrow(buf);
#else
    std::string combined;
    if (p[0] == '/') {
        combined = std::string(p);
    } else {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof cwd)) return {};
        combined = std::string(cwd) + "/" + std::string(p);
    }
    const bool trailing = combined.size() > 1 && combined.back() == '/';
    std::vector<std::string> parts;
    for (const auto& seg : text::Split(combined, "/", true)) {
        if (seg == ".") continue;
        if (seg == "..") {
            if (!parts.empty()) parts.pop_back();
            continue;
        }
        parts.push_back(seg);
    }
    std::string r = "/";
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) r.push_back('/');
        r += parts[i];
    }
    if (trailing && r.size() > 1) r.push_back('/');
    return r;
#endif
}

std::optional<std::string> GetRelativePath(std::string_view relativeToIn, std::string_view pathIn) {
    if (relativeToIn.empty() || pathIn.empty()) return std::nullopt;
    const std::string relativeTo = GetFullPath(relativeToIn);
    const std::string p = GetFullPath(pathIn);
    if (relativeTo.empty() || p.empty()) return std::nullopt;

    // Different roots: return the full path.
    const std::size_t rootA = GetRootLength(relativeTo), rootB = GetRootLength(p);
    if (rootA != rootB ||
        EqualStartingCharacterCount(std::string_view(relativeTo).substr(0, rootA),
                                    std::string_view(p).substr(0, rootB), kIgnoreCase) != rootA)
        return p;

    std::size_t common = GetCommonPathLength(relativeTo, p, kIgnoreCase);
    if (common == 0) return p;

    std::size_t relLen = relativeTo.size();
    if (EndsInSep(relativeTo)) --relLen;
    const bool pathEndsInSep = EndsInSep(p);
    std::size_t pathLen = p.size();
    if (pathEndsInSep) --pathLen;

    if (relLen == pathLen && common >= relLen) return std::string(".");

    std::string sb;
    if (common < relLen) {
        sb += "..";
        for (std::size_t i = common + 1; i < relLen; ++i) {
            if (IsSep(relativeTo[i])) {
                sb.push_back(kSep);
                sb += "..";
            }
        }
    } else if (IsSep(p[common])) {
        ++common;
    }

    std::size_t diff = pathLen - common;
    if (pathEndsInSep) ++diff;
    if (diff > 0) {
        if (!sb.empty()) sb.push_back(kSep);
        sb.append(p, common, diff);
    }
    return sb;
}

std::string SanitizeFileName(std::string_view name) {
    std::string r(name);
    for (char& c : r) {
        const auto u = static_cast<unsigned char>(c);
#ifdef _WIN32
        if (u < 32 || c == '"' || c == '<' || c == '>' || c == '|' || c == ':' || c == '*' || c == '?' ||
            c == '\\' || c == '/')
            c = '_';
#else
        if (u == 0 || c == '/') c = '_';
#endif
    }
    return r;
}

std::string FindCommonBasePath(const std::vector<std::string>& paths) {
    if (paths.empty()) return {};
    if (paths.size() == 1) return paths[0];

    // Split on the primary separator only, removing empty entries (as the original does).
    auto splitParts = [](std::string_view s) { return text::Split(s, std::string_view(&kSep, 1), true); };

    // OrderBy(p => p.Length).First() — stable, so the first of the shortest wins.
    const std::string* shortest = &paths[0];
    for (const auto& p : paths)
        if (p.size() < shortest->size()) shortest = &p;

    std::vector<std::vector<std::string>> allParts;
    allParts.reserve(paths.size());
    for (const auto& p : paths) allParts.push_back(splitParts(p));
    const std::vector<std::string> shortestParts = splitParts(*shortest);

    const bool isRooted = IsPathRooted(paths[0]);

    // Fix over the original: the split above drops leading separators, which turned UNC
    // paths ("\\server\share") and POSIX paths ("/home") into relative ones. Preserve the
    // leading separator run when every path shares it.
    std::string lead;
    for (char c : paths[0]) {
        if (c != kSep) break;
        lead.push_back(c);
    }
    for (const auto& p : paths)
        if (!std::string_view(p).starts_with(lead)) lead.clear();

    std::string common;
    for (std::size_t i = 0; i < shortestParts.size(); ++i) {
        const std::string& seg = shortestParts[i];
        bool all = true;
        for (const auto& parts : allParts) {
            if (!(i < parts.size() && text::EqualsIgnoreCase(parts[i], seg))) {
                all = false;
                break;
            }
        }
        if (!all) break;

        if (i == 0 && isRooted && seg.find(':') != std::string::npos)
            common = seg + kSep;
        else if (i == 0 && !lead.empty())
            common = lead + seg;
        else
            common = Combine(common, seg);
    }
    while (!common.empty() && common.back() == kSep) common.pop_back();
    return common;
}

} // namespace sfv::path
