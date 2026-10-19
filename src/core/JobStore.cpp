// SharpSFV (C++ port) - job queue store.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "JobStore.h"

#include <algorithm>

namespace sfv {

std::string Job::OutputPath() const {
    std::lock_guard lock(mutex_);
    return outputPath_;
}
std::string Job::TimeStr() const {
    std::lock_guard lock(mutex_);
    return timeStr_;
}
void Job::SetOutputPath(std::string s) {
    std::lock_guard lock(mutex_);
    outputPath_ = std::move(s);
}
void Job::SetTimeStr(std::string s) {
    std::lock_guard lock(mutex_);
    timeStr_ = std::move(s);
}

int JobStore::Add(std::string name, std::string rootPath, std::vector<std::string> inputs) {
    auto job = std::make_shared<Job>();
    job->name = std::move(name);
    job->rootPath = std::move(rootPath);
    job->inputs = std::move(inputs);
    std::lock_guard lock(mutex_);
    job->id = nextId_++;
    jobs_.push_back(std::move(job));
    return static_cast<int>(jobs_.size()) - 1;
}

int JobStore::Count() const {
    std::lock_guard lock(mutex_);
    return static_cast<int>(jobs_.size());
}

std::shared_ptr<Job> JobStore::Get(int index) const {
    std::lock_guard lock(mutex_);
    if (index < 0 || index >= static_cast<int>(jobs_.size())) return nullptr;
    return jobs_[static_cast<std::size_t>(index)];
}

std::shared_ptr<Job> JobStore::NextQueued() const {
    std::lock_guard lock(mutex_);
    for (const auto& j : jobs_)
        if (j->status.load() == JobStatus::Queued) return j;
    return nullptr;
}

void JobStore::RemoveCompleted() {
    std::lock_guard lock(mutex_);
    std::erase_if(jobs_, [](const std::shared_ptr<Job>& j) { return j->status.load() == JobStatus::Done; });
}

void JobStore::Clear() {
    std::lock_guard lock(mutex_);
    jobs_.clear();
}

} // namespace sfv
