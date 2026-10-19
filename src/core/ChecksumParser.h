// SharpSFV (C++ port) - checksum file parsing (.sfv, .md5, .sha1, .sha256, .xxh3, ...).
// Ported from the parsing section of RunVerification in Form1.Processing.cs.
//
// The original matches every line against two regular expressions:
//   A: ^\s*([0-9a-fA-F]+)\s+\*?(.*?)\s*$      "HASH *filename" / "HASH filename"
//   B: ^\s*(.*?)\s+([0-9a-fA-F]{8})\s*$       "filename CRC32" (classic SFV)
// B is preferred for CRC-32 files, A otherwise. The matchers below reproduce exactly what
// those patterns capture without a regex engine (std::regex is slow and recursive).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "Types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sfv {

struct ChecksumEntry {
    std::vector<std::uint8_t> expectedHash;
    std::string fileName; // as written in the file (relative or absolute)
};

struct ParsedChecksumFile {
    std::vector<ChecksumEntry> entries;
    std::string comments; // "//" comment lines, each terminated by a newline
};

// Algorithm implied by the checksum file's extension; fallback for unknown extensions.
HashType DetectAlgoFromExtension(std::string_view checksumPath, HashType fallback);

// Is this extension one the app treats as a checksum file when dropped/opened?
// (.xxh3 .xxh .sum .md5 .sfv .sha1 .sha256 .txt — _verificationExtensions)
bool IsVerificationExtension(std::string_view path);

// Parses decoded (UTF-8) file content.
ParsedChecksumFile ParseChecksumText(std::string_view content, HashType algo);

// Reads and parses a checksum file. nullopt (with error) if it cannot be read.
std::optional<ParsedChecksumFile> ParseChecksumFile(const std::string& path, HashType algo, std::string* error);

// Single-line matchers (exposed for tests). Return false when the pattern does not match.
bool MatchHashFirst(std::string_view line, std::string& hash, std::string& fileName); // pattern A
bool MatchNameFirst(std::string_view line, std::string& fileName, std::string& hash); // pattern B

} // namespace sfv
