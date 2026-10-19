// SharpSFV (lean C++ rewrite) - console, clock, Ctrl+C and the command line (see System.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "System.h"

#include "Text.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <signal.h>
#include <time.h>
#include <unistd.h>
#endif

namespace sfv {

namespace {
int g_interrupted = 0; // read/written with __atomic builtins

#ifdef _WIN32
void WriteTo(bool err, StrView s) {
    HANDLE h = GetStdHandle(err ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (h == nullptr || h == INVALID_HANDLE_VALUE || s.Empty()) return;
    DWORD mode, written;
    if (GetConsoleMode(h, &mode)) { // a console window: write UTF-16 so any name displays
        const text::WStr w(s);
        WriteConsoleW(h, w.C(), static_cast<DWORD>(w.Size()), &written, nullptr);
    } else { // redirected to a file or pipe: UTF-8 bytes, as the standard build writes
        WriteFile(h, s.Data(), static_cast<DWORD>(s.Size()), &written, nullptr);
    }
}
constexpr const char* kNl = "\r\n";

BOOL WINAPI CtrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        __atomic_store_n(&g_interrupted, 1, __ATOMIC_SEQ_CST);
        return TRUE;
    }
    return FALSE;
}
#else
void WriteTo(bool err, StrView s) {
    const int fd = err ? 2 : 1;
    usize off = 0;
    while (off < s.Size()) {
        const ssize_t n = ::write(fd, s.Data() + off, s.Size() - off);
        if (n <= 0) return;
        off += static_cast<usize>(n);
    }
}
constexpr const char* kNl = "\n";

extern "C" void SigIntHandler(int) { __atomic_store_n(&g_interrupted, 1, __ATOMIC_SEQ_CST); }
#endif
} // namespace

namespace console {
void Write(StrView s) { WriteTo(false, s); }
void WriteLine(StrView s) { WriteTo(false, Str(s) + kNl); }
void WriteError(StrView s) { WriteTo(true, Str(s) + kNl); }
} // namespace console

#ifdef _WIN32
i64 TickMs() { return static_cast<i64>(GetTickCount64()); }
void InstallInterruptHandler() { SetConsoleCtrlHandler(CtrlHandler, TRUE); }
void RemoveInterruptHandler() { SetConsoleCtrlHandler(CtrlHandler, FALSE); }
#else
i64 TickMs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<i64>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}
void InstallInterruptHandler() { signal(SIGINT, SigIntHandler); }
void RemoveInterruptHandler() { signal(SIGINT, SIG_DFL); }
#endif
bool Interrupted() { return __atomic_load_n(&g_interrupted, __ATOMIC_SEQ_CST) != 0; }

#ifdef _WIN32
Vec<Str> ParseCommandLine(const wchar_t* p) {
    Vec<Str> args;
    if (!p) return args;
    // Program name: up to the closing quote, or to the first blank. No escapes.
    if (*p == L'"') {
        ++p;
        while (*p && *p != L'"') ++p;
        if (*p) ++p;
    } else {
        while (*p && *p != L' ' && *p != L'\t') ++p;
    }
    for (;;) {
        while (*p == L' ' || *p == L'\t') ++p;
        if (!*p) break;
        text::WStr arg;
        bool quoted = false;
        while (*p && (quoted || (*p != L' ' && *p != L'\t'))) {
            usize slashes = 0;
            while (*p == L'\\') ++slashes, ++p;
            if (*p == L'"') {
                for (usize i = 0; i < slashes / 2; ++i) arg.Append(L'\\');
                if (slashes % 2) { // \" is a literal quote
                    arg.Append(L'"');
                    ++p;
                } else if (quoted && p[1] == L'"') { // "" inside quotes is a literal quote
                    arg.Append(L'"');
                    p += 2;
                } else {
                    quoted = !quoted;
                    ++p;
                }
            } else {
                for (usize i = 0; i < slashes; ++i) arg.Append(L'\\');
                if (*p && (quoted || (*p != L' ' && *p != L'\t'))) arg.Append(*p++);
            }
        }
        args.Push(text::Narrow(arg.C(), arg.Size()));
    }
    return args;
}

Vec<Str> CommandLineArguments() { return ParseCommandLine(GetCommandLineW()); }
#endif

} // namespace sfv
