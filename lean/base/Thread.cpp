// SharpSFV (lean C++ rewrite) - threads, locks and condition variables (see Thread.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Thread.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace sfv {

#ifdef _WIN32

static_assert(sizeof(SRWLOCK) == sizeof(void*) && sizeof(CONDITION_VARIABLE) == sizeof(void*));

Mutex::Mutex() = default;
Mutex::~Mutex() = default;
void Mutex::Lock() { AcquireSRWLockExclusive(reinterpret_cast<PSRWLOCK>(&srw_)); }
void Mutex::Unlock() { ReleaseSRWLockExclusive(reinterpret_cast<PSRWLOCK>(&srw_)); }

CondVar::CondVar() = default;
CondVar::~CondVar() = default;
void CondVar::Wait(Mutex& m) {
    SleepConditionVariableSRW(reinterpret_cast<PCONDITION_VARIABLE>(&cv_), reinterpret_cast<PSRWLOCK>(&m.srw_), INFINITE, 0);
}
void CondVar::NotifyOne() { WakeConditionVariable(reinterpret_cast<PCONDITION_VARIABLE>(&cv_)); }
void CondVar::NotifyAll() { WakeAllConditionVariable(reinterpret_cast<PCONDITION_VARIABLE>(&cv_)); }

unsigned long __attribute__((stdcall)) Thread::Entry(void* self) {
    Thread* t = static_cast<Thread*>(self);
    t->fn_(t->arg_);
    return 0;
}

bool Thread::Start(void (*fn)(void*), void* arg) {
    if (started_) return false;
    fn_ = fn, arg_ = arg;
    // CreateThread is safe here: the C runtime in use is the msvcrt.dll DLL, which sets up
    // its per-thread data itself (the _beginthreadex rule applies to a static C runtime).
    handle_ = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(&Entry), this, 0, nullptr);
    started_ = handle_ != nullptr;
    return started_;
}

void Thread::Join() {
    if (!started_) return;
    WaitForSingleObject(handle_, INFINITE);
    CloseHandle(handle_);
    handle_ = nullptr;
    started_ = false;
}

int ProcessorCount() {
    const DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS); // all groups, unlike GetSystemInfo
    return n > 0 ? static_cast<int>(n) : 1;
}

#else

Mutex::Mutex() { pthread_mutex_init(&m_, nullptr); }
Mutex::~Mutex() { pthread_mutex_destroy(&m_); }
void Mutex::Lock() { pthread_mutex_lock(&m_); }
void Mutex::Unlock() { pthread_mutex_unlock(&m_); }

CondVar::CondVar() { pthread_cond_init(&c_, nullptr); }
CondVar::~CondVar() { pthread_cond_destroy(&c_); }
void CondVar::Wait(Mutex& m) { pthread_cond_wait(&c_, &m.m_); }
void CondVar::NotifyOne() { pthread_cond_signal(&c_); }
void CondVar::NotifyAll() { pthread_cond_broadcast(&c_); }

void* Thread::Entry(void* self) {
    Thread* t = static_cast<Thread*>(self);
    t->fn_(t->arg_);
    return nullptr;
}

bool Thread::Start(void (*fn)(void*), void* arg) {
    if (started_) return false;
    fn_ = fn, arg_ = arg;
    started_ = pthread_create(&t_, nullptr, &Entry, this) == 0;
    return started_;
}

void Thread::Join() {
    if (!started_) return;
    pthread_join(t_, nullptr);
    started_ = false;
}

int ProcessorCount() {
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? static_cast<int>(n) : 1;
}

#endif

} // namespace sfv
