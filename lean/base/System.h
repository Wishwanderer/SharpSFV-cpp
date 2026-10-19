// SharpSFV (lean C++ rewrite) - console, clock, Ctrl+C and the command line.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "Str.h"
#include "Vec.h"

namespace sfv {

namespace console {
void Write(StrView s);
void WriteLine(StrView s = {});
void WriteError(StrView s); // stderr, with newline
} // namespace console

i64 TickMs(); // monotonic milliseconds (Environment.TickCount64)

// Ctrl+C / Ctrl+Break: sets a flag the engine polls instead of killing the process.
void InstallInterruptHandler();
void RemoveInterruptHandler();
bool Interrupted();

#ifdef _WIN32
// The program's arguments (without the program name), parsed from GetCommandLineW() with
// the C runtime's rules, as UTF-8.
Vec<Str> CommandLineArguments();
Vec<Str> ParseCommandLine(const wchar_t* commandLine); // exposed for tests
#endif

} // namespace sfv
