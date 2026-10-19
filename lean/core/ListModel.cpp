// SharpSFV (lean C++ rewrite) - display-list logic for the file list (see ListModel.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "ListModel.h"

#include "../base/Text.h"

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

// Lexicographic byte comparison; "no hash" sorts first, shorter first on a common prefix.
int CompareBytes(const u8* a, usize na, const u8* b, usize nb) {
    if (na == 0 && nb == 0) return 0;
    if (na == 0) return -1;
    if (nb == 0) return 1;
    const int c = memcmp(a, b, Min(na, nb));
    if (c != 0) return c < 0 ? -1 : 1;
    return na == nb ? 0 : (na < nb ? -1 : 1);
}

int Sign(i64 v) { return v < 0 ? -1 : (v > 0 ? 1 : 0); }

bool StatusMatches(ItemStatus s, StrView filter) {
    if (text::EqualsIgnoreCase(filter, "All") || filter.Empty()) return true;
    if (text::EqualsIgnoreCase(filter, "BAD")) return s == ItemStatus::Bad || s == ItemStatus::Error || s == ItemStatus::Missing;
    if (text::EqualsIgnoreCase(filter, "Waiting...")) return s == ItemStatus::Queued;
    return text::EqualsIgnoreCase(ToString(s), filter);
}

} // namespace

void SortIndices(Vec<int>& idx, const FileStore& store, ColumnKind column, SortOrder order) {
    Vec<Str> keys; // case-folded names, indexed by store index
    if (order != SortOrder::None && column == ColumnKind::Name) {
        keys.Resize(static_cast<usize>(store.Count()));
        for (const int i : idx) keys[static_cast<usize>(i)] = text::FoldCase(store.FileName(i));
    }
    auto compare = [&](int x, int y) -> int {
        if (order == SortOrder::None) return Sign(static_cast<i64>(store.OriginalIndex(x)) - store.OriginalIndex(y));
        int r = 0;
        switch (column) {
            case ColumnKind::Name: {
                const int c = keys[static_cast<usize>(x)].View().Compare(keys[static_cast<usize>(y)].View());
                r = c < 0 ? -1 : (c > 0 ? 1 : 0);
                break;
            }
            case ColumnKind::Hash: {
                const Digest& a = store.Calculated(x);
                const Digest& b = store.Calculated(y);
                r = CompareBytes(a.Data(), a.size, b.Data(), b.size);
                break;
            }
            case ColumnKind::Status: r = Sign(static_cast<int>(store.Status(x)) - static_cast<int>(store.Status(y))); break;
            case ColumnKind::Expected: {
                const Vec<u8>& a = store.Expected(x);
                const Vec<u8>& b = store.Expected(y);
                r = CompareBytes(a.Data(), a.Size(), b.Data(), b.Size());
                break;
            }
            case ColumnKind::Time: r = Sign(store.TimeMs(x) - store.TimeMs(y)); break;
            default: break;
        }
        return order == SortOrder::Descending ? -r : r;
    };
    StableSortIndices(idx, [&](int x, int y) { return compare(x, y) < 0; });
}

bool FilterActive(const FilterSpec& f) { return f.duplicates || !f.text.Empty() || !text::EqualsIgnoreCase(f.status, "All"); }

bool MatchesFilter(const FileStore& store, int i, const FilterSpec& f) {
    if (!f.text.Empty() && !text::ContainsIgnoreCase(store.FileName(i), f.text)) return false;
    return StatusMatches(store.Status(i), f.status);
}

Vec<Vec<int>> DuplicateGroups(const FileStore& store) {
    // Hashed items sorted (stably) by digest: equal digests become runs, each in store order.
    Vec<int> hashed;
    const int n = store.Count();
    for (int i = 0; i < n; ++i)
        if (!store.Calculated(i).Empty()) hashed.Push(i);
    auto cmp = [&](int a, int b) {
        const Digest& x = store.Calculated(a);
        const Digest& y = store.Calculated(b);
        return CompareBytes(x.Data(), x.size, y.Data(), y.size);
    };
    StableSortIndices(hashed, [&](int a, int b) { return cmp(a, b) < 0; });
    Vec<Vec<int>> runs;
    for (usize s = 0; s < hashed.Size();) {
        usize e = s + 1;
        while (e < hashed.Size() && cmp(hashed[s], hashed[e]) == 0) ++e;
        if (e - s >= 2) {
            Vec<int> g;
            for (usize k = s; k < e; ++k) g.Push(hashed[k]);
            runs.Push(Move(g));
        }
        s = e;
    }
    // Groups in order of first appearance (what the standard build's insertion order gives).
    Vec<int> order;
    for (usize g = 0; g < runs.Size(); ++g) order.Push(static_cast<int>(g));
    StableSortIndices(order, [&](int a, int b) { return runs[static_cast<usize>(a)][0] < runs[static_cast<usize>(b)][0]; });
    Vec<Vec<int>> out;
    for (const int g : order) out.Push(Move(runs[static_cast<usize>(g)]));
    return out;
}

Vec<int> FilterIndices(const FileStore& store, const FilterSpec& f, ColumnKind sortColumn, SortOrder order) {
    Vec<int> out;
    if (f.duplicates) {
        const Vec<Vec<int>> groups = DuplicateGroups(store);
        Vec<int> byHash; // group order, sorted by the shared hash
        for (usize g = 0; g < groups.Size(); ++g) byHash.Push(static_cast<int>(g));
        StableSortIndices(byHash, [&](int a, int b) {
            const Digest& x = store.Calculated(groups[static_cast<usize>(a)][0]);
            const Digest& y = store.Calculated(groups[static_cast<usize>(b)][0]);
            return CompareBytes(x.Data(), x.size, y.Data(), y.size) < 0;
        });
        for (const int g : byHash)
            for (const int i : groups[static_cast<usize>(g)]) out.Push(i);
        return out;
    }
    const int n = store.Count();
    out.Reserve(static_cast<usize>(n));
    const Str folded = text::FoldCase(f.text);
    const bool anyStatus = text::EqualsIgnoreCase(f.status, "All") || f.status.Empty();
    for (int i = 0; i < n; ++i) {
        if (!folded.Empty() && text::FoldCase(store.FileName(i)).Find(folded) == kNpos) continue;
        if (!anyStatus && !StatusMatches(store.Status(i), f.status)) continue;
        out.Push(i);
    }
    if (order != SortOrder::None) SortIndices(out, store, sortColumn, order);
    return out;
}

void RemapAfterRemoval(Vec<int>& display, const Vec<int>& removedSorted) {
    if (removedSorted.Empty()) return;
    Vec<int> out;
    out.Reserve(display.Size());
    for (const int i : display) {
        // lower_bound: how many removed indices are below i, and whether i itself was removed
        usize lo = 0, hi = removedSorted.Size();
        while (lo < hi) {
            const usize mid = (lo + hi) / 2;
            if (removedSorted[mid] < i) lo = mid + 1;
            else hi = mid;
        }
        if (lo < removedSorted.Size() && removedSorted[lo] == i) continue;
        out.Push(i - static_cast<int>(lo));
    }
    display = Move(out);
}

} // namespace sfv
