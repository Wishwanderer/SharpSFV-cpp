// SharpSFV (C++ port) - editing a ';'-separated PATH list (the user's PATH variable).
// Used by the optional "Install for command line" action, which replaces the original's
// silent self-installer (Utils/SelfInstaller.cs).
//
// Entries are compared case-insensitively, ignoring surrounding quotes/spaces and a
// trailing backslash; `expand` (e.g. ExpandEnvironmentStrings) lets "%LOCALAPPDATA%\SharpSFV"
// match "C:\Users\me\AppData\Local\SharpSFV". The original used a substring test
// (currentPath.Contains(dir)), which also matched longer paths such as "...\SharpSFV2".
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace sfv {

using ExpandFn = std::function<std::string(std::string_view)>;

bool PathListContains(std::string_view list, std::string_view dir, const ExpandFn& expand = nullptr);
// Appends dir (unchanged if already present). Never leaves a leading or doubled ';'.
std::string PathListAdd(std::string_view list, std::string_view dir, const ExpandFn& expand = nullptr);
// Removes every entry equal to dir; other entries keep their exact text and order.
std::string PathListRemove(std::string_view list, std::string_view dir, const ExpandFn& expand = nullptr);

} // namespace sfv
