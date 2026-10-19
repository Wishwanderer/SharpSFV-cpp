// SharpSFV (lean C++ rewrite) - System.IO.Path semantics.
// Line-for-line port of src/core/PathUtil.* onto Str/StrView.
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

namespace sfv::path {

#ifdef _WIN32
inline constexpr char kSep = '\\';
inline constexpr bool kIgnoreCase = true;
#else
inline constexpr char kSep = '/';
inline constexpr bool kIgnoreCase = false;
#endif

bool IsSep(char c);
bool IsPathRooted(StrView p);
usize GetRootLength(StrView p);
Str GetPathRoot(StrView p);

Str Combine(StrView a, StrView b);
StrView GetFileName(StrView p);
Opt<Str> GetDirectoryName(StrView p); // no value == C# null
StrView GetExtension(StrView p);
StrView GetFileNameWithoutExtension(StrView p);
Str GetFullPath(StrView p);                                    // "" on failure
Opt<Str> GetRelativePath(StrView relativeTo, StrView p);       // no value for empty arguments
Str SanitizeFileName(StrView name);
Str FindCommonBasePath(const Vec<Str>& paths);

} // namespace sfv::path
