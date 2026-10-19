// SharpSFV (C++ port) - command-line entry point (SharpSFV-cli).
// A console-subsystem build of the headless mode, so the shell waits for it and output
// and exit codes work naturally in scripts. The GUI build accepts the same arguments
// after -headless.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "FileSystem.h"
#include "Headless.h"
#include "Text.h"

#include <string>
#include <vector>

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(sfv::text::Narrow(argv[i]));
    return sfv::RunHeadless(args, sfv::fs::ExecutableDirectory());
}
#else
int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    return sfv::RunHeadless(args, sfv::fs::ExecutableDirectory());
}
#endif
