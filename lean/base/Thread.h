// SharpSFV (lean C++ rewrite) - threads, locks and condition variables.
// Port of src/core/Threading.* (which wraps the same Windows primitives for the standard
// build): SRWLOCK, CONDITION_VARIABLE and CreateThread on Windows, pthreads elsewhere.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "Base.h"

#ifndef _WIN32
#include <pthread.h>
#endif

namespace sfv {

class Mutex {
public:
    Mutex();
    ~Mutex();
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    void Lock();
    void Unlock();

private:
    friend class CondVar;
#ifdef _WIN32
    void* srw_ = nullptr; // SRWLOCK (one pointer, SRWLOCK_INIT = null)
#else
    pthread_mutex_t m_;
#endif
};

class LockGuard {
public:
    explicit LockGuard(Mutex& m) : m_(m) { m_.Lock(); }
    ~LockGuard() { m_.Unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    Mutex& m_;
};

class CondVar {
public:
    CondVar();
    ~CondVar();
    CondVar(const CondVar&) = delete;
    CondVar& operator=(const CondVar&) = delete;
    void Wait(Mutex& m); // m must be locked; may wake spuriously (callers loop)
    void NotifyOne();
    void NotifyAll();

private:
#ifdef _WIN32
    void* cv_ = nullptr; // CONDITION_VARIABLE
#else
    pthread_cond_t c_;
#endif
};

class Thread {
public:
    Thread() = default;
    ~Thread() { Join(); }
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    // Runs fn(arg) on a new thread. False if the thread could not be created.
    bool Start(void (*fn)(void*), void* arg);
    // Runs f() on a new thread; f must outlive the thread (Join before it goes away).
    template <class F> bool Start(F& f) {
        return Start([](void* p) { (*static_cast<F*>(p))(); }, &f);
    }
    void Join();
    bool Running() const { return started_; }

private:
    void (*fn_)(void*) = nullptr;
    void* arg_ = nullptr;
    bool started_ = false;
#ifdef _WIN32
    void* handle_ = nullptr;
    static unsigned long __attribute__((stdcall)) Entry(void* self);
#else
    pthread_t t_{};
    static void* Entry(void* self);
#endif
};

// Logical processors available to the program (Environment.ProcessorCount); at least 1.
int ProcessorCount();

} // namespace sfv
