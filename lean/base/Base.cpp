// SharpSFV (lean C++ rewrite) - heap, integer formatting and the freestanding C++ glue.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: Str constructors, assignments and concatenation (moved from Str.h).
// Modified 2026-10-04: StrCat (joining strings in one allocation) replaces operator+.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Base.h"
#include "Str.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <stdlib.h>
#include <unistd.h>
#endif

namespace sfv {

#ifdef _WIN32
void* MemAlloc(usize n) {
    void* p = HeapAlloc(GetProcessHeap(), 0, n ? n : 1);
    if (!p) FatalOutOfMemory();
    return p;
}
void* MemRealloc(void* p, usize n) {
    if (!p) return MemAlloc(n);
    void* q = HeapReAlloc(GetProcessHeap(), 0, p, n ? n : 1);
    if (!q) FatalOutOfMemory();
    return q;
}
void MemFree(void* p) {
    if (p) HeapFree(GetProcessHeap(), 0, p);
}
void FatalOutOfMemory() {
    static const char kMsg[] = "SharpSFV: out of memory\r\n";
    DWORD w = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), kMsg, sizeof kMsg - 1, &w, nullptr);
    ExitProcess(2);
}
#else
void* MemAlloc(usize n) {
    void* p = malloc(n ? n : 1);
    if (!p) FatalOutOfMemory();
    return p;
}
void* MemRealloc(void* p, usize n) {
    void* q = realloc(p, n ? n : 1);
    if (!q) FatalOutOfMemory();
    return q;
}
void MemFree(void* p) { free(p); }
void FatalOutOfMemory() {
    static const char kMsg[] = "SharpSFV: out of memory\n";
    (void)!write(2, kMsg, sizeof kMsg - 1);
    _exit(2);
}
#endif

// Out of line: these run everywhere, and one copy keeps the program small.
void Str::Reserve(usize n) {
    if (n + 1 <= cap_) return;
    usize c = cap_ < 16 ? 16 : cap_;
    while (c < n + 1) c *= 2;
    p_ = static_cast<char*>(MemRealloc(p_, c));
    cap_ = c;
}

Str& Str::Append(StrView v) {
    if (v.Empty()) return *this; // an empty string needs no buffer (Data() returns "")
    // v may point into this string: copy it before a reallocation can move it.
    if (p_ && v.Data() >= p_ && v.Data() < p_ + cap_) {
        Str copy(v.Data(), v.Size());
        return Append(copy.View());
    }
    Reserve(n_ + v.Size());
    memcpy(p_ + n_, v.Data(), v.Size());
    n_ += v.Size();
    p_[n_] = 0;
    return *this;
}

Str& Str::Append(char c) {
    Reserve(n_ + 1);
    p_[n_++] = c;
    p_[n_] = 0;
    return *this;
}

Str& Str::AppendInt(i64 v) {
    char buf[24];
    usize i = sizeof buf;
    u64 u = v < 0 ? 0 - static_cast<u64>(v) : static_cast<u64>(v);
    do {
        buf[--i] = static_cast<char>('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0) buf[--i] = '-';
    return Append(StrView(buf + i, sizeof buf - i));
}

Str::Str(StrView v) { Append(v); }
Str::Str(const char* z) { Append(StrView(z)); }
Str::Str(const char* p, usize n) { Append(StrView(p, n)); }
Str::Str(const Str& o) { Append(o.View()); }
Str& Str::operator=(const Str& o) {
    if (this != &o) {
        n_ = 0;
        Append(o.View());
    }
    return *this;
}
Str& Str::operator=(Str&& o) noexcept {
    if (this != &o) {
        MemFree(p_);
        p_ = o.p_, n_ = o.n_, cap_ = o.cap_;
        o.p_ = nullptr, o.n_ = o.cap_ = 0;
    }
    return *this;
}
Str& Str::operator=(StrView v) {
    Str t(v);
    return *this = Move(t);
}
Str& Str::operator=(const char* z) { return *this = StrView(z); }

Str StrCat::Build() const {
    usize total = flat_.Size();
    for (int i = 0; i < n_; ++i) total += part_[i].Size();
    Str r;
    r.Reserve(total);
    r.Append(flat_.View());
    for (int i = 0; i < n_; ++i) r.Append(part_[i]);
    return r;
}

void StrCat::Flatten() {
    flat_ = Build();
    n_ = 0;
    nch_ = 0;
}

Str ToStr(i64 v) {
    Str s;
    s.AppendInt(v);
    return s;
}

} // namespace sfv

// ------------------------------------------------------------------ freestanding glue
// The shipped Windows build links no C++ runtime, so the few things the compiler expects
// from one are defined here: operator new/delete on the process heap, and the handler for
// a call to a pure virtual function (never happens; ends the process if it ever did).
#ifdef SFV_FREESTANDING
void* operator new(size_t n) { return sfv::MemAlloc(n); }
void* operator new[](size_t n) { return sfv::MemAlloc(n); }
void operator delete(void* p) noexcept { sfv::MemFree(p); }
void operator delete[](void* p) noexcept { sfv::MemFree(p); }
void operator delete(void* p, size_t) noexcept { sfv::MemFree(p); }
void operator delete[](void* p, size_t) noexcept { sfv::MemFree(p); }
extern "C" void __cxa_pure_virtual() { ExitProcess(3); }
#endif
