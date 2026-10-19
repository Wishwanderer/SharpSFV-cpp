// SharpSFV (lean C++ rewrite) - display-list logic for the file list (sorting, filtering,
// duplicates). Port of src/core/ListModel.* and src/core/SortUtil.* (same orders, same
// filters, same fixes over the C# original; see those files).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "../base/Str.h"
#include "../base/Vec.h"
#include "../base/Sort.h"
#include "Store.h"

namespace sfv {

enum class ColumnKind { Name, Hash, Status, Expected, Time, JobName, RootPath, Progress, JobStatus, JobTime };
enum class SortOrder { None, Ascending, Descending };

// Tag saved in SharpSFV.ini's ColumnOrder ("Name:0,Hash:1,...") - same tags as the original.
const char* ColumnTag(ColumnKind k);

// Sorts store indices for display. SortOrder::None restores load order.
void SortIndices(Vec<int>& indices, const FileStore& store, ColumnKind column, SortOrder order);

struct FilterSpec {
    Str text;           // file-name substring, case-insensitive ("" = any)
    Str status = "All"; // All | Pending | OK | BAD | MISSING | Waiting...
    bool duplicates = false;
};

bool FilterActive(const FilterSpec& f);
bool MatchesFilter(const FileStore& store, int i, const FilterSpec& f); // text/status only

// The display list for a filter: with duplicates, grouped by hash (sorted by hash);
// otherwise store order, then sorted by (column, order).
Vec<int> FilterIndices(const FileStore& store, const FilterSpec& f, ColumnKind sortColumn, SortOrder order);

// Groups (store order, first appearance first) of items with identical calculated digests;
// only groups of two or more. Items without a hash are ignored.
Vec<Vec<int>> DuplicateGroups(const FileStore& store);

// After removing `removedSorted` (ascending) from the store: drops them from `display` and
// renumbers the rest.
void RemapAfterRemoval(Vec<int>& display, const Vec<int>& removedSorted);

} // namespace sfv
