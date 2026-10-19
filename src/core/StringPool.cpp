// SharpSFV (C++ port) - string interning pool.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "StringPool.h"

namespace sfv {

const std::string& StringPool::Empty() {
    static const std::string empty;
    return empty;
}

const std::string* StringPool::GetOrAdd(std::string_view s) {
    if (s.empty()) return &Empty();
    std::lock_guard lock(mutex_);
    auto it = pool_.find(s);
    if (it != pool_.end()) return &*it;
    return &*pool_.emplace(s).first;
}

void StringPool::Clear() {
    std::lock_guard lock(mutex_);
    pool_.clear();
}

std::size_t StringPool::Size() const {
    std::lock_guard lock(mutex_);
    return pool_.size();
}

} // namespace sfv
