// SharpSFV (lean C++ rewrite) - growable array (replaces std::vector).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "Base.h"

namespace sfv {

template <class T> class Vec {
public:
    Vec() = default;
    Vec(const Vec& o) { CopyFrom(o); }
    Vec(Vec&& o) noexcept : data_(o.data_), size_(o.size_), cap_(o.cap_) { o.data_ = nullptr, o.size_ = o.cap_ = 0; }
    Vec& operator=(const Vec& o) {
        if (this != &o) {
            Clear();
            CopyFrom(o);
        }
        return *this;
    }
    Vec& operator=(Vec&& o) noexcept {
        if (this != &o) {
            Destroy();
            data_ = o.data_, size_ = o.size_, cap_ = o.cap_;
            o.data_ = nullptr, o.size_ = o.cap_ = 0;
        }
        return *this;
    }
    ~Vec() { Destroy(); }

    usize Size() const { return size_; }
    bool Empty() const { return size_ == 0; }
    T* Data() { return data_; }
    const T* Data() const { return data_; }
    T& operator[](usize i) { return data_[i]; }
    const T& operator[](usize i) const { return data_[i]; }
    T& Back() { return data_[size_ - 1]; }
    const T& Back() const { return data_[size_ - 1]; }
    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }

    void Reserve(usize n) {
        if (n <= cap_) return;
        T* nd = static_cast<T*>(MemAlloc(n * sizeof(T)));
        for (usize i = 0; i < size_; ++i) {
            new (nd + i) T(Move(data_[i]));
            data_[i].~T();
        }
        MemFree(data_);
        data_ = nd;
        cap_ = n;
    }
    template <class... A> T& Emplace(A&&... a) {
        if (size_ == cap_) Reserve(cap_ < 8 ? 8 : cap_ * 2);
        T* p = new (data_ + size_) T(Forward<A>(a)...);
        ++size_;
        return *p;
    }
    void Push(const T& v) { Emplace(v); }
    void Push(T&& v) { Emplace(Move(v)); }
    void PopBack() { data_[--size_].~T(); }
    void Resize(usize n) {
        while (size_ > n) PopBack();
        Reserve(n);
        while (size_ < n) Emplace();
    }
    void Clear() {
        while (size_ > 0) PopBack();
    }
    // Removes element i, keeping the order.
    void RemoveAt(usize i) {
        for (usize k = i; k + 1 < size_; ++k) data_[k] = Move(data_[k + 1]);
        PopBack();
    }
    // Removes the first n elements (a cheap queue: callers pop from the front in batches).
    void RemoveFront(usize n) {
        if (n == 0) return;
        for (usize k = 0; k + n < size_; ++k) data_[k] = Move(data_[k + n]);
        for (usize k = 0; k < n && size_ > 0; ++k) PopBack();
    }

private:
    void CopyFrom(const Vec& o) {
        Reserve(o.size_);
        for (usize i = 0; i < o.size_; ++i) Emplace(o.data_[i]);
    }
    void Destroy() {
        Clear();
        MemFree(data_);
        data_ = nullptr;
        cap_ = 0;
    }

    T* data_ = nullptr;
    usize size_ = 0, cap_ = 0;
};

} // namespace sfv
