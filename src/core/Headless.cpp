// SharpSFV (C++ port) - headless (command-line) mode.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
// Modified 2026-10-03: CRC byte order, drive commands; native threads.
// Modified 2026-10-04: a missing SharpSFV.ini is created with the default settings.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Headless.h"

#include "AppSettings.h"
#include "ChecksumParser.h"
#include "ChecksumWriter.h"
#include "Console.h"
#include "DataStorage.h"
#include "DriveClassifier.h"
#include "DriveDetector.h"
#include "Engine.h"
#include "FileStore.h"
#include "FileSystem.h"
#include "PathUtil.h"
#include "Text.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <optional>
#include "Threading.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sfv {

namespace {

// ------------------------------------------------------------------ Ctrl+C

std::atomic<bool> g_interrupted{false};

#ifdef _WIN32
BOOL WINAPI CtrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        g_interrupted.store(true);
        return TRUE;
    }
    return FALSE;
}
#else
extern "C" void SigIntHandler(int) { g_interrupted.store(true); }
#endif

// Watches the interrupt flag and cancels the engine (signal handlers may not lock mutexes).
class InterruptWatcher {
public:
    explicit InterruptWatcher(Engine& e) : engine_(e) {
#ifdef _WIN32
        SetConsoleCtrlHandler(CtrlHandler, TRUE);
#else
        std::signal(SIGINT, SigIntHandler);
#endif
        thread_ = Thread([this] {
            while (!stop_.load()) {
                if (g_interrupted.load()) {
                    engine_.Cancel();
                    return;
                }
                SleepMs(50);
            }
        });
    }
    ~InterruptWatcher() {
        stop_.store(true);
        thread_.join();
#ifdef _WIN32
        SetConsoleCtrlHandler(CtrlHandler, FALSE);
#else
        std::signal(SIGINT, SIG_DFL);
#endif
    }

private:
    Engine& engine_;
    std::atomic<bool> stop_{false};
    Thread thread_;
};

// ------------------------------------------------------------------ Arguments

struct Args {
    std::vector<std::string> paths;
    bool create = false;
    bool help = false;
    std::string out;
    std::optional<HashType> algo;
    std::optional<ProcessingMode> mode;
    std::optional<Crc32ByteOrder> crcOrder;
    // Management commands (run instead of hashing).
    std::optional<std::string> dataStorage; // status | allow | deny | ask
    bool listDrives = false;
    std::string driveStatusPath;
    std::string driveSetPath, driveSetValue;
    std::string error;
};

constexpr const char* kProg = "SharpSFV-cli";

std::optional<HashType> ParseAlgoName(std::string_view s) {
    if (auto t = ParseHashType(s)) return t;
    const std::string f = text::FoldCase(s);
    if (f == "XXH3" || f == "XXHASH" || f == "XXHASH-3" || f == "XXH128") return HashType::XXHASH3;
    if (f == "CRC" || f == "CRC-32" || f == "SFV") return HashType::Crc32;
    if (f == "SHA-1") return HashType::SHA1;
    if (f == "SHA-256") return HashType::SHA256;
    return std::nullopt;
}

Args ParseArgs(const std::vector<std::string>& argv) {
    Args a;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        const std::string& s = argv[i];
        if (!s.starts_with('-')) { // the original treats every non-dash argument as a path
            a.paths.push_back(s);
            continue;
        }
        auto value = [&](const char* name) -> std::string {
            if (i + 1 >= argv.size()) {
                a.error = std::string("Missing value for ") + name;
                return {};
            }
            return argv[++i];
        };
        auto is = [&](const char* opt) { return text::EqualsIgnoreCase(s, opt); };
        if (is("-headless")) continue;
        if (is("-create")) a.create = true;
        else if (is("-out") || is("-o")) a.out = value("-out");
        else if (is("-algo")) {
            const std::string v = value("-algo");
            if (!v.empty() && !(a.algo = ParseAlgoName(v))) a.error = "Unknown algorithm: " + v;
        } else if (is("-mode")) {
            const std::string v = value("-mode");
            if (!v.empty() && !(a.mode = ParseProcessingMode(v))) a.error = "Unknown mode: " + v;
        } else if (is("-crc-order")) {
            const std::string v = value("-crc-order");
            if (!v.empty() && !(a.crcOrder = ParseCrc32ByteOrder(v)))
                a.error = "Unknown CRC-32 byte order: " + v + " (use standard or reversed)";
        } else if (is("-datastorage")) {
            a.dataStorage = "status";
            if (i + 1 < argv.size()) {
                const std::string& n = argv[i + 1];
                for (const char* c : {"status", "allow", "deny", "ask"})
                    if (text::EqualsIgnoreCase(n, c)) {
                        a.dataStorage = c;
                        ++i;
                    }
            }
        } else if (is("-drives")) {
            a.listDrives = true;
        } else if (is("-drive-status")) {
            a.driveStatusPath = value("-drive-status");
        } else if (is("-drive-set")) {
            a.driveSetPath = value("-drive-set");
            if (a.error.empty()) a.driveSetValue = value("-drive-set");
            if (a.error.empty() && !text::EqualsIgnoreCase(a.driveSetValue, "hdd") &&
                !text::EqualsIgnoreCase(a.driveSetValue, "ssd") && !text::EqualsIgnoreCase(a.driveSetValue, "auto"))
                a.error = "-drive-set needs hdd, ssd or auto after the path";
        } else if (is("-h") || is("-help") || is("--help") || s == "-?") {
            a.help = true;
        } else {
            console::WriteError("Ignoring unknown option: " + s);
        }
        if (!a.error.empty()) break;
    }
    return a;
}

// ------------------------------------------------------------------ Output

void PrintSummary(int total, int ok, int bad, int missing, std::int64_t ms) {
    // HandleCompletion (headless branch).
    console::WriteLine();
    console::WriteLine("------------------------------------------------");
    console::WriteLine("Total: " + std::to_string(total));
    console::WriteLine("OK: " + std::to_string(ok));
    console::WriteLine("BAD: " + std::to_string(bad));
    console::WriteLine("MISSING: " + std::to_string(missing));
    console::WriteLine("Time: " + std::to_string(ms) + " ms");
    console::WriteLine("------------------------------------------------");
}

void InstallConsolePresenter(ProgressThrottle& throttle, bool showStats) {
    throttle.SetShowThroughput(showStats);
    throttle.SetPresenter([&throttle, showStats](const ProgressInfo& p) {
        const double pct = p.total > 0 ? static_cast<double>(p.current) / p.total * 100.0 : 0.0;
        std::string line = "\r" + std::to_string(p.current) + "/" + std::to_string(p.total) + " (" + text::FormatF1(pct) +
                           "%) | OK: " + std::to_string(p.ok) + " | BAD: " + std::to_string(p.bad) +
                           " | MISSING: " + std::to_string(p.missing);
        if (showStats) line += " | " + p.speed;
        line += "   ";
        console::Write(line);
        throttle.Release();
    });
}

std::int64_t SinceMs(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

int ExitCodeFor(const RunResult& r) {
    if (r.cancelled) return kExitCancelled;
    return (r.bad > 0 || r.missing > 0) ? kExitProblems : kExitOk;
}

std::string Quote(const std::string& p) { return p.find(' ') == std::string::npos ? p : "\"" + p + "\""; }

// Decides HDD/SSD for a run and explains the decision (and how to fix a failed detection).
bool ResolveDrive(const DriveResolver& drives, const DataStorage& storage, const Args& args,
                  const AppSettings& settings, const std::string& probe) {
    const ProcessingMode mode = args.mode.value_or(settings.processingMode);
    const DriveDecision d = drives.Resolve(mode, probe, args.mode.has_value());
    std::string where = d.volume.mountPoint.empty() ? std::string() : " [" + d.volume.mountPoint + "]";
    console::WriteLine("Mode: " + DescribeDecision(d) + where);
    const bool automatic = d.source == DriveDecision::Source::Stored || d.source == DriveDecision::Source::DetectedSaved ||
                           d.source == DriveDecision::Source::DetectedNotSaved;
    if (automatic && d.detectionFailed) {
        console::WriteLine("  Automatic detection did not work for this drive (common with encrypted volumes).");
        if (storage.Allowed())
            console::WriteLine(std::string("  Set it yourself: ") + kProg + " -drive-set " + Quote(probe) + " hdd|ssd");
        else // a per-drive setting needs local data storage; the global overrides do not
            console::WriteLine("  Choose for this run with -mode hdd|ssd, or for all drives with ProcessingMode in SharpSFV.ini.");
    }
    if (d.source == DriveDecision::Source::DetectedNotSaved && storage.Consent() == DataConsent::NotAsked)
        console::WriteLine(std::string("  To detect each drive only once and keep per-drive settings: ") + kProg +
                           " -datastorage ask");
    return d.isHdd;
}

// ------------------------------------------------------------------ Modes

int RunVerify(const std::string& checkFile, const Args& args, const AppSettings& settings,
              const DriveResolver& drives, const DataStorage& storage) {
    // .sfv/.md5/... pick the algorithm; other extensions (.txt, .sum, .xxh) use -algo or
    // the configured default.
    const HashType algo = DetectAlgoFromExtension(checkFile, args.algo.value_or(settings.defaultAlgo));

    std::string err;
    auto parsed = ParseChecksumFile(checkFile, algo, &err);
    if (!parsed) {
        console::WriteLine("Error parsing: " + err);
        return kExitFailure;
    }

    const std::string baseFolder = path::GetDirectoryName(checkFile).value_or("");
    FileStore store;
    const std::vector<int> order = PopulateVerificationStore(store, *parsed, baseFolder, false);
    console::WriteLine("Verifying " + checkFile + " (" + std::to_string(store.Count()) + " files)...");
    const bool isHdd = ResolveDrive(drives, storage, args, settings, baseFolder.empty() ? std::string(".") : baseFolder);

    ProgressThrottle throttle;
    throttle.ResetMetrics();
    InstallConsolePresenter(throttle, settings.showThroughputStats);
    Engine engine(store, throttle);
    InterruptWatcher watcher(engine);

    HashRunOptions opt;
    opt.algo = algo;
    opt.isHdd = isHdd;

    const auto t0 = std::chrono::steady_clock::now();
    const RunResult r = engine.Verify(order, opt, [&](const Counters& c) {
        throttle.Update(c.completed, c.total, c.ok, c.bad, c.missing);
    });
    PrintSummary(r.completed, r.ok, r.bad, r.missing, SinceMs(t0));
    if (algo == HashType::Crc32 && r.crcStandardMatches + r.crcReversedMatches > 0) {
        console::WriteLine("CRC-32 byte order: " + std::to_string(r.crcStandardMatches) + " standard (QuickSFV-style), " +
                           std::to_string(r.crcReversedMatches) + " reversed (C# SharpSFV-style)");
        if (r.crcStandardMatches > 0 && r.crcReversedMatches > 0)
            console::WriteLine("Note: this file mixes both byte orders.");
    }
    if (r.cancelled) console::WriteLine("Cancelled.");
    return ExitCodeFor(r);
}

int RunCreate(const Args& args, AppSettings& settings, const DriveResolver& drives, const DataStorage& storage) {
    const std::vector<std::string>& paths = args.paths;
    if (args.crcOrder) settings.crc32ByteOrder = *args.crcOrder; // this run only (not saved)

    // Base directory: the folder itself, or the deepest folder shared by all parents.
    std::string baseDirectory;
    if (paths.size() == 1 && fs::DirectoryExists(paths[0])) {
        baseDirectory = paths[0];
    } else {
        std::vector<std::string> parents;
        for (const auto& p : paths) parents.push_back(path::GetDirectoryName(p).value_or(p));
        baseDirectory = path::FindCommonBasePath(parents);
    }

    const bool writeOutput = args.create || !args.out.empty();
    HashType algo = args.algo.value_or(settings.defaultAlgo);
    if (!args.algo && !args.out.empty()) algo = DetectAlgoFromExtension(args.out, algo);

    std::string outPath = args.out;
    if (writeOutput && outPath.empty()) {
        if (baseDirectory.empty()) {
            console::WriteError("The inputs share no common folder; use -out <file> to choose where to save.");
            return kExitFailure;
        }
        outPath = path::Combine(baseDirectory, std::string("checksums") + ExtensionForAlgo(algo));
    }

    std::string probe;
    if (!paths.empty())
        probe = fs::DirectoryExists(paths[0]) ? paths[0] : path::GetDirectoryName(paths[0]).value_or("");
    const bool isHdd = ResolveDrive(drives, storage, args, settings, probe.empty() ? std::string(".") : probe);

    FileStore store;
    ProgressThrottle throttle;
    throttle.ResetMetrics();
    InstallConsolePresenter(throttle, settings.showThroughputStats);
    Engine engine(store, throttle);
    InterruptWatcher watcher(engine);

    HashRunOptions opt;
    opt.algo = algo;
    opt.isHdd = isHdd;
    opt.applyFilters = settings.showAdvancedBar;
    opt.includePattern = settings.includePattern;
    opt.excludePattern = settings.excludePattern;
    opt.recursive = (args.create || settings.showAdvancedBar) ? settings.scanRecursive : true;
    opt.skipPath = outPath;

    const auto t0 = std::chrono::steady_clock::now();
    const RunResult r = engine.HashFiles(paths, baseDirectory, opt, [&](const Counters& c) {
        throttle.Update(c.completed, c.total, c.ok, c.bad, 0);
    });
    PrintSummary(r.completed, r.ok, r.bad, 0, SinceMs(t0));

    if (r.cancelled) {
        console::WriteLine("Cancelled.");
        return kExitCancelled;
    }
    if (writeOutput) {
        std::string err;
        if (!WriteSaveAs(outPath, store, settings, algo, &err)) {
            console::WriteError("Error saving checksum file: " + err);
            return kExitFailure;
        }
        std::string what = ToString(algo);
        if (algo == HashType::Crc32)
            what += settings.crc32ByteOrder == Crc32ByteOrder::Standard ? ", standard byte order" : ", reversed byte order";
        console::WriteLine("Saved: " + outPath + " [" + what + "]");
    }
    return ExitCodeFor(r);
}

// ------------------------------------------------------------------ Management commands

std::string ConsentText(DataConsent c) {
    switch (c) {
        case DataConsent::Allowed: return "Allowed";
        case DataConsent::Declined: return "Declined";
        default: return "Not allowed yet (never asked)";
    }
}

int RunDataStorage(const std::string& action, DataStorage& storage, const AppSettings& settings) {
    std::string err;
    if (storage.Directory().empty()) {
        console::WriteError("The local app data folder could not be determined.");
        return kExitFailure;
    }
    if (action == "allow" || action == "ask") {
        DataConsent result = DataConsent::Allowed;
        if (action == "ask") {
            result = storage.Ask(nullptr, &err);
        } else if (!storage.SetConsent(DataConsent::Allowed, nullptr, &err)) {
            result = storage.Consent();
        }
        if (!err.empty()) {
            console::WriteError(err);
            return kExitFailure;
        }
        if (result == DataConsent::Allowed)
            console::WriteLine("Local data storage allowed. Drive classifications will be kept in:\n  " + storage.DriveTypeFile());
        else
            console::WriteLine("Local data storage declined. Nothing will be stored.");
        return kExitOk;
    }
    if (action == "deny") {
        std::vector<std::string> removed;
        if (!storage.SetConsent(DataConsent::Declined, &removed, &err)) {
            console::WriteError(err);
            return kExitFailure;
        }
        console::WriteLine("Local data storage declined. Nothing will be stored.");
        for (const auto& f : removed) console::WriteLine("Deleted: " + f);
        return kExitOk;
    }
    // status
    console::WriteLine("Local data storage: " + ConsentText(storage.Consent()));
    console::WriteLine("Folder:             " + storage.Directory());
    if (storage.Allowed()) {
        const auto records = DriveTypeStore(storage.DriveTypeFile()).Load();
        console::WriteLine("DriveType.ini:      " + (fs::FileExists(storage.DriveTypeFile())
                                                         ? std::to_string(records.size()) + " drive(s) recorded"
                                                         : std::string("not created yet")));
    }
    console::WriteLine("Decision kept in:   " + settings.IniPath());
    if (storage.Consent() != DataConsent::Allowed)
        console::WriteLine(std::string("Change it with:     ") + kProg + " -datastorage allow | deny | ask");
    return kExitOk;
}

// "has not been allowed yet" / "was declined", for use inside sentences.
std::string ConsentPhrase(DataConsent c) {
    switch (c) {
        case DataConsent::Allowed: return "is allowed";
        case DataConsent::Declined: return "was declined";
        default: return "has not been allowed yet";
    }
}

std::string RecordSummary(const DriveRecord& r) {
    std::string s = (r.isHdd ? "HDD" : "SSD");
    if (r.userSet) {
        s += "  your setting";
        if (!r.changed.empty()) s += " (" + r.changed + ")";
        s += std::string("; auto-detection said ") + (r.detectedIsHdd ? "HDD" : "SSD");
        if (!r.detectedBy.empty()) s += " - " + r.detectedBy;
    } else {
        s += "  detected " + r.recorded + " - " + r.detectedBy;
    }
    return s;
}

int RunListDrives(const DriveResolver& drives, const DataStorage& storage) {
    if (!storage.Allowed()) {
        console::WriteLine("Local data storage " + ConsentPhrase(storage.Consent()) + ", so no drives are stored.");
        return kExitOk;
    }
    const auto records = drives.List();
    console::WriteLine(drives.StoreFile() + ": " + std::to_string(records.size()) + " drive(s)");
    for (const DriveRecord& r : records) {
        console::WriteLine();
        std::string where = r.mountPoint.empty() ? r.section : r.mountPoint;
        if (!r.label.empty() || !r.fileSystem.empty()) where += "  (" + r.label + (r.label.empty() ? "" : ", ") + r.fileSystem + ")";
        console::WriteLine(where);
        console::WriteLine("    " + RecordSummary(r));
        if (!r.guid.empty() || !r.serial.empty())
            console::WriteLine("    GUID " + (r.guid.empty() ? std::string("n/a") : r.guid) + "   Serial " +
                               (r.serial.empty() ? std::string("n/a") : r.serial));
    }
    return kExitOk;
}

int RunDriveStatus(const std::string& p, const DriveResolver& drives, const DataStorage& storage,
                   const AppSettings& settings, const Args& args) {
    const ProcessingMode mode = args.mode.value_or(settings.processingMode);
    const auto st = drives.Inspect(mode, p, args.mode.has_value());
    auto orNA = [](const std::string& s) { return s.empty() ? std::string("n/a") : s; };
    console::WriteLine("Path:           " + orNA(st.volume.probePath));
    console::WriteLine("Mount point:    " + orNA(st.volume.mountPoint) + "  (" + st.volume.driveType + ")");
    console::WriteLine("Volume GUID:    " + orNA(st.volume.guid));
    console::WriteLine("Serial number:  " + orNA(st.volume.serial));
    console::WriteLine("File system:    " + orNA(st.volume.fileSystem) + "   Label: " + orNA(st.volume.label));
    if (!storage.Allowed())
        console::WriteLine("Stored:         - (local data storage " + ConsentPhrase(storage.Consent()) + ")");
    else
        console::WriteLine("Stored:         " + (st.stored ? RecordSummary(*st.stored) : std::string("not yet (detected on first use)")));
    console::WriteLine(std::string("Detection now:  ") + (st.detectedNow.isHdd ? "HDD" : "SSD") + " - " + st.detectedNow.method);
    console::WriteLine("In use:         " + DescribeDecision(st.inUse));
    console::WriteLine();
    console::Write(drive::GetDebugInfo(p));
    return kExitOk;
}

int RunDriveSet(const Args& args, const DriveResolver& drives, const DataStorage& storage) {
    if (!storage.Allowed()) {
        console::WriteError("Local data storage " + ConsentPhrase(storage.Consent()) +
                            ", so a per-drive setting cannot be saved.\n"
                            "Allow it with: " + kProg + " -datastorage allow\n"
                            "(Without it, -mode hdd|ssd or ProcessingMode in SharpSFV.ini still apply to all drives.)");
        return kExitFailure;
    }
    std::optional<bool> isHdd;
    if (text::EqualsIgnoreCase(args.driveSetValue, "hdd")) isHdd = true;
    else if (text::EqualsIgnoreCase(args.driveSetValue, "ssd")) isHdd = false;
    DriveDecision d;
    std::string err;
    if (!drives.SetDrive(args.driveSetPath, isHdd, &d, &err)) {
        console::WriteError(err);
        return kExitFailure;
    }
    console::WriteLine("Saved: " + (d.volume.mountPoint.empty() ? args.driveSetPath : d.volume.mountPoint) + " -> " +
                       DescribeDecision(d));
    console::WriteLine("In: " + drives.StoreFile());
    return kExitOk;
}

} // namespace

std::string HeadlessUsage() {
    return "SharpSFV - file hashing and verification (C++ port)\n"
           "\n"
           "Usage:\n"
           "  SharpSFV-cli <checksum file>                 Verify (.sfv .md5 .sha1 .sha256 .xxh3 .txt ...)\n"
           "  SharpSFV-cli <files/folders...>              Hash and report\n"
           "  SharpSFV-cli -create <files/folders...>      Hash and save checksums\n"
           "\n"
           "Options:\n"
           "  -out <file>          Where to save (default: <common folder>\\checksums.<ext>).\n"
           "                       Its extension picks the algorithm unless -algo is given.\n"
           "  -algo <name>         xxh3 | crc32 | md5 | sha1 | sha256 (default: DefaultAlgo in SharpSFV.ini)\n"
           "  -mode <mode>         auto | hdd | ssd for this run (default: ProcessingMode in SharpSFV.ini)\n"
           "  -crc-order <order>   standard | reversed: byte order of CRC-32 values written to .sfv files\n"
           "                       (default: Crc32ByteOrder in SharpSFV.ini, standard = QuickSFV-compatible).\n"
           "                       Verification always accepts both.\n"
           "\n"
           "Drives and local data storage:\n"
           "  -datastorage [status|allow|deny|ask]   Permission to keep DriveType.ini in %LOCALAPPDATA%\\SharpSFV\n"
           "                                         (deny deletes it; ask shows the consent message box)\n"
           "  -drives                                List the drives recorded in DriveType.ini\n"
           "  -drive-status <path>                   How the drive holding <path> is identified and classified\n"
           "  -drive-set <path> hdd|ssd|auto         Your setting for that drive (auto = detect again)\n"
           "\n"
           "Settings are read from SharpSFV.ini next to the program (created with the defaults if missing).\n"
           "The GUI build accepts the same arguments after -headless.\n"
           "Exit codes: 0 all OK, 1 bad/missing/unreadable files, 2 error, 3 cancelled.\n";
}

int RunHeadless(const std::vector<std::string>& argv, const std::string& iniDirectory) {
    // Settings first: the first run creates SharpSFV.ini with the defaults, so there is a
    // file to edit (the GUI writes it on exit; the command line used to never write it).
    AppSettings settings(iniDirectory);
    settings.Load();
    settings.CreateIfMissing();

    const Args args = ParseArgs(argv);
    if (!args.error.empty()) {
        console::WriteError(args.error);
        return kExitFailure;
    }
    DataStorage storage(settings);
    DriveResolver drives(&storage);

    // Management commands.
    if (args.dataStorage) return RunDataStorage(*args.dataStorage, storage, settings);
    if (args.listDrives) return RunListDrives(drives, storage);
    if (!args.driveStatusPath.empty()) return RunDriveStatus(args.driveStatusPath, drives, storage, settings, args);
    if (!args.driveSetPath.empty()) return RunDriveSet(args, drives, storage);

    if (args.help || args.paths.empty()) {
        console::Write(HeadlessUsage());
        return args.help ? kExitOk : kExitFailure;
    }

    // The GUI silently ignored dropped paths that do not exist; in a script a typo must not
    // look like success, so report them and fail when nothing usable is left.
    int existing = 0;
    for (const auto& p : args.paths) {
        if (fs::FileExists(p) || fs::DirectoryExists(p))
            ++existing;
        else
            console::WriteError("Not found: " + p);
    }
    if (existing == 0) return kExitFailure;

    // HandleDroppedPaths: a single existing checksum file means "verify".
    const std::string& first = args.paths[0];
    const bool verify = !args.create && args.out.empty() && args.paths.size() == 1 && !fs::DirectoryExists(first) &&
                        IsVerificationExtension(first) && fs::FileExists(first);
    return verify ? RunVerify(first, args, settings, drives, storage) : RunCreate(args, settings, drives, storage);
}

} // namespace sfv
