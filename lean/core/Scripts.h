// SharpSFV (lean C++ rewrite) - generated Windows batch scripts (bad-file and duplicate
// tools) and editing the user's PATH list.
// Port of src/core/Scripts.* and src/core/EnvPath.* (same text, same fixes: escaped '>' in
// the banner, '%' doubled in names, "chcp 65001" for UTF-8; PATH entries compared
// case-insensitively, ignoring quotes and a trailing backslash).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "../base/Str.h"
#include "../base/Vec.h"

namespace sfv {

// "path" for a .bat file: quoted, with % doubled.
Str BatchQuote(StrView path);

// Full script texts (CRLF line endings, UTF-8).
Str DeleteBadFilesScript(const Vec<Str>& paths);
// Each group: the first path is kept; it is copied over the others / the others are deleted.
Str CopyDuplicatesScript(const Vec<Vec<Str>>& groups);
Str DeleteDuplicatesScript(const Vec<Vec<Str>>& groups);

// ';'-separated PATH lists. `expand` (e.g. ExpandEnvironmentStrings) may be null.
using ExpandFn = Str (*)(StrView);
bool PathListContains(StrView list, StrView dir, ExpandFn expand = nullptr);
Str PathListAdd(StrView list, StrView dir, ExpandFn expand = nullptr);    // never a leading or doubled ';'
Str PathListRemove(StrView list, StrView dir, ExpandFn expand = nullptr); // other entries kept verbatim

} // namespace sfv
