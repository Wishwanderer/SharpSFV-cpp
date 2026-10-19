// SharpSFV (C++ port) - threads, mutexes and condition variables.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the C++20 port.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
//
// On Windows these are thin wrappers over the native primitives (threads, SRW locks,
// condition variables). With MinGW, std::thread/std::mutex/std::condition_variable link
// libstdc++'s thread support plus its std::system_error machinery (~50 KB per executable)
// for what the OS already provides. The interfaces are the subset of the std types the
// program uses, so they work with std::lock_guard / std::unique_lock. Elsewhere they are the
// std types.
#pragma once

#include <functional>
#include <mutex> // std::lock_guard, std::unique_lock

#ifndef _WIN32
#include <condition_variable>
#include <thread>
#endif

namespace sfv {

#ifdef _WIN32

class Mutex {
public:
    Mutex() = default;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    void lock() noexcept;
    void unlock() noexcept;
    bool try_lock() noexcept;

private:
    friend class CondVar;
    void* srw_ = nullptr; // SRWLOCK (SRWLOCK_INIT is all zeros)
};

class CondVar {
public:
    CondVar() = default;
    CondVar(const CondVar&) = delete;
    CondVar& operator=(const CondVar&) = delete;
    void notify_one() noexcept;
    void notify_all() noexcept;
    void wait(std::unique_lock<Mutex>& lock) noexcept; // the lock must be held
    template <class Pred>
    void wait(std::unique_lock<Mutex>& lock, Pred pred) {
        while (!pred()) wait(lock);
    }

private:
    void* cv_ = nullptr; // CONDITION_VARIABLE (CONDITION_VARIABLE_INIT is all zeros)
};

class Thread {
public:
    Thread() noexcept = default;
    template <class F>
    explicit Thread(F&& f) {
        Start(new std::function<void()>(std::forward<F>(f)));
    }
    Thread(Thread&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    Thread& operator=(Thread&& other) noexcept;
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    ~Thread(); // like std::thread: destroying a joinable thread terminates

    bool joinable() const noexcept { return handle_ != nullptr; }
    void join();
    static unsigned hardware_concurrency() noexcept;

private:
    void Start(std::function<void()>* fn); // takes ownership; throws if no thread can be created
    void* handle_ = nullptr;
};

#else

using Mutex = std::mutex;
using CondVar = std::condition_variable;
using Thread = std::thread;

#endif

void SleepMs(unsigned ms);

} // namespace sfv
