// SharpSFV (lean C++ rewrite) - UTF-8 strings (replaces std::string / std::string_view).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: constructors, assignments and concatenation moved out of line (size).
// Modified 2026-10-04: a + b + c joins its pieces with one allocation (StrCat; size and speed).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
//
// Text is UTF-8 inside the program, exactly as in the current (standard-library) build, so
// the engine logic ports line for line; it is converted to UTF-16 only at the Windows API
// boundary (see WStr in Platform.h).
#pragma once

#include "Base.h"

namespace sfv {

inline constexpr usize kNpos = static_cast<usize>(-1);

class Str;

// A read-only view of characters owned elsewhere.
class StrView {
public:
    constexpr StrView() = default;
    constexpr StrView(const char* p, usize n) : p_(p), n_(n) {}
    StrView(const char* z) : p_(z), n_(z ? strlen(z) : 0) {} // NOLINT: implicit like std::string_view
    inline StrView(const Str& s);                            // NOLINT

    const char* Data() const { return p_; }
    usize Size() const { return n_; }
    bool Empty() const { return n_ == 0; }
    char operator[](usize i) const { return p_[i]; }
    char Front() const { return p_[0]; }
    char Back() const { return p_[n_ - 1]; }
    const char* begin() const { return p_; }
    const char* end() const { return p_ + n_; }

    StrView Sub(usize pos, usize n = kNpos) const {
        if (pos > n_) pos = n_;
        if (n > n_ - pos) n = n_ - pos;
        return {p_ + pos, n};
    }
    StrView DropFront(usize k) const { return Sub(k); }
    bool StartsWith(StrView s) const { return s.n_ <= n_ && memcmp(p_, s.p_, s.n_) == 0; }
    bool StartsWith(char c) const { return n_ > 0 && p_[0] == c; }
    bool EndsWith(StrView s) const { return s.n_ <= n_ && memcmp(p_ + n_ - s.n_, s.p_, s.n_) == 0; }
    usize Find(char c, usize from = 0) const {
        for (usize i = from; i < n_; ++i)
            if (p_[i] == c) return i;
        return kNpos;
    }
    usize Find(StrView s, usize from = 0) const {
        if (s.n_ == 0) return from <= n_ ? from : kNpos;
        for (usize i = from; i + s.n_ <= n_; ++i)
            if (memcmp(p_ + i, s.p_, s.n_) == 0) return i;
        return kNpos;
    }
    usize RFind(char c) const {
        for (usize i = n_; i-- > 0;)
            if (p_[i] == c) return i;
        return kNpos;
    }
    bool Contains(char c) const { return Find(c) != kNpos; }
    // Byte-wise comparison (like std::string::compare): <0, 0, >0.
    int Compare(StrView o) const {
        const int c = memcmp(p_, o.p_, Min(n_, o.n_));
        if (c != 0) return c < 0 ? -1 : 1;
        return n_ == o.n_ ? 0 : (n_ < o.n_ ? -1 : 1);
    }
    friend bool operator==(StrView a, StrView b) { return a.n_ == b.n_ && memcmp(a.p_, b.p_, a.n_) == 0; }
    friend bool operator!=(StrView a, StrView b) { return !(a == b); }
    friend bool operator<(StrView a, StrView b) { return a.Compare(b) < 0; }

private:
    const char* p_ = "";
    usize n_ = 0;
};

class StrCat;

// An owning, growable, always NUL-terminated UTF-8 string.
class Str {
public:
    Str() = default;
    // Out of line (Base.cpp): these are used in hundreds of places, and one shared copy of
    // each keeps the program small.
    Str(StrView v);              // NOLINT
    Str(const char* z);          // NOLINT
    Str(const char* p, usize n);
    Str(const Str& o);
    Str(const StrCat& c); // NOLINT (a + b + ...)
    Str(Str&& o) noexcept : p_(o.p_), n_(o.n_), cap_(o.cap_) { o.p_ = nullptr, o.n_ = o.cap_ = 0; }
    Str& operator=(const Str& o);
    Str& operator=(Str&& o) noexcept;
    Str& operator=(StrView v);
    Str& operator=(const char* z);
    Str& operator=(const StrCat& c);
    ~Str() { MemFree(p_); }

    const char* Data() const { return p_ ? p_ : ""; }
    const char* CStr() const { return Data(); }
    char* MutableData() { return p_; }
    usize Size() const { return n_; }
    bool Empty() const { return n_ == 0; }
    char operator[](usize i) const { return p_[i]; }
    char& operator[](usize i) { return p_[i]; }
    char Back() const { return p_[n_ - 1]; }
    StrView View() const { return {Data(), n_}; }
    StrView Sub(usize pos, usize n = kNpos) const { return View().Sub(pos, n); }
    bool StartsWith(StrView s) const { return View().StartsWith(s); }
    bool StartsWith(char c) const { return View().StartsWith(c); }
    bool EndsWith(StrView s) const { return View().EndsWith(s); }
    usize Find(char c, usize from = 0) const { return View().Find(c, from); }
    usize Find(StrView s, usize from = 0) const { return View().Find(s, from); }
    usize RFind(char c) const { return View().RFind(c); }

    void Reserve(usize n);
    Str& Append(StrView v);
    Str& Append(char c);
    Str& Append(const char* z) { return Append(StrView(z)); }
    Str& Append(const Str& s) { return Append(s.View()); }
    Str& AppendInt(i64 v);
    void Resize(usize n, char fill = 0) {
        Reserve(n);
        for (usize i = n_; i < n; ++i) p_[i] = fill;
        n_ = n;
        p_[n_] = 0;
    }
    void PopBack() { p_[--n_] = 0; }
    void Clear() {
        n_ = 0;
        if (p_) p_[0] = 0;
    }

    Str& operator+=(StrView v) { return Append(v); }
    Str& operator+=(const char* z) { return Append(StrView(z)); }
    Str& operator+=(const Str& s) { return Append(s.View()); }
    Str& operator+=(char c) { return Append(c); }
    Str& operator+=(const StrCat& c);

    friend bool operator==(const Str& a, const Str& b) { return a.View() == b.View(); }
    friend bool operator==(const Str& a, StrView b) { return a.View() == b; }
    friend bool operator==(const Str& a, const char* b) { return a.View() == StrView(b); }
    friend bool operator!=(const Str& a, const Str& b) { return !(a == b); }
    friend bool operator!=(const Str& a, const char* b) { return !(a == b); }
    friend bool operator<(const Str& a, const Str& b) { return a.View() < b.View(); }

private:
    char* p_ = nullptr;
    usize n_ = 0, cap_ = 0;
};

inline StrView::StrView(const Str& s) : p_(s.Data()), n_(s.Size()) {}

// Concatenation (Str + anything string-like): readable call sites like the current code.
// `a + b + c` collects views of its parts in a StrCat and builds the result once, when it is
// converted to a Str (or a StrView: then it is built inside the StrCat, which lives until the
// end of the statement). One allocation and one out-of-line call per expression, instead of a
// temporary string per '+'. A StrCat must not outlive its statement: never store one.
class StrCat {
public:
    static constexpr int kMax = 24;
    StrCat(StrView a, StrView b) : n_(2) { part_[0] = a, part_[1] = b; }
    StrCat(StrView a, char c) : n_(2), nch_(1) {
        ch_[0] = c;
        part_[0] = a, part_[1] = StrView(ch_, 1);
    }
    StrCat(const StrCat&) = delete;
    StrCat& operator=(const StrCat&) = delete;
    StrCat&& Add(StrView v) && {
        if (n_ == kMax) Flatten();
        part_[n_++] = v;
        return Move(*this);
    }
    StrCat&& Add(char c) && {
        if (nch_ == 8 || n_ == kMax) Flatten();
        ch_[nch_] = c;
        part_[n_++] = StrView(&ch_[nch_++], 1);
        return Move(*this);
    }
    operator StrView() const {               // NOLINT
        out_ = Build();
        return out_.View();
    }
    __attribute__((noinline)) Str Build() const;

private:
    __attribute__((noinline)) void Flatten(); // more than kMax parts: join them into flat_ first
    StrView part_[kMax];
    int n_;
    int nch_ = 0;
    char ch_[8];
    Str flat_;         // parts joined by Flatten
    mutable Str out_;  // the result, when a view of it was asked for
};

inline Str::Str(const StrCat& c) : Str(c.Build()) {}
inline Str& Str::operator=(const StrCat& c) { return *this = c.Build(); }
inline Str& Str::operator+=(const StrCat& c) { return Append(StrView(c)); }

inline StrCat operator+(StrView a, StrView b) { return StrCat(a, b); }
inline StrCat operator+(const Str& a, StrView b) { return StrCat(a.View(), b); }
inline StrCat operator+(StrView a, const Str& b) { return StrCat(a, b.View()); }
inline StrCat operator+(const Str& a, const Str& b) { return StrCat(a.View(), b.View()); }
inline StrCat operator+(const Str& a, const char* b) { return StrCat(a.View(), StrView(b)); }
inline StrCat operator+(const char* a, const Str& b) { return StrCat(StrView(a), b.View()); }
inline StrCat operator+(const char* a, StrView b) { return StrCat(StrView(a), b); }
inline StrCat operator+(StrView a, const char* b) { return StrCat(a, StrView(b)); }
inline StrCat operator+(const Str& a, char c) { return StrCat(a.View(), c); }
inline StrCat&& operator+(StrCat&& a, StrView b) { return Move(a).Add(b); }
inline StrCat&& operator+(StrCat&& a, const Str& b) { return Move(a).Add(b.View()); }
inline StrCat&& operator+(StrCat&& a, const char* b) { return Move(a).Add(StrView(b)); }
inline StrCat&& operator+(StrCat&& a, char c) { return Move(a).Add(c); }

// Decimal text of an integer (std::to_string).
Str ToStr(i64 v);

} // namespace sfv
