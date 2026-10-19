// SharpSFV (C++ port) - one shared stable sort for index lists (see SortUtil.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the C++20 port.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "SortUtil.h"

#include <algorithm>

namespace sfv {

void StableSortIndices(std::vector<int>& indices, IndexLess less) { std::stable_sort(indices.begin(), indices.end(), less); }

} // namespace sfv
