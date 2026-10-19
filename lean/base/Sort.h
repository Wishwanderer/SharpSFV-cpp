// SharpSFV (lean C++ rewrite) - one shared stable sort for index lists.
// Port of src/core/SortUtil.*: every sort in the program goes through one compiled merge sort.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "Vec.h"

namespace sfv {

// Stable sort of a list of indices with an "a before b" predicate (one shared merge sort).
struct IndexLess {
    bool (*fn)(const void* ctx, int a, int b);
    const void* ctx;
};
void StableSortIndices(Vec<int>& indices, IndexLess less);
template <class Less> void StableSortIndices(Vec<int>& indices, const Less& less) {
    StableSortIndices(indices, IndexLess{[](const void* c, int a, int b) { return (*static_cast<const Less*>(c))(a, b); }, &less});
}

} // namespace sfv
