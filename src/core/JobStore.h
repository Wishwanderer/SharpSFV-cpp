// SharpSFV (C++ port) - job queue store.
// Ported from Models/JobStore.cs.
//
// The C# version uses parallel arrays addressed by index. That has a bug: the worker keeps
// the index of the running job, so "Clear Completed Jobs" while a job runs shifts the
// arrays and the worker then updates the wrong job. Here each job is a shared object the
// worker holds directly, so removal of other jobs cannot redirect its updates. Jobs are
// few (tens), so the structure-of-arrays layout bought nothing here.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
// Modified 2026-10-03: native mutex (Threading.h).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "Types.h"

#include <atomic>
#include <memory>
#include "Threading.h"
#include <string>
#include <vector>

namespace sfv {

struct Job {
    int id = 0;
    std::string name;                // display name (usually the folder name)
    std::string rootPath;            // where the checksum file is written
    std::vector<std::string> inputs; // raw dropped paths, scanned when the job starts

    std::atomic<double> progress{0.0};
    std::atomic<JobStatus> status{JobStatus::Queued};

    // Written by the worker, read by the UI.
    std::string OutputPath() const;
    std::string TimeStr() const;
    void SetOutputPath(std::string s);
    void SetTimeStr(std::string s);

private:
    mutable Mutex mutex_;
    std::string outputPath_;
    std::string timeStr_;
};

class JobStore {
public:
    // Thread-safe. Returns the new job's index.
    int Add(std::string name, std::string rootPath, std::vector<std::string> inputs);
    int Count() const;
    std::shared_ptr<Job> Get(int index) const; // nullptr if out of range
    std::shared_ptr<Job> NextQueued() const;   // first job with status Queued
    void RemoveCompleted();                    // drops every job marked Done
    void Clear();

private:
    mutable Mutex mutex_;
    std::vector<std::shared_ptr<Job>> jobs_;
    // The C# version used Count + 1, so IDs repeated after "Clear Completed" and a new job
    // could overwrite an earlier job's "<id>.<name>.ext" output. IDs are now monotonic.
    int nextId_ = 1;
};

} // namespace sfv
