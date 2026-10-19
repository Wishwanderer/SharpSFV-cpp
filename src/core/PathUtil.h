// SharpSFV (C++ port) - path helpers.
// Re-implements the System.IO.Path behaviour SharpSFV relies on (Combine, GetFileName,
// GetDirectoryName, GetExtension, GetRelativePath, ...) with the same Windows semantics,
// plus FindCommonBasePath from Form1.Processing.Helpers.cs.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sfv::path {

#ifdef _WIN32
inline constexpr char kSep = '\\';
inline constexpr bool kIgnoreCase = true;
#else
inline constexpr char kSep = '/';
inline constexpr bool kIgnoreCase = false;
#endif

bool IsSep(char c);
bool IsPathRooted(std::string_view p);
std::size_t GetRootLength(std::string_view p);
std::string GetPathRoot(std::string_view p);

std::string Combine(std::string_view a, std::string_view b);
std::string_view GetFileName(std::string_view p);
std::optional<std::string> GetDirectoryName(std::string_view p); // nullopt == C# null
std::string_view GetExtension(std::string_view p);
std::string_view GetFileNameWithoutExtension(std::string_view p);

// Path.GetFullPath. Returns "" on failure (where .NET would throw).
std::string GetFullPath(std::string_view p);

// Path.GetRelativePath. Returns nullopt where .NET would throw (empty arguments).
std::optional<std::string> GetRelativePath(std::string_view relativeTo, std::string_view p);

// Replaces Path.GetInvalidFileNameChars() with '_' (string.Join("_", name.Split(invalid))).
std::string SanitizeFileName(std::string_view name);

// Deepest common directory of a list of directory paths (FindCommonBasePath(List<string>)
// in Form1.Processing.Helpers.cs, which is the overload every call site binds to).
std::string FindCommonBasePath(const std::vector<std::string>& paths);

} // namespace sfv::path
