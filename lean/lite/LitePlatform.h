// SharpSFV Lite - the few operating-system services the run loop needs (see LiteVerify.h):
// sleeping, local time, the reason a file operation failed, replacing the status file in one
// step, the named Pause / Stop events and watching the parent process.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-06: added (SharpSFV Lite).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "LiteVerify.h"

namespace sfv::lite::plat {

void SleepMs(u32 ms);
Str NowText(); // local time, "2026-10-06 14:02:11"

// The reason the last file operation on this thread failed (Windows: the system message for
// GetLastError(); POSIX: strerror(errno)). Call it right after the failing call.
Str LastErrorText();

// Moves `from` over `to` in one step (readers see the old or the new file, never a mix).
// One attempt: false when a reader holds `to` open; the caller tries again on its next write.
bool MoveReplace(StrView from, StrView to);

// Windows: the events Local\<name>.pause and Local\<name>.stop (manual reset; created or
// opened, so either side may come first). POSIX test build: the files <tmp>/<name>.pause and
// <tmp>/<name>.stop (present = set).
class EventControl final : public Control {
public:
    explicit EventControl(StrView name);
    ~EventControl() override;
    EventControl(const EventControl&) = delete;
    EventControl& operator=(const EventControl&) = delete;
    void Poll(bool& pause, bool& stop) override;
    bool StartedOk() const override { return ok_; }

private:
    bool ok_ = false;
#ifdef _WIN32
    void* pause_ = nullptr;
    void* stop_ = nullptr;
#else
    Str pausePath_, stopPath_;
#endif
};

// The process that started this one (Windows: found through a process snapshot and opened
// for SYNCHRONIZE; POSIX: getppid() changing).
class ProcessParentWatch final : public ParentWatch {
public:
    ProcessParentWatch();
    ~ProcessParentWatch() override;
    ProcessParentWatch(const ProcessParentWatch&) = delete;
    ProcessParentWatch& operator=(const ProcessParentWatch&) = delete;
    bool ParentGone() override;

private:
#ifdef _WIN32
    void* parent_ = nullptr;
#else
    int ppid_ = 0;
#endif
};

} // namespace sfv::lite::plat
