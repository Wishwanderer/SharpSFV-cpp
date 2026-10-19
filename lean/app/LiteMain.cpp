// SharpSFV Lite - entry point (SharpSFV-Lite.exe; see lite/LiteVerify.h).
// No C runtime start-up code in the shipped Windows build, like SharpSFV-cli.exe; nothing
// here relies on global constructors.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-06: added (SharpSFV Lite).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "../base/System.h"
#include "../lite/LitePlatform.h"
#include "../lite/LiteVerify.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

int LiteMain(const sfv::Vec<sfv::Str>& args) {
    using namespace sfv;
    lite::Options opt;
    Str error;
    if (!lite::ParseArgs(args, opt, error)) {
        if (error.Empty()) { // -? / -help
            console::Write(lite::UsageText());
            return lite::kExitFailed;
        }
        if (!opt.statusPath.Empty()) { // installer mode: report through the status file
            lite::WriteFailedStatus(opt.statusPath, error);
        } else {
            console::WriteError("SharpSFV-Lite: " + error);
            console::WriteError("Run SharpSFV-Lite -? for help.");
        }
        return lite::kExitFailed;
    }
    const bool consoleMode = opt.statusPath.Empty();
    if (consoleMode) InstallInterruptHandler();
    lite::NoControl noControl;
    lite::plat::ProcessParentWatch parent;
    int code;
    if (!opt.control.Empty()) {
        lite::plat::EventControl events(opt.control);
        code = lite::Run(opt, events, parent);
    } else {
        code = lite::Run(opt, noControl, parent);
    }
    if (consoleMode) RemoveInterruptHandler();
    return code;
}

} // namespace

#ifdef _WIN32

#ifdef SFV_FREESTANDING
extern "C" void mainCRTStartup() {
    int code;
    {
        const sfv::Vec<sfv::Str> args = sfv::CommandLineArguments();
        code = LiteMain(args);
    }
    ExitProcess(static_cast<UINT>(code));
}
#else
int main() { return LiteMain(sfv::CommandLineArguments()); }
#endif

#else

int main(int argc, char** argv) {
    sfv::Vec<sfv::Str> args;
    for (int i = 1; i < argc; ++i) args.Push(sfv::Str(argv[i]));
    return LiteMain(args);
}

#endif
