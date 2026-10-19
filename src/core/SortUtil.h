// SharpSFV (C++ port) - one shared stable sort for index lists.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the C++20 port.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include <vector>

namespace sfv {

// Stable sort of a list of indices with any "a before b" predicate. Every sort in the program
// goes through one compiled copy of std::stable_sort (each predicate type would otherwise get
// its own ~3 KB copy).
struct IndexLess {
    bool (*fn)(const void* ctx, int a, int b);
    const void* ctx;
    bool operator()(int a, int b) const { return fn(ctx, a, b); }
};
void StableSortIndices(std::vector<int>& indices, IndexLess less);
template <class Less>
void StableSortIndices(std::vector<int>& indices, const Less& less) {
    StableSortIndices(indices, IndexLess{[](const void* c, int a, int b) { return (*static_cast<const Less*>(c))(a, b); }, &less});
}

} // namespace sfv
