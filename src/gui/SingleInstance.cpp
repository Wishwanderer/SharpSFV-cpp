// SharpSFV (C++ port) - single instance for Explorer's "Create Checksum".
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "SingleInstance.h"

#include "Text.h"

#ifndef PIPE_REJECT_REMOTE_CLIENTS
#define PIPE_REJECT_REMOTE_CLIENTS 0x00000008
#endif

namespace sfv::gui {

namespace {

std::wstring PipeName() {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    return L"\\\\.\\pipe\\SharpSFV_Pipe_" + text::Widen(std::to_string(session)); // to_wstring links a wide printf
}

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

bool ForwardToPrimary(const std::vector<std::string>& args) {
    std::string payload;
    for (const auto& a : args) payload += a + "\n";
    const std::wstring name = PipeName();
    const ULONGLONG deadline = GetTickCount64() + 3000;
    while (GetTickCount64() < deadline) {
        HANDLE h = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            const BOOL ok = WriteFile(h, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr);
            CloseHandle(h);
            return ok && written == payload.size();
        }
        if (GetLastError() == ERROR_PIPE_BUSY)
            WaitNamedPipeW(name.c_str(), 500);
        else
            Sleep(50); // the primary has not created the pipe yet
    }
    return false;
}

void CreateModeServer::Start(HWND notify, UINT message) {
    if (thread_.joinable()) return;
    notify_ = notify;
    message_ = message;
    stop_.store(false);
    finished_.store(false);
    thread_ = Thread([this] {
        Run();
        finished_.store(true);
    });
}

void CreateModeServer::Stop() {
    if (!thread_.joinable()) return;
    stop_.store(true);
    // Unblock ConnectNamedPipe with throw-away connections. Repeat until the thread has left:
    // a single attempt can land between its stop check and its next CreateNamedPipeW, and
    // the thread would then wait for a client forever (and join() with it).
    const std::wstring name = PipeName();
    for (int i = 0; i < 250 && !finished_.load(); ++i) {
        HANDLE h = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        if (!finished_.load()) Sleep(20);
    }
    thread_.join();
}

std::vector<std::string> CreateModeServer::TakePaths() {
    std::lock_guard lock(mutex_);
    std::vector<std::string> out;
    out.swap(paths_);
    return out;
}

void CreateModeServer::Run() {
    const std::wstring name = PipeName();
    while (!stop_.load()) {
        HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                       PIPE_UNLIMITED_INSTANCES, 0, 64 * 1024, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(100);
            continue;
        }
        const bool connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (stop_.load()) {
            CloseHandle(pipe);
            break;
        }
        std::string data;
        if (connected) {
            char buf[4096];
            DWORD got = 0;
            while (ReadFile(pipe, buf, sizeof buf, &got, nullptr) && got > 0) data.append(buf, got);
            DisconnectNamedPipe(pipe);
        }
        CloseHandle(pipe);

        std::vector<std::string> lines;
        for (const auto& l : text::SplitLines(data)) {
            const std::string_view t = text::Trim(l);
            if (!t.empty() && !t.starts_with('-')) lines.emplace_back(t);
        }
        if (!lines.empty()) {
            {
                std::lock_guard lock(mutex_);
                paths_.insert(paths_.end(), lines.begin(), lines.end());
            }
            PostMessageW(notify_, message_, 0, 0);
        }
    }
}

} // namespace sfv::gui
