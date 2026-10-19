// SharpSFV (lean C++ rewrite) - Job Queue mode (JobRunner) and Engine::CountFiles.
// Port of the JobRunner and CountFiles parts of src/core/Engine.cpp. Kept in their own file
// so the console program, which does not use them, does not carry them.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Engine.h"

#include "../base/FileSystem.h"
#include "../base/Path.h"
#include "../base/System.h"
#include "DriveStore.h"
#include "Filter.h"

namespace sfv {

int Engine::CountFiles(const Vec<Str>& paths, const HashRunOptions& opt) const {
    int count = 0;
    auto allowed = [&](StrView full) {
        return !opt.applyFilters || IsFileAllowed(path::GetFileName(full), opt.includePattern, opt.excludePattern);
    };
    for (const Str& p : paths) {
        if (IsCancelled()) break;
        if (fs::FileExists(p)) {
            if (allowed(p)) ++count;
        } else if (fs::DirectoryExists(p)) {
            fs::EnumerateFiles(p, opt.recursive, [&](const Str& f, i64) {
                if (allowed(f)) ++count;
                return !IsCancelled();
            });
        }
    }
    return count;
}

// ------------------------------------------------------------------ JobRunner

void JobRunner::Run(JobStore& jobs, Engine& engine, const AppSettings& settings, HashType algo, bool createMode,
                    const DriveResolver& drives, const Hooks& hooks) {
    auto changed = [&] {
        if (hooks.jobsChanged) hooks.jobsChanged(hooks.ctx);
    };
    auto log = [&](StrView s) {
        if (hooks.log) hooks.log(hooks.ctx, s);
    };

    while (JobRef job = jobs.NextQueued()) {
        // --- Scanning phase: exact count first so the percentage is accurate.
        job->SetStatus(JobStatus::Scanning);
        changed();
        log("Scanning Job: " + job->name);

        HashRunOptions opt;
        opt.algo = algo;
        opt.applyFilters = settings.showAdvancedBar;
        opt.includePattern = settings.includePattern;
        opt.excludePattern = settings.excludePattern;
        opt.recursive = (createMode || settings.showAdvancedBar) ? settings.scanRecursive : true;

        const int exactTotal = engine.CountFiles(job->inputs, opt);
        if (engine.IsCancelled()) {
            job->SetStatus(JobStatus::Error);
            job->SetTimeStr("Cancelled");
            changed();
            log("Job Cancelled: " + job->name);
            break;
        }

        job->SetStatus(JobStatus::InProgress);
        changed();

        engine.Throttle().ResetMetrics();
        const i64 start = TickMs();

        Str probe;
        if (!job->inputs.Empty()) {
            const Str& first = job->inputs[0];
            probe = fs::DirectoryExists(first) ? first : path::GetDirectoryName(first).ValueOr(Str());
        }
        const DriveDecision decision = drives.Resolve(settings.processingMode, probe.Empty() ? job->rootPath : probe);
        opt.isHdd = decision.isHdd;
        log("Drive: " + DescribeDecision(decision));

        FileStore& store = engine.Store();
        store.Clear();

        i64 lastRepaint = TickMs(); // shared by the workers: __atomic only
        const RunResult r = engine.HashFiles(job->inputs, job->rootPath, opt, [&](const Counters& c) {
            if (exactTotal > 0) job->SetProgress(Min(100.0, static_cast<double>(c.completed) / exactTotal * 100.0));
            const i64 now = TickMs();
            i64 last = __atomic_load_n(&lastRepaint, __ATOMIC_RELAXED);
            if ((now - last >= 250 || c.completed == exactTotal) &&
                __atomic_compare_exchange_n(&lastRepaint, &last, now, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
                changed();
            if (hooks.progress) {
                Counters g = c;
                g.total = exactTotal;
                hooks.progress(hooks.ctx, g);
            }
        });

        if (r.cancelled) {
            job->SetStatus(JobStatus::Error);
            job->SetTimeStr("Cancelled");
            store.Clear();
            changed();
            log("Job Cancelled: " + job->name);
            break;
        }

        // Save "<id>.<name><ext>" next to the job's root.
        const Str fileName = ToStr(job->id) + "." + path::SanitizeFileName(job->name) + ExtensionForAlgo(algo);
        const Str fullSavePath = path::Combine(job->rootPath, fileName);
        const bool hasErrors = WriteJobFile(fullSavePath, store, settings, algo);

        const Str timeStr = ToStr(TickMs() - start) + " ms";
        job->SetTimeStr(timeStr);
        const JobStatus finalStatus = hasErrors ? JobStatus::Error : JobStatus::Done;
        job->SetOutputPath(fullSavePath);
        job->SetStatus(finalStatus);
        job->SetProgress(100.0);

        store.Clear();
        changed();
        log(Str("Job Finished: ") + ToString(finalStatus) + " (" + timeStr + ")");
    }
}

} // namespace sfv
