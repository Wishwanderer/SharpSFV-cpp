// SharpSFV (lean C++ rewrite) - the job queue (Job Queue mode).
// Port of src/core/JobStore.*: each job is a shared, reference-counted object, so the worker
// keeps updating the right job while the UI removes finished ones (a bug of the C# arrays).
// IDs are monotonic, so "<id>.<name>.ext" outputs never collide.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "../base/Str.h"
#include "../base/Thread.h"
#include "../base/Vec.h"
#include "Types.h"

namespace sfv {

class Job {
public:
    int id = 0;
    Str name;         // display name (usually the folder name)
    Str rootPath;     // where the checksum file is written
    Vec<Str> inputs;  // raw dropped paths, scanned when the job starts

    // Shared between the worker and the UI.
    double Progress() const;
    void SetProgress(double p);
    JobStatus Status() const { return static_cast<JobStatus>(__atomic_load_n(&status_, __ATOMIC_ACQUIRE)); }
    void SetStatus(JobStatus s) { __atomic_store_n(&status_, static_cast<int>(s), __ATOMIC_RELEASE); }
    bool CompareExchangeStatus(JobStatus expected, JobStatus desired);
    Str OutputPath() const;
    Str TimeStr() const;
    void SetOutputPath(StrView s);
    void SetTimeStr(StrView s);

    void AddRef() { __atomic_fetch_add(&refs_, 1, __ATOMIC_RELAXED); }
    void Release() {
        if (__atomic_sub_fetch(&refs_, 1, __ATOMIC_ACQ_REL) == 0) delete this;
    }

private:
    i64 progressBits_ = 0; // a double, stored as its bits for atomic access
    int status_ = 0;
    int refs_ = 1;
    mutable Mutex mutex_;
    Str outputPath_, timeStr_;
};

// Owning handle to a Job (std::shared_ptr<Job> for one class).
class JobRef {
public:
    JobRef() = default;
    explicit JobRef(Job* adopt) : p_(adopt) {} // takes over one reference
    JobRef(const JobRef& o) : p_(o.p_) {
        if (p_) p_->AddRef();
    }
    JobRef(JobRef&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    JobRef& operator=(JobRef o) noexcept {
        Swap(p_, o.p_);
        return *this;
    }
    ~JobRef() {
        if (p_) p_->Release();
    }
    Job* operator->() const { return p_; }
    Job& operator*() const { return *p_; }
    explicit operator bool() const { return p_ != nullptr; }
    Job* Get() const { return p_; }

private:
    Job* p_ = nullptr;
};

class JobStore {
public:
    int Add(StrView name, StrView rootPath, Vec<Str> inputs); // returns the new job's index
    int Count() const;
    JobRef Get(int index) const; // empty if out of range
    JobRef NextQueued() const;   // first job with status Queued
    void RemoveCompleted();      // drops every job marked Done
    void Clear();

private:
    mutable Mutex mutex_;
    Vec<JobRef> jobs_;
    int nextId_ = 1;
};

} // namespace sfv
