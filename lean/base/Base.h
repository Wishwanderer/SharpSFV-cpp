// SharpSFV (lean C++ rewrite) - basic types and helpers used everywhere.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
//
// The lean build uses the C++ language (classes, templates, RAII) but not the C++ standard
// library: no std::string, std::vector, iostreams, exceptions or RTTI. Only the C headers
// below are used, for memcpy and friends, which come from the C runtime DLL that ships with
// Windows (msvcrt.dll). Everything else is in this folder or provided by Windows itself.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace sfv {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i32 = int32_t;
using i64 = int64_t;
using usize = size_t;

// std::move / std::forward without <utility>.
template <class T> struct RemoveRef { using Type = T; };
template <class T> struct RemoveRef<T&> { using Type = T; };
template <class T> struct RemoveRef<T&&> { using Type = T; };
template <class T> constexpr typename RemoveRef<T>::Type&& Move(T&& v) noexcept {
    return static_cast<typename RemoveRef<T>::Type&&>(v);
}
template <class T> constexpr T&& Forward(typename RemoveRef<T>::Type& v) noexcept { return static_cast<T&&>(v); }
template <class T> constexpr T&& Forward(typename RemoveRef<T>::Type&& v) noexcept { return static_cast<T&&>(v); }

template <class T> constexpr const T& Min(const T& a, const T& b) { return b < a ? b : a; }
template <class T> constexpr const T& Max(const T& a, const T& b) { return a < b ? b : a; }
template <class T> void Swap(T& a, T& b) {
    T t = Move(a);
    a = Move(b);
    b = Move(t);
}

// Heap. Never returns null: an allocation failure ends the process (FatalOutOfMemory), the
// same outcome an uncaught std::bad_alloc had, without exception machinery.
void* MemAlloc(usize n);
void* MemRealloc(void* p, usize n);
void MemFree(void* p);
[[noreturn]] void FatalOutOfMemory();

// A value that may be absent (std::optional for default-constructible types).
template <class T> class Opt {
public:
    Opt() = default;
    Opt(const T& v) : has_(true), v_(v) {} // NOLINT
    Opt(T&& v) : has_(true), v_(Move(v)) {} // NOLINT
    bool HasValue() const { return has_; }
    explicit operator bool() const { return has_; }
    T& operator*() { return v_; }
    const T& operator*() const { return v_; }
    T* operator->() { return &v_; }
    const T* operator->() const { return &v_; }
    T ValueOr(const T& fallback) const { return has_ ? v_ : fallback; }

private:
    bool has_ = false;
    T v_{};
};

// Non-owning reference to any callable (a lambda usually), like std::function_ref:
// one indirect call, no allocation, no per-lambda copy of the calling code.
template <class Sig> class FnRef;
template <class R, class... A> class FnRef<R(A...)> {
public:
    template <class F>
    FnRef(F&& f) : obj_(const_cast<void*>(static_cast<const void*>(&f))), call_([](void* o, A... a) -> R {
                       return (*static_cast<typename RemoveRef<F>::Type*>(o))(Forward<A>(a)...);
                   }) {}
    R operator()(A... a) const { return call_(obj_, Forward<A>(a)...); }

private:
    void* obj_;
    R (*call_)(void*, A...);
};

} // namespace sfv

// Placement new. The shipped Windows build (SFV_FREESTANDING) defines it itself; the
// Linux test build may mix in standard headers, so it takes the standard declaration.
#ifdef SFV_FREESTANDING
inline void* operator new(size_t, void* p) noexcept { return p; }
inline void operator delete(void*, void*) noexcept {}
#else
#include <new>
#endif
