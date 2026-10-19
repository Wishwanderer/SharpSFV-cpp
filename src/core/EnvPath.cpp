// SharpSFV (C++ port) - editing a ';'-separated PATH list.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "EnvPath.h"

#include "Text.h"

#include <vector>

namespace sfv {

namespace {

std::string Normalize(std::string_view entry, const ExpandFn& expand) {
    std::string e(text::Trim(entry));
    if (e.size() >= 2 && e.front() == '"' && e.back() == '"') e = e.substr(1, e.size() - 2);
    if (expand) e = expand(e);
    while (e.size() > 3 && (e.back() == '\\' || e.back() == '/')) e.pop_back();
    return text::FoldCase(e);
}

std::vector<std::string> Entries(std::string_view list) { return text::Split(list, ";", false); }

} // namespace

bool PathListContains(std::string_view list, std::string_view dir, const ExpandFn& expand) {
    const std::string target = Normalize(dir, expand);
    for (const auto& e : Entries(list))
        if (!text::Trim(e).empty() && Normalize(e, expand) == target) return true;
    return false;
}

std::string PathListAdd(std::string_view list, std::string_view dir, const ExpandFn& expand) {
    if (PathListContains(list, dir, expand)) return std::string(list);
    std::string r(list);
    while (!r.empty() && r.back() == ';') r.pop_back();
    if (!r.empty()) r += ';';
    r += dir;
    return r;
}

std::string PathListRemove(std::string_view list, std::string_view dir, const ExpandFn& expand) {
    const std::string target = Normalize(dir, expand);
    std::string r;
    for (const auto& e : Entries(list)) {
        if (text::Trim(e).empty() || Normalize(e, expand) == target) continue;
        if (!r.empty()) r += ';';
        r += e;
    }
    return r;
}

} // namespace sfv
