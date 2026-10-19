// SharpSFV (C++ port) - threads, mutexes and condition variables (see Threading.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the C++20 port.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Threading.h"

#include <exception>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>

static_assert(sizeof(void*) == sizeof(SRWLOCK) && sizeof(void*) == sizeof(CONDITION_VARIABLE));
#else
#include <chrono>
#endif

namespace sfv {

#ifdef _WIN32

namespace {
PSRWLOCK Srw(void*& p) { return reinterpret_cast<PSRWLOCK>(&p); }
PCONDITION_VARIABLE Cv(void*& p) { return reinterpret_cast<PCONDITION_VARIABLE>(&p); }

struct ThreadStartError final : std::exception {
    const char* what() const noexcept override { return "could not start a thread"; }
};

unsigned __stdcall ThreadMain(void* arg) {
    std::function<void()>* fn = static_cast<std::function<void()>*>(arg);
    try {
        (*fn)();
    } catch (...) {
        std::terminate(); // as std::thread does for an exception leaving the thread
    }
    delete fn;
    return 0;
}
} // namespace

void Mutex::lock() noexcept { AcquireSRWLockExclusive(Srw(srw_)); }
void Mutex::unlock() noexcept { ReleaseSRWLockExclusive(Srw(srw_)); }
bool Mutex::try_lock() noexcept { return TryAcquireSRWLockExclusive(Srw(srw_)) != 0; }

void CondVar::notify_one() noexcept { WakeConditionVariable(Cv(cv_)); }
void CondVar::notify_all() noexcept { WakeAllConditionVariable(Cv(cv_)); }
void CondVar::wait(std::unique_lock<Mutex>& lock) noexcept {
    SleepConditionVariableSRW(Cv(cv_), Srw(lock.mutex()->srw_), INFINITE, 0);
}

void Thread::Start(std::function<void()>* fn) {
    const std::uintptr_t h = _beginthreadex(nullptr, 0, ThreadMain, fn, 0, nullptr);
    if (h == 0) {
        delete fn;
        throw ThreadStartError();
    }
    handle_ = reinterpret_cast<void*>(h);
}

Thread& Thread::operator=(Thread&& other) noexcept {
    if (this != &other) {
        if (joinable()) std::terminate();
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

Thread::~Thread() {
    if (joinable()) std::terminate();
}

void Thread::join() {
    if (!handle_) return;
    WaitForSingleObject(static_cast<HANDLE>(handle_), INFINITE);
    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
}

unsigned Thread::hardware_concurrency() noexcept {
    const DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS); // all groups, unlike GetSystemInfo
    return n ? static_cast<unsigned>(n) : 1u;
}

void SleepMs(unsigned ms) { Sleep(ms); }

#else

void SleepMs(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

#endif

} // namespace sfv
