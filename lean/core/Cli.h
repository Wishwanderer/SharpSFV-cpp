// SharpSFV (lean C++ rewrite) - the command-line mode.
// Port of src/core/Headless.* (verify, hash, -create, -mode, and the drive and data-storage
// commands).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: drive and data-storage commands.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "../base/Str.h"
#include "../base/Vec.h"

namespace sfv {

enum ExitCode : int {
    kExitOk = 0,        // everything verified / hashed
    kExitProblems = 1,  // BAD, MISSING or unreadable files
    kExitFailure = 2,   // usage error, unreadable checksum file, cannot write output
    kExitCancelled = 3, // Ctrl+C
};

Str HeadlessUsage();
// argv without the program name; iniDirectory holds SharpSFV.ini.
int RunHeadless(const Vec<Str>& argv, StrView iniDirectory);

} // namespace sfv
