// SharpSFV (lean C++ rewrite) - one shared stable sort for index lists (see Sort.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Sort.h"

namespace sfv {

// Bottom-up merge sort: stable, O(n log n), one scratch buffer.
void StableSortIndices(Vec<int>& v, IndexLess less) {
    const usize n = v.Size();
    if (n < 2) return;
    int* a = v.Data();
    int* tmp = static_cast<int*>(MemAlloc(n * sizeof(int)));
    // Short runs by insertion sort first (fewer passes for small lists).
    constexpr usize kRun = 16;
    for (usize lo = 0; lo < n; lo += kRun) {
        const usize hi = Min(lo + kRun, n);
        for (usize i = lo + 1; i < hi; ++i) {
            const int x = a[i];
            usize j = i;
            while (j > lo && less.fn(less.ctx, x, a[j - 1])) {
                a[j] = a[j - 1];
                --j;
            }
            a[j] = x;
        }
    }
    int* src = a;
    int* dst = tmp;
    for (usize width = kRun; width < n; width *= 2) {
        for (usize lo = 0; lo < n; lo += 2 * width) {
            const usize mid = Min(lo + width, n), hi = Min(lo + 2 * width, n);
            usize i = lo, j = mid, k = lo;
            while (i < mid && j < hi) dst[k++] = less.fn(less.ctx, src[j], src[i]) ? src[j++] : src[i++];
            while (i < mid) dst[k++] = src[i++];
            while (j < hi) dst[k++] = src[j++];
        }
        int* t = src;
        src = dst;
        dst = t;
    }
    if (src != a) memcpy(a, src, n * sizeof(int));
    MemFree(tmp);
}

} // namespace sfv
