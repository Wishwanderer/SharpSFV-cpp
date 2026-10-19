// SharpSFV (C++ port) - single instance for Explorer's "Create Checksum" with several items
// selected. Ported from Program.cs (mutex + SendArgsToPrimaryInstance) and StartPipeServer
// in Form1.cs: the first instance owns a mutex and listens on a named pipe; later instances
// send it their paths and exit.
//
// Changes: the pipe name includes the session ID (pipe names are machine-wide while the
// mutex is per session, so two logged-in users could receive each other's files), remote
// pipe clients are rejected, several senders are served (the C# server allowed one
// instance), and a sender that cannot reach the primary takes over as primary instead of
// dropping its file.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "Win32Util.h"

#include <atomic>
#include "Threading.h"
#include <string>
#include <vector>

namespace sfv::gui {

// True if this process now owns the create-mode mutex (it is the primary instance).
bool AcquireCreateModeMutex();

// Sends paths to the primary instance. False if it could not be reached within ~3 s.
bool ForwardToPrimary(const std::vector<std::string>& args);

class CreateModeServer {
public:
    CreateModeServer() = default;
    ~CreateModeServer() { Stop(); }
    CreateModeServer(const CreateModeServer&) = delete;
    CreateModeServer& operator=(const CreateModeServer&) = delete;

    // Starts listening; posts `message` to `notify` whenever new paths are queued.
    void Start(HWND notify, UINT message);
    void Stop();
    std::vector<std::string> TakePaths();

private:
    void Run();

    Thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> finished_{true};
    Mutex mutex_;
    std::vector<std::string> paths_;
    HWND notify_ = nullptr;
    UINT message_ = 0;
};

} // namespace sfv::gui
