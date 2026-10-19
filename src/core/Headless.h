// SharpSFV (C++ port) - headless (command-line) mode.
// Ported from the -headless paths of Program.cs / Form1.cs (HandleDroppedPaths,
// RunHashCreation, RunVerification, ThrottledUiUpdate console output, HandleCompletion).
//
//   SharpSFV -headless <checksum file>                      verify (.sfv .md5 .sha1 .sha256 .xxh3 ...)
//   SharpSFV -headless <files/folders...>                   hash and report only
//   SharpSFV -headless -create <files/folders...> [-out F]  hash and write a checksum file
//   Options: -algo xxh3|crc32|md5|sha1|sha256   -mode auto|hdd|ssd
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include <string>
#include <vector>

namespace sfv {

// Exit codes (new; the C# app always exited with 0).
enum HeadlessExit : int {
    kExitOk = 0,        // everything verified / hashed
    kExitProblems = 1,  // BAD, MISSING or unreadable files
    kExitFailure = 2,   // usage error, unreadable checksum file, cannot write output
    kExitCancelled = 3, // Ctrl+C
};

// args: command-line arguments without the program name, UTF-8.
// iniDirectory: where SharpSFV.ini lives (the executable's folder).
int RunHeadless(const std::vector<std::string>& args, const std::string& iniDirectory);

std::string HeadlessUsage();

} // namespace sfv
