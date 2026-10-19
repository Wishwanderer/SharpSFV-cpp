// SharpSFV (C++ port) - string interning pool.
// Ported from StringPool.cs. Thousands of files share identical base-directory strings;
// interning stores each unique string once. Unlike the C# version (a process-wide static),
// each FileStore owns its pool so clearing the store releases the strings too.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
// Modified 2026-10-03: native mutex (Threading.h).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "Threading.h"
#include <string>
#include <string_view>
#include <unordered_set>

namespace sfv {

class StringPool {
public:
    // Returns a pointer to the pooled copy of s. Pointers stay valid until Clear()
    // (unordered_set nodes never move). Lookup with a string_view does not allocate.
    const std::string* GetOrAdd(std::string_view s);
    void Clear();
    std::size_t Size() const;

    static const std::string& Empty();

private:
    struct Hash {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
    };
    struct Eq {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
    };

    mutable Mutex mutex_;
    std::unordered_set<std::string, Hash, Eq> pool_;
};

} // namespace sfv
