// SharpSFV (lean C++ rewrite) - single instance for Explorer's "Create Checksum" (see
// SingleInstance.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "SingleInstance.h"

#ifndef PIPE_REJECT_REMOTE_CLIENTS
#define PIPE_REJECT_REMOTE_CLIENTS 0x00000008
#endif

namespace sfv::gui {

namespace {

text::WStr PipeName() {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    return W("\\\\.\\pipe\\SharpSFV_Pipe_" + ToStr(static_cast<i64>(session)));
}

bool Load(const bool& b) { return __atomic_load_n(&b, __ATOMIC_ACQUIRE); }
void Store(bool& b, bool v) { __atomic_store_n(&b, v, __ATOMIC_RELEASE); }

} // namespace

bool AcquireCreateModeMutex() {
    static HANDLE mutex = nullptr; // held for the life of the process
    if (mutex) return true;
    HANDLE h = CreateMutexW(nullptr, TRUE, L"SharpSFV_Instance_Mutex");
    if (!h) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another instance owns it, unless it has exited and abandoned it.
        const DWORD r = WaitForSingleObject(h, 0);
        if (r != WAIT_OBJECT_0 && r != WAIT_ABANDONED) {
            CloseHandle(h);
            return false;
        }
    }
    mutex = h;
    return true;
}

bool ForwardToPrimary(const Vec<Str>& args) {
    Str payload;
    for (const Str& a : args) payload += a + "\n";
    const text::WStr name = PipeName();
    const ULONGLONG deadline = GetTickCount64() + 3000;
    while (GetTickCount64() < deadline) {
        HANDLE h = CreateFileW(name.C(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            const BOOL ok = WriteFile(h, payload.Data(), static_cast<DWORD>(payload.Size()), &written, nullptr);
            CloseHandle(h);
            return ok && written == payload.Size();
        }
        if (GetLastError() == ERROR_PIPE_BUSY)
            WaitNamedPipeW(name.C(), 500);
        else
            Sleep(50); // the primary has not created the pipe yet
    }
    return false;
}

void CreateModeServer::ThreadMain(void* self) {
    auto* s = static_cast<CreateModeServer*>(self);
    s->Run();
    Store(s->finished_, true);
}

void CreateModeServer::Start(HWND notify, UINT message) {
    if (thread_.Running()) return;
    notify_ = notify;
    message_ = message;
    Store(stop_, false);
    Store(finished_, false);
    if (!thread_.Start(&ThreadMain, this)) Store(finished_, true);
}

void CreateModeServer::Stop() {
    if (!thread_.Running()) return;
    Store(stop_, true);
    // Unblock ConnectNamedPipe with throw-away connections. Repeat until the thread has left:
    // a single attempt can land between its stop check and its next CreateNamedPipeW.
    const text::WStr name = PipeName();
    for (int i = 0; i < 250 && !Load(finished_); ++i) {
        HANDLE h = CreateFileW(name.C(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        if (!Load(finished_)) Sleep(20);
    }
    thread_.Join();
}

Vec<Str> CreateModeServer::TakePaths() {
    LockGuard g(mutex_);
    Vec<Str> out = Move(paths_);
    paths_ = Vec<Str>();
    return out;
}

void CreateModeServer::Run() {
    const text::WStr name = PipeName();
    while (!Load(stop_)) {
        HANDLE pipe = CreateNamedPipeW(name.C(), PIPE_ACCESS_INBOUND,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                       PIPE_UNLIMITED_INSTANCES, 0, 64 * 1024, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(100);
            continue;
        }
        const bool connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (Load(stop_)) {
            CloseHandle(pipe);
            break;
        }
        Str data;
        if (connected) {
            char buf[4096];
            DWORD got = 0;
            while (ReadFile(pipe, buf, sizeof buf, &got, nullptr) && got > 0) data.Append(StrView(buf, got));
            DisconnectNamedPipe(pipe);
        }
        CloseHandle(pipe);

        Vec<Str> lines;
        for (const Str& l : text::SplitLines(data)) {
            const StrView t = text::Trim(l);
            if (!t.Empty() && t[0] != '-') lines.Push(Str(t));
        }
        if (!lines.Empty()) {
            {
                LockGuard g(mutex_);
                for (Str& l : lines) paths_.Push(Move(l));
            }
            PostMessageW(notify_, message_, 0, 0);
        }
    }
}

} // namespace sfv::gui
