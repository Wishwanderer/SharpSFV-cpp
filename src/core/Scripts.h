// SharpSFV (C++ port) - generated Windows batch scripts (bad-file and duplicate tools).
// Ported from PerformBatchExport / PerformGenerateDupCopyScript / PerformGenerateDupDeleteScript.
//
// Fixes over the original (see README):
//   * The copy script's banner "echo ... 1st file -> Dest: ..." contained an unescaped '>',
//     which cmd.exe treats as output redirection; it is now escaped (^>).
//   * '%' in a file name is doubled, otherwise cmd.exe expands it inside a .bat file.
//   * "chcp 65001" makes cmd.exe read the UTF-8 file correctly, so non-ASCII paths work.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sfv {

// "path" for a .bat file: quoted, with % doubled.
std::string BatchQuote(std::string_view path);

// Full script texts (CRLF line endings, UTF-8).
std::string DeleteBadFilesScript(const std::vector<std::string>& paths);
// Each group: the first path is kept; it is copied over the others / the others are deleted.
std::string CopyDuplicatesScript(const std::vector<std::vector<std::string>>& groups);
std::string DeleteDuplicatesScript(const std::vector<std::vector<std::string>>& groups);

} // namespace sfv
