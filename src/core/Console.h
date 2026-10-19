// SharpSFV (C++ port) - console output.
// UTF-8 aware: on Windows, writes to a real console with WriteConsoleW (so non-ASCII file
// names display correctly) and writes UTF-8 bytes when redirected to a file or pipe.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include <string_view>

namespace sfv::console {

void Write(std::string_view s);
void WriteLine(std::string_view s = {});
void WriteError(std::string_view s); // stderr, with newline

} // namespace sfv::console
