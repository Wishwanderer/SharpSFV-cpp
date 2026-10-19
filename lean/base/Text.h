// SharpSFV (lean C++ rewrite) - text helpers with .NET semantics.
// Line-for-line port of src/core/Text.* (the standard-library build) onto Str/StrView.
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

#ifdef _WIN32
#include <wchar.h>
#endif

namespace sfv::text {

u32 DecodeUtf8(StrView s, usize& i); // invalid sequences give U+FFFD and advance one byte
void AppendUtf8(Str& out, u32 cp);

bool IsWhiteSpace(u32 cp); // char.IsWhiteSpace

StrView TrimStart(StrView s);
StrView TrimEnd(StrView s);
StrView Trim(StrView s);
bool IsNullOrWhiteSpace(StrView s);

// Case-insensitive helpers (OrdinalIgnoreCase: invariant upper-casing on Windows, ASCII
// elsewhere), as in the standard-library build.
Str FoldCase(StrView s);
bool EqualsIgnoreCase(StrView a, StrView b);
bool StartsWithIgnoreCase(StrView s, StrView prefix);
bool EndsWithIgnoreCase(StrView s, StrView suffix);
bool ContainsIgnoreCase(StrView haystack, StrView needle);
int CompareIgnoreCase(StrView a, StrView b);

Vec<Str> Split(StrView s, StrView separators, bool removeEmpty);
Vec<Str> SplitLines(StrView s); // File.ReadAllLines: \r\n, \r or \n
Str DecodeTextFile(StrView bytes); // BOM-aware (UTF-8/16/32), invalid UTF-8 -> U+FFFD
bool TryParseInt32(StrView s, int& v); // int.TryParse (writes 0 on failure)
Str FormatF1(double v); // "F1": one decimal, half away from zero

#ifdef _WIN32
// UTF-16 copy of a UTF-8 string for Windows API calls (always NUL-terminated).
class WStr {
public:
    WStr() { v_.Push(0); }
    explicit WStr(StrView s);
    const wchar_t* C() const { return v_.Data(); }
    wchar_t* Data() { return v_.Data(); }
    usize Size() const { return v_.Size() - 1; }
    void Append(const wchar_t* z);
    void Append(wchar_t c);
    void Resize(usize n); // contents up to n kept, NUL-terminated
    wchar_t Back() const { return v_[v_.Size() - 2]; }

private:
    Vec<wchar_t> v_;
};
Str Narrow(const wchar_t* s, usize n);
inline Str Narrow(const wchar_t* z) { return Narrow(z, wcslen(z)); }
#endif

} // namespace sfv::text
