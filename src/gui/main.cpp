// SharpSFV (C++ port) - GUI entry point (SharpSFV.exe).
// Ported from Program.cs:
//   -headless  attach to the parent console and run the command-line mode
//   -create    Explorer "Create Checksum": one primary instance collects every selected
//              item (later instances forward their paths over a named pipe and exit)
//   otherwise  the main window; file paths on the command line are opened (verify/create)
// The original also ran SelfInstaller.EnsureCentralizedInstall() here on every start,
// silently copying itself to %LOCALAPPDATA% and editing PATH. That is now an explicit,
// reversible menu action (Options > System Integration > Install for Command Line).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20; -headless can run SharpSFV-cli.exe.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "FileSystem.h"
#ifdef SHARPSFV_GUI_EMBED_HEADLESS
#include "Headless.h"
#endif
#include "MainWindow.h"
#include "PathUtil.h"
#include "SingleInstance.h"
#include "Text.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <cstring>
#include <exception>

using namespace sfv;

namespace {

bool HasFlag(const std::vector<std::string>& args, const char* flag) {
    for (const auto& a : args)
        if (text::EqualsIgnoreCase(a, flag)) return true;
    return false;
}

// A GUI-subsystem process has no console; attach to the one it was started from so
// "SharpSFV -headless ..." can print. Redirected handles (> file, | pipe) are kept.
bool AttachParentConsole() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return false;
    for (DWORD which : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
        HANDLE h = GetStdHandle(which);
        if (!h || h == INVALID_HANDLE_VALUE || GetFileType(h) == FILE_TYPE_UNKNOWN) {
            SECURITY_ATTRIBUTES inheritable{sizeof inheritable, nullptr, TRUE}; // passed on to SharpSFV-cli.exe
            HANDLE c = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &inheritable, OPEN_EXISTING, 0, nullptr);
            if (c != INVALID_HANDLE_VALUE) SetStdHandle(which, c);
        }
    }
    return true;
}

#ifndef SHARPSFV_GUI_EMBED_HEADLESS
// "SharpSFV.exe -headless ..." runs SharpSFV-cli.exe from the same folder with the same
// arguments, console and exit code. Keeping the command-line mode in one executable instead of
// two keeps SharpSFV.exe small (build with -DSHARPSFV_GUI_EMBED_HEADLESS=ON to embed it).
int RunHeadlessViaCli() {
    const bool attached = AttachParentConsole();
    const std::wstring cli = text::Widen(path::Combine(fs::ExecutableDirectory(), "SharpSFV-cli.exe"));
    if (GetFileAttributesW(cli.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const char* msg = "SharpSFV -headless needs SharpSFV-cli.exe in the same folder as SharpSFV.exe.\r\n";
        DWORD written = 0;
        HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
        if (!err || err == INVALID_HANDLE_VALUE ||
            !WriteFile(err, msg, static_cast<DWORD>(strlen(msg)), &written, nullptr))
            MessageBoxA(nullptr, msg, "SharpSFV", MB_OK | MB_ICONERROR);
        if (attached) FreeConsole();
        return 2;
    }
    // Pass the arguments through exactly as typed: everything after our own program name.
    const wchar_t* rest = GetCommandLineW();
    if (*rest == L'"') {
        ++rest;
        while (*rest && *rest != L'"') ++rest;
        if (*rest) ++rest;
    } else {
        while (*rest && *rest != L' ' && *rest != L'\t') ++rest;
    }
    std::wstring cmd = L"\"" + cli + L"\"" + rest;

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    SetConsoleCtrlHandler(nullptr, TRUE); // Ctrl+C is the child's to handle (it exits with code 3)
    PROCESS_INFORMATION pi{};
    DWORD code = 2;
    if (CreateProcessW(cli.c_str(), cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    if (attached) FreeConsole();
    return static_cast<int>(code);
}
#endif

int Run(const std::vector<std::string>& args, int show) {
    if (HasFlag(args, "-headless")) {
#ifdef SHARPSFV_GUI_EMBED_HEADLESS
        const bool attached = AttachParentConsole();
        const int code = RunHeadless(args, fs::ExecutableDirectory());
        if (attached) FreeConsole();
        return code;
#else
        return RunHeadlessViaCli();
#endif
    }

    const bool create = HasFlag(args, "-create");
    if (create && !gui::AcquireCreateModeMutex()) {
        if (gui::ForwardToPrimary(args)) return 0;
        // The primary could not be reached (it may have just exited): take over.
        if (!gui::AcquireCreateModeMutex()) return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES |
                                             ICC_STANDARD_CLASSES | ICC_LINK_CLASS | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    int code = 1;
    {
        gui::MainWindow window(args, create);
        if (window.Create(show)) code = window.RunMessageLoop();
    }
    CoUninitialize();
    return code;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(text::Narrow(argv[i]));
    LocalFree(argv);
    try {
        return Run(args, show);
    } catch (const std::exception& e) {
        MessageBoxW(nullptr, text::Widen(std::string("Critical Startup Error:\n") + e.what()).c_str(),
                    L"SharpSFV Fatal Error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
