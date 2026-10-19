// SharpSFV (C++ port) - display-list logic for the file list (sorting, filtering, duplicates).
// Ported from Utils/FileListSorter.cs and ApplyFilter in Form1.UI.VirtualMode.cs. Kept free
// of any window code so it can be unit-tested; the GUI holds a std::vector<int> of store
// indices (the "display indices") and uses these functions to build it.
//
// Differences from the C# version (see README):
//   * Columns are identified by kind, not position. The original sorted by column *index*,
//     so with the Hash column hidden, clicking "Status" sorted by hash.
//   * Time sorts numerically (the original compared "100 ms" and "20 ms" as text).
//   * Sorting is stable, and names are case-folded once per sort instead of per comparison.
//   * Duplicates are grouped by digest; items without a hash are never "duplicates" (the
//     original grouped all hash-less items, e.g. unreadable files, together).
//   * The "Waiting..." status filter shows queued items (it matched nothing before).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "FileStore.h"
#include "SortUtil.h"

#include <string>
#include <vector>

namespace sfv {

enum class ColumnKind { Name, Hash, Status, Expected, Time, JobName, RootPath, Progress, JobStatus, JobTime };
enum class SortOrder { None, Ascending, Descending };

// Tag saved in SharpSFV.ini's ColumnOrder ("Name:0,Hash:1,...") - same tags as the original.
const char* ColumnTag(ColumnKind k);

// Sorts store indices for display. SortOrder::None restores load order.
void SortIndices(std::vector<int>& indices, const FileStore& store, ColumnKind column, SortOrder order);

struct FilterSpec {
    std::string text;           // file-name substring, case-insensitive ("" = any)
    std::string status = "All"; // All | Pending | OK | BAD | MISSING | Waiting...
    bool duplicates = false;    // show only files whose hash occurs more than once
};

bool FilterActive(const FilterSpec& f);

// True if store item i passes the text/status filter (duplicates are not considered).
bool MatchesFilter(const FileStore& store, int i, const FilterSpec& f);

// Builds the display list for a filter. With duplicates, the result is grouped by hash
// (sorted by hash); otherwise it is in store order, then sorted by (column, order).
std::vector<int> FilterIndices(const FileStore& store, const FilterSpec& f, ColumnKind sortColumn, SortOrder order);

// Groups of store indices (store order) whose calculated digests are identical; only
// groups with two or more members. Items without a calculated hash are ignored.
std::vector<std::vector<int>> DuplicateGroups(const FileStore& store);

// After removing `removedSorted` (ascending store indices) from the store: drops those
// entries from `display` and renumbers the rest to their new store indices.
void RemapAfterRemoval(std::vector<int>& display, const std::vector<int>& removedSorted);

} // namespace sfv
