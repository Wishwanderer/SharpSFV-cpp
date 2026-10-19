// SharpSFV (lean C++ rewrite) - SharpSFV-cli.exe entry point.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
//
// The Windows build has no C runtime start-up code: Windows calls mainCRTStartup directly,
// which reads the command line itself and ends with ExitProcess. Nothing in the program
// relies on global constructors (the build checks that none exist).
#include "../base/FileSystem.h"
#include "../base/System.h"
#include "../core/Cli.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifdef SFV_FREESTANDING
extern "C" void mainCRTStartup() {
    int code;
    {
        const sfv::Vec<sfv::Str> args = sfv::CommandLineArguments();
        code = sfv::RunHeadless(args, sfv::fs::ExecutableDirectory());
    }
    ExitProcess(static_cast<UINT>(code));
}
#else
int main() { // a normal (CRT) build of the same program, for comparison and debugging
    const sfv::Vec<sfv::Str> args = sfv::CommandLineArguments();
    return sfv::RunHeadless(args, sfv::fs::ExecutableDirectory());
}
#endif

#else

int main(int argc, char** argv) {
    sfv::Vec<sfv::Str> args;
    for (int i = 1; i < argc; ++i) args.Push(sfv::Str(argv[i]));
    return sfv::RunHeadless(args, sfv::fs::ExecutableDirectory());
}

#endif
