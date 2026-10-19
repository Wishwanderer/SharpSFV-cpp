// SharpSFV (C++ port) - display-list logic for the file list.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20; one shared stable sort.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "ListModel.h"

#include "Text.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace sfv {

const char* ColumnTag(ColumnKind k) {
    switch (k) {
        case ColumnKind::Name: return "Name";
        case ColumnKind::Hash: return "Hash";
        case ColumnKind::Status: return "Status";
        case ColumnKind::Expected: return "Expected";
        case ColumnKind::Time: return "Time";
        case ColumnKind::JobName: return "JobName";
        case ColumnKind::RootPath: return "RootPath";
        case ColumnKind::Progress: return "Progress";
        case ColumnKind::JobStatus: return "Status";
        case ColumnKind::JobTime: return "Time";
    }
    return "";
}

namespace {

// Lexicographic byte comparison; "no hash" sorts first, shorter first on a common prefix
// (CompareHashes in FileListSorter.cs).
int CompareBytes(const std::uint8_t* a, std::size_t na, const std::uint8_t* b, std::size_t nb) {
    if (na == 0 && nb == 0) return 0;
    if (na == 0) return -1;
    if (nb == 0) return 1;
    const int c = std::memcmp(a, b, std::min(na, nb));
    if (c != 0) return c < 0 ? -1 : 1;
    return na == nb ? 0 : (na < nb ? -1 : 1);
}

int Sign(long long v) { return v < 0 ? -1 : (v > 0 ? 1 : 0); }

} // namespace

void SortIndices(std::vector<int>& idx, const FileStore& store, ColumnKind column, SortOrder order) {
    // Summary rows always stay at the bottom; then the column comparison.
    std::vector<std::string> keys; // case-folded names, indexed by store index
    if (order != SortOrder::None && column == ColumnKind::Name) {
        keys.resize(static_cast<std::size_t>(store.Count()));
        for (int i : idx) keys[static_cast<std::size_t>(i)] = text::FoldCase(store.FileName(i));
    }
    auto compare = [&](int x, int y) -> int {
        const bool xs = store.IsSummaryRow(x), ys = store.IsSummaryRow(y);
        if (xs != ys) return xs ? 1 : -1;
        if (xs) return 0;
        if (order == SortOrder::None) return Sign(static_cast<long long>(store.OriginalIndex(x)) - store.OriginalIndex(y));
        int r = 0;
        switch (column) {
            case ColumnKind::Name: {
                const int c = keys[static_cast<std::size_t>(x)].compare(keys[static_cast<std::size_t>(y)]);
                r = c < 0 ? -1 : (c > 0 ? 1 : 0);
                break;
            }
            case ColumnKind::Hash: {
                const Digest& a = store.Calculated(x);
                const Digest& b = store.Calculated(y);
                r = CompareBytes(a.data(), a.size, b.data(), b.size);
                break;
            }
            case ColumnKind::Status:
                r = Sign(static_cast<int>(store.Status(x)) - static_cast<int>(store.Status(y)));
                break;
            case ColumnKind::Expected: {
                const auto& a = store.Expected(x);
                const auto& b = store.Expected(y);
                r = CompareBytes(a.data(), a.size(), b.data(), b.size());
                break;
            }
            case ColumnKind::Time:
                r = Sign(store.TimeMs(x) - store.TimeMs(y));
                break;
            default:
                break;
        }
        return order == SortOrder::Descending ? -r : r;
    };
    StableSortIndices(idx, [&](int x, int y) { return compare(x, y) < 0; });
}

bool FilterActive(const FilterSpec& f) {
    return f.duplicates || !f.text.empty() || !text::EqualsIgnoreCase(f.status, "All");
}

namespace {
bool StatusMatches(ItemStatus s, const std::string& filter) {
    if (text::EqualsIgnoreCase(filter, "All") || filter.empty()) return true;
    if (text::EqualsIgnoreCase(filter, "BAD")) return s == ItemStatus::Bad || s == ItemStatus::Error || s == ItemStatus::Missing;
    if (text::EqualsIgnoreCase(filter, "Waiting...")) return s == ItemStatus::Queued;
    return text::EqualsIgnoreCase(ToString(s), filter);
}
} // namespace

bool MatchesFilter(const FileStore& store, int i, const FilterSpec& f) {
    if (!f.text.empty() && !text::ContainsIgnoreCase(store.FileName(i), f.text)) return false;
    return StatusMatches(store.Status(i), f.status);
}

std::vector<std::vector<int>> DuplicateGroups(const FileStore& store) {
    std::unordered_map<std::string, std::size_t> groupOf;
    std::vector<std::vector<int>> groups;
    const int n = store.Count();
    for (int i = 0; i < n; ++i) {
        if (store.IsSummaryRow(i)) continue;
        const Digest& d = store.Calculated(i);
        if (d.empty()) continue;
        const std::string key(reinterpret_cast<const char*>(d.data()), d.size);
        auto [it, inserted] = groupOf.emplace(key, groups.size());
        if (inserted) groups.emplace_back();
        groups[it->second].push_back(i);
    }
    std::erase_if(groups, [](const std::vector<int>& g) { return g.size() < 2; });
    return groups;
}

std::vector<int> FilterIndices(const FileStore& store, const FilterSpec& f, ColumnKind sortColumn, SortOrder order) {
    std::vector<int> out;
    if (f.duplicates) {
        const auto groups = DuplicateGroups(store);
        std::vector<int> byHash(groups.size()); // group order, sorted by the shared hash
        for (std::size_t g = 0; g < groups.size(); ++g) byHash[g] = static_cast<int>(g);
        StableSortIndices(byHash, [&](int a, int b) {
            const Digest& x = store.Calculated(groups[static_cast<std::size_t>(a)].front());
            const Digest& y = store.Calculated(groups[static_cast<std::size_t>(b)].front());
            return CompareBytes(x.data(), x.size, y.data(), y.size) < 0;
        });
        for (int g : byHash) out.insert(out.end(), groups[static_cast<std::size_t>(g)].begin(), groups[static_cast<std::size_t>(g)].end());
        return out;
    }
    const int n = store.Count();
    out.reserve(static_cast<std::size_t>(n));
    const std::string folded = text::FoldCase(f.text);
    const bool anyStatus = text::EqualsIgnoreCase(f.status, "All") || f.status.empty();
    for (int i = 0; i < n; ++i) {
        if (!folded.empty() && text::FoldCase(store.FileName(i)).find(folded) == std::string::npos) continue;
        if (!anyStatus && !StatusMatches(store.Status(i), f.status)) continue;
        out.push_back(i);
    }
    if (order != SortOrder::None) SortIndices(out, store, sortColumn, order);
    return out;
}

void RemapAfterRemoval(std::vector<int>& display, const std::vector<int>& removedSorted) {
    if (removedSorted.empty()) return;
    std::vector<int> out;
    out.reserve(display.size());
    for (int i : display) {
        const auto it = std::lower_bound(removedSorted.begin(), removedSorted.end(), i);
        if (it != removedSorted.end() && *it == i) continue;
        out.push_back(i - static_cast<int>(it - removedSorted.begin()));
    }
    display.swap(out);
}

} // namespace sfv
