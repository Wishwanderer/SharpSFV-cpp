// SharpSFV (C++ port) - include/exclude filename filters.
// Ported from IsFileAllowed / FitsMask in Form1.Processing.Helpers.cs.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include <string>
#include <string_view>

namespace sfv {

// Matches a file name against a wildcard mask ("*.txt", "data?.dat"), case-insensitively.
bool FitsMask(std::string_view fileName, std::string_view mask);

// Exclusions win; when an include list is present the name must match one entry.
// Patterns are separated by ';' or ','.
bool IsFileAllowed(std::string_view fileName, std::string_view includePattern, std::string_view excludePattern);

} // namespace sfv
