// SharpSFV (C++ port) - console output.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
// Modified 2026-10-03: native mutex (Threading.h).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Console.h"

#include "Text.h"

#include <cstdio>
#include "Threading.h"
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sfv::console {

namespace {
Mutex g_mutex;

void WriteTo(bool err, std::string_view s) {
    std::lock_guard lk(g_mutex);
#ifdef _WIN32
    HANDLE h = GetStdHandle(err ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) return;
    DWORD mode;
    if (GetConsoleMode(h, &mode)) {
        const std::wstring w = text::Widen(s);
        DWORD written;
        WriteConsoleW(h, w.data(), static_cast<DWORD>(w.size()), &written, nullptr);
    } else {
        DWORD written;
        WriteFile(h, s.data(), static_cast<DWORD>(s.size()), &written, nullptr);
    }
#else
    std::FILE* f = err ? stderr : stdout;
    std::fwrite(s.data(), 1, s.size(), f);
    std::fflush(f);
#endif
}

#ifdef _WIN32
constexpr std::string_view kNl = "\r\n";
#else
constexpr std::string_view kNl = "\n";
#endif
} // namespace

void Write(std::string_view s) { WriteTo(false, s); }

void WriteLine(std::string_view s) {
    std::string line(s);
    line.append(kNl);
    WriteTo(false, line);
}

void WriteError(std::string_view s) {
    std::string line(s);
    line.append(kNl);
    WriteTo(true, line);
}

} // namespace sfv::console
