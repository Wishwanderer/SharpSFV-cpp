// SharpSFV (lean C++ rewrite) - GUI entry point (SharpSFV.exe).
// Port of src/gui/main.cpp (Program.cs):
//   -headless  attach to the parent console and run the command-line mode in this process,
//              as the C# original did (the standard build starts SharpSFV-cli.exe instead;
//              here the command-line code costs little, so SharpSFV.exe is self-sufficient)
//   -create    Explorer "Create Checksum": one primary instance collects every selected
//              item (later instances forward their paths over a named pipe and exit)
//   otherwise  the main window; file paths on the command line are opened (verify/create)
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "../base/FileSystem.h"
#include "../base/System.h"
#include "../core/Cli.h"
#include "MainWindow.h"
#include "SingleInstance.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

using namespace sfv;

namespace {

bool HasFlag(const Vec<Str>& args, const char* flag) {
    for (const Str& a : args)
        if (text::EqualsIgnoreCase(a, flag)) return true;
    return false;
}

// A GUI-subsystem process has no console; attach to the one it was started from so
// "SharpSFV -headless ..." can print. Redirected handles (> file, | pipe) are kept.
bool AttachParentConsole() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return false;
    const DWORD which[] = {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for (const DWORD w : which) {
        HANDLE h = GetStdHandle(w);
        if (!h || h == INVALID_HANDLE_VALUE || GetFileType(h) == FILE_TYPE_UNKNOWN) {
            HANDLE c = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
            if (c != INVALID_HANDLE_VALUE) SetStdHandle(w, c);
        }
    }
    return true;
}

int Run(int show) {
    // The window's arguments as Explorer passes them (CommandLineToArgvW rules); -headless uses
    // the C runtime's rules instead, exactly as SharpSFV-cli.exe parses its command line.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    Vec<Str> args;
    for (int i = 1; i < argc; ++i) args.Push(text::Narrow(argv[i]));
    LocalFree(argv);

    if (HasFlag(args, "-headless")) {
        const bool attached = AttachParentConsole();
        const int code = RunHeadless(CommandLineArguments(), fs::ExecutableDirectory());
        if (attached) FreeConsole();
        return code;
    }

    const bool create = HasFlag(args, "-create");
    if (create && !gui::AcquireCreateModeMutex()) {
        if (gui::ForwardToPrimary(args)) return 0;
        // The primary could not be reached (it may have just exited): take over.
        if (!gui::AcquireCreateModeMutex()) return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES |
                                             ICC_LINK_CLASS | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    int code = 1;
    {
        gui::MainWindow* window = new gui::MainWindow(args, create); // large: on the heap
        if (window->Create(show)) code = window->RunMessageLoop();
        delete window;
    }
    CoUninitialize();
    return code;
}

int ShowCommand() {
    STARTUPINFOW si{};
    si.cb = sizeof si;
    GetStartupInfoW(&si);
    return (si.dwFlags & STARTF_USESHOWWINDOW) ? si.wShowWindow : SW_SHOWDEFAULT;
}

} // namespace

#ifdef SFV_FREESTANDING
// No C runtime start-up code: this is the process entry point (see base/Base.h).
extern "C" void WinMainCRTStartup() { ExitProcess(static_cast<UINT>(Run(ShowCommand()))); }
#else
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show) { return Run(show); }
#endif
