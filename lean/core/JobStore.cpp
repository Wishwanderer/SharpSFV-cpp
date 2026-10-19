// SharpSFV (lean C++ rewrite) - the job queue (see JobStore.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "JobStore.h"

namespace sfv {

double Job::Progress() const {
    const i64 bits = __atomic_load_n(&progressBits_, __ATOMIC_RELAXED);
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

void Job::SetProgress(double p) {
    i64 bits;
    memcpy(&bits, &p, sizeof bits);
    __atomic_store_n(&progressBits_, bits, __ATOMIC_RELAXED);
}

bool Job::CompareExchangeStatus(JobStatus expected, JobStatus desired) {
    int e = static_cast<int>(expected);
    return __atomic_compare_exchange_n(&status_, &e, static_cast<int>(desired), false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

Str Job::OutputPath() const {
    LockGuard g(mutex_);
    return outputPath_;
}
Str Job::TimeStr() const {
    LockGuard g(mutex_);
    return timeStr_;
}
void Job::SetOutputPath(StrView s) {
    LockGuard g(mutex_);
    outputPath_ = s;
}
void Job::SetTimeStr(StrView s) {
    LockGuard g(mutex_);
    timeStr_ = s;
}

int JobStore::Add(StrView name, StrView rootPath, Vec<Str> inputs) {
    Job* job = new Job;
    job->name = name;
    job->rootPath = rootPath;
    job->inputs = Move(inputs);
    LockGuard g(mutex_);
    job->id = nextId_++;
    jobs_.Push(JobRef(job));
    return static_cast<int>(jobs_.Size()) - 1;
}

int JobStore::Count() const {
    LockGuard g(mutex_);
    return static_cast<int>(jobs_.Size());
}

JobRef JobStore::Get(int index) const {
    LockGuard g(mutex_);
    if (index < 0 || index >= static_cast<int>(jobs_.Size())) return {};
    return jobs_[static_cast<usize>(index)];
}

JobRef JobStore::NextQueued() const {
    LockGuard g(mutex_);
    for (const JobRef& j : jobs_)
        if (j->Status() == JobStatus::Queued) return j;
    return {};
}

void JobStore::RemoveCompleted() {
    LockGuard g(mutex_);
    Vec<JobRef> kept;
    for (JobRef& j : jobs_)
        if (j->Status() != JobStatus::Done) kept.Push(Move(j));
    jobs_ = Move(kept);
}

void JobStore::Clear() {
    LockGuard g(mutex_);
    jobs_.Clear();
}

} // namespace sfv
