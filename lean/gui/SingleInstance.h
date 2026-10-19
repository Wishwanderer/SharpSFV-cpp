// SharpSFV (lean C++ rewrite) - single instance for Explorer's "Create Checksum" with several
// items selected. Port of src/gui/SingleInstance.*: the first instance owns a mutex and
// listens on a named pipe (per session, local clients only); later instances send it their
// paths and exit, or take over if the primary cannot be reached.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "../base/Thread.h"
#include "Win32Util.h"

namespace sfv::gui {

// True if this process now owns the create-mode mutex (it is the primary instance).
bool AcquireCreateModeMutex();

// Sends paths to the primary instance. False if it could not be reached within ~3 s.
bool ForwardToPrimary(const Vec<Str>& args);

class CreateModeServer {
public:
    CreateModeServer() = default;
    ~CreateModeServer() { Stop(); }
    CreateModeServer(const CreateModeServer&) = delete;
    CreateModeServer& operator=(const CreateModeServer&) = delete;

    // Starts listening; posts `message` to `notify` whenever new paths are queued.
    void Start(HWND notify, UINT message);
    void Stop();
    Vec<Str> TakePaths();

private:
    static void ThreadMain(void* self);
    void Run();

    Thread thread_;
    bool stop_ = false;    // __atomic
    bool finished_ = true; // __atomic
    Mutex mutex_;
    Vec<Str> paths_;
    HWND notify_ = nullptr;
    UINT message_ = 0;
};

} // namespace sfv::gui
