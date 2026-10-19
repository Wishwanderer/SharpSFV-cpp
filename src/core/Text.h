// SharpSFV (C++ port) - text helpers.
// Replaces the .NET string facilities the original relies on: char.IsWhiteSpace, Trim,
// StringComparison.OrdinalIgnoreCase, int.TryParse, File.ReadAllLines line splitting,
// and UTF-8 <-> UTF-16 conversion at the Win32 boundary.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sfv::text {

// All strings in the core are UTF-8.

// Decodes one code point at s[i] and advances i. Invalid sequences yield U+FFFD and
// consume a single byte.
char32_t DecodeUtf8(std::string_view s, std::size_t& i);
void AppendUtf8(std::string& out, char32_t cp);

// char.IsWhiteSpace / Regex \s (identical sets in .NET).
bool IsWhiteSpace(char32_t cp);

std::string_view TrimStart(std::string_view s);
std::string_view TrimEnd(std::string_view s);
std::string_view Trim(std::string_view s);
bool IsNullOrWhiteSpace(std::string_view s);

// Ordinal-ignore-case support. FoldCase upper-cases using the invariant culture on
// Windows (what OrdinalIgnoreCase uses) and ASCII rules elsewhere.
std::string FoldCase(std::string_view s);
bool EqualsIgnoreCase(std::string_view a, std::string_view b);
bool StartsWithIgnoreCase(std::string_view s, std::string_view prefix);
bool EndsWithIgnoreCase(std::string_view s, std::string_view suffix);
bool ContainsIgnoreCase(std::string_view haystack, std::string_view needle);
int CompareIgnoreCase(std::string_view a, std::string_view b);

// string.Split(chars, RemoveEmptyEntries when removeEmpty).
std::vector<std::string> Split(std::string_view s, std::string_view separators, bool removeEmpty);

// File.ReadAllLines line semantics: splits on "\r\n", "\r" and "\n"; a trailing
// terminator does not produce a final empty line.
std::vector<std::string> SplitLines(std::string_view s);

// Decodes file bytes the way StreamReader does by default: honours UTF-8, UTF-16 LE/BE
// and UTF-32 LE byte-order marks, otherwise assumes UTF-8. Result is UTF-8.
std::string DecodeTextFile(std::string_view bytes);

// int.TryParse(s, out v): on failure v is set to 0, like the out parameter in C#.
bool TryParseInt32(std::string_view s, int& v);

// double.ToString("F1") (invariant formatting).
std::string FormatF1(double v);

#ifdef _WIN32
std::wstring Widen(std::string_view s);
std::string Narrow(std::wstring_view s);
#endif

} // namespace sfv::text
