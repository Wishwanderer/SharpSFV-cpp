// SharpSFV (lean C++ rewrite) - a set of strings (replaces std::unordered_set<std::string>).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "Str.h"
#include "Vec.h"

namespace sfv {

// Open addressing with linear probing; FNV-1a hash. Insert-only, as the engine needs.
class StrSet {
public:
    // Adds s; returns false if it was already present.
    bool Insert(StrView s) {
        if ((count_ + 1) * 4 >= slots_.Size() * 3) Grow();
        const usize mask = slots_.Size() - 1;
        for (usize i = Hash(s) & mask;; i = (i + 1) & mask) {
            Slot& slot = slots_[i];
            if (!slot.used) {
                slot.used = true;
                slot.value = s;
                ++count_;
                return true;
            }
            if (slot.value == s) return false;
        }
    }
    usize Size() const { return count_; }

private:
    struct Slot {
        Str value;
        bool used = false;
    };
    static u64 Hash(StrView s) {
        u64 h = 1469598103934665603ULL;
        for (char c : s) h = (h ^ static_cast<u8>(c)) * 1099511628211ULL;
        return h;
    }
    void Grow() {
        Vec<Slot> old = Move(slots_);
        slots_ = Vec<Slot>();
        slots_.Resize(old.Empty() ? 64 : old.Size() * 2);
        count_ = 0;
        for (Slot& s : old)
            if (s.used) Reinsert(Move(s.value));
    }
    void Reinsert(Str&& v) {
        const usize mask = slots_.Size() - 1;
        for (usize i = Hash(v) & mask;; i = (i + 1) & mask) {
            if (!slots_[i].used) {
                slots_[i].used = true;
                slots_[i].value = Move(v);
                ++count_;
                return;
            }
        }
    }
    Vec<Slot> slots_;
    usize count_ = 0;
};

} // namespace sfv
