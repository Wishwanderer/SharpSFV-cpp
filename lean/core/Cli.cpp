// SharpSFV (lean C++ rewrite) - the command-line mode (see Cli.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: drive detection, -mode, parallel hashing on SSDs, the drive and data
//                     storage commands; a missing SharpSFV.ini is created with defaults.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Cli.h"

#include "../base/FileSystem.h"
#include "../base/Path.h"
#include "../base/System.h"
#include "../base/Text.h"
#include "ChecksumFile.h"
#include "DriveStore.h"
#include "Engine.h"
#include "Settings.h"

namespace sfv {

namespace {

struct Args {
    Vec<Str> paths;
    bool create = false;
    bool help = false;
    Str out;
    Opt<HashType> algo;
    Opt<ProcessingMode> mode;
    Opt<Crc32ByteOrder> crcOrder;
    // Management commands (run instead of hashing).
    Opt<Str> dataStorage; // status | allow | deny | ask
    bool listDrives = false;
    Str driveStatusPath;
    Str driveSetPath, driveSetValue;
    Str error;
};

constexpr const char* kProg = "SharpSFV-cli";

Opt<HashType> ParseAlgoName(StrView s) {
    if (Opt<HashType> t = ParseHashType(s)) return t;
    const Str f = text::FoldCase(s);
    if (f == "XXH3" || f == "XXHASH" || f == "XXHASH-3" || f == "XXH128") return HashType::XXHASH3;
    if (f == "CRC" || f == "CRC-32" || f == "SFV") return HashType::Crc32;
    if (f == "SHA-1") return HashType::SHA1;
    if (f == "SHA-256") return HashType::SHA256;
    return {};
}

Args ParseArgs(const Vec<Str>& argv) {
    Args a;
    for (usize i = 0; i < argv.Size(); ++i) {
        const Str& s = argv[i];
        if (!s.StartsWith('-')) { // the original treats every non-dash argument as a path
            a.paths.Push(s);
            continue;
        }
        auto value = [&](const char* name) -> Str {
            if (i + 1 >= argv.Size()) {
                a.error = Str("Missing value for ") + name;
                return {};
            }
            return argv[++i];
        };
        auto is = [&](const char* opt) { return text::EqualsIgnoreCase(s, opt); };
        if (is("-headless")) continue;
        if (is("-create")) a.create = true;
        else if (is("-out") || is("-o")) a.out = value("-out");
        else if (is("-algo")) {
            const Str v = value("-algo");
            if (!v.Empty() && !(a.algo = ParseAlgoName(v))) a.error = "Unknown algorithm: " + v;
        } else if (is("-mode")) {
            const Str v = value("-mode");
            if (!v.Empty() && !(a.mode = ParseProcessingMode(v))) a.error = "Unknown mode: " + v;
        } else if (is("-crc-order")) {
            const Str v = value("-crc-order");
            if (!v.Empty() && !(a.crcOrder = ParseCrc32ByteOrder(v)))
                a.error = "Unknown CRC-32 byte order: " + v + " (use standard or reversed)";
        } else if (is("-datastorage")) {
            a.dataStorage = Str("status");
            if (i + 1 < argv.Size()) {
                static const char* const kActions[] = {"status", "allow", "deny", "ask"};
                const Str& n = argv[i + 1];
                for (const char* c : kActions)
                    if (text::EqualsIgnoreCase(n, c)) {
                        a.dataStorage = Str(c);
                        ++i;
                    }
            }
        } else if (is("-drives")) {
            a.listDrives = true;
        } else if (is("-drive-status")) {
            a.driveStatusPath = value("-drive-status");
        } else if (is("-drive-set")) {
            a.driveSetPath = value("-drive-set");
            if (a.error.Empty()) a.driveSetValue = value("-drive-set");
            if (a.error.Empty() && !text::EqualsIgnoreCase(a.driveSetValue, "hdd") &&
                !text::EqualsIgnoreCase(a.driveSetValue, "ssd") && !text::EqualsIgnoreCase(a.driveSetValue, "auto"))
                a.error = "-drive-set needs hdd, ssd or auto after the path";
        } else if (is("-h") || is("-help") || is("--help") || s == "-?") {
            a.help = true;
        } else {
            console::WriteError("Ignoring unknown option: " + s);
        }
        if (!a.error.Empty()) break;
    }
    return a;
}

// ------------------------------------------------------------------ Output

void PrintSummary(int total, int ok, int bad, int missing, i64 ms) {
    console::WriteLine();
    console::WriteLine("------------------------------------------------");
    console::WriteLine("Total: " + ToStr(total));
    console::WriteLine("OK: " + ToStr(ok));
    console::WriteLine("BAD: " + ToStr(bad));
    console::WriteLine("MISSING: " + ToStr(missing));
    console::WriteLine("Time: " + ToStr(ms) + " ms");
    console::WriteLine("------------------------------------------------");
}

void ConsolePresenter(void*, const ProgressInfo& p) {
    const double pct = p.total > 0 ? static_cast<double>(p.current) / p.total * 100.0 : 0.0;
    Str line = "\r" + ToStr(p.current) + "/" + ToStr(p.total) + " (" + text::FormatF1(pct) + "%) | OK: " + ToStr(p.ok) +
               " | BAD: " + ToStr(p.bad) + " | MISSING: " + ToStr(p.missing);
    if (!p.speed.Empty()) line += " | " + p.speed;
    line += "   ";
    console::Write(line);
}

void InstallPresenter(ProgressThrottle& throttle, bool showStats) {
    throttle.SetShowThroughput(showStats);
    throttle.SetPresenter(ConsolePresenter, nullptr);
}

int ExitCodeFor(const RunResult& r) {
    if (r.cancelled) return kExitCancelled;
    return (r.bad > 0 || r.missing > 0) ? kExitProblems : kExitOk;
}

Str Quote(const Str& p) { return p.Find(' ') == kNpos ? p : "\"" + p + "\""; }

// Decides HDD/SSD for a run and explains the decision (and how to fix a failed detection).
bool ResolveDrive(const DriveResolver& drives, const DataStorage& storage, const Args& args,
                  const AppSettings& settings, const Str& probe) {
    const ProcessingMode mode = args.mode ? *args.mode : settings.processingMode;
    const DriveDecision d = drives.Resolve(mode, probe, args.mode.HasValue());
    const Str where = d.volume.mountPoint.Empty() ? Str() : " [" + d.volume.mountPoint + "]";
    console::WriteLine("Mode: " + DescribeDecision(d) + where);
    const bool automatic = d.source == DriveDecision::Source::Stored || d.source == DriveDecision::Source::DetectedSaved ||
                           d.source == DriveDecision::Source::DetectedNotSaved;
    if (automatic && d.detectionFailed) {
        console::WriteLine("  Automatic detection did not work for this drive (common with encrypted volumes).");
        if (storage.Allowed())
            console::WriteLine(Str("  Set it yourself: ") + kProg + " -drive-set " + Quote(probe) + " hdd|ssd");
        else // a per-drive setting needs local data storage; the global overrides do not
            console::WriteLine("  Choose for this run with -mode hdd|ssd, or for all drives with ProcessingMode in SharpSFV.ini.");
    }
    if (d.source == DriveDecision::Source::DetectedNotSaved && storage.Consent() == DataConsent::NotAsked)
        console::WriteLine(Str("  To detect each drive only once and keep per-drive settings: ") + kProg +
                           " -datastorage ask");
    return d.isHdd;
}

struct InterruptScope {
    InterruptScope() { InstallInterruptHandler(); }
    ~InterruptScope() { RemoveInterruptHandler(); }
};

// ------------------------------------------------------------------ Modes

int RunVerify(const Str& checkFile, const Args& args, const AppSettings& settings, const DriveResolver& drives,
              const DataStorage& storage) {
    // .sfv/.md5/... pick the algorithm; other extensions (.txt, .sum, .xxh) use -algo or
    // the configured default.
    const HashType algo = DetectAlgoFromExtension(checkFile, args.algo ? *args.algo : settings.defaultAlgo);

    Str err;
    ParsedChecksumFile parsed;
    if (!ParseChecksumFile(checkFile, algo, parsed, &err)) {
        console::WriteLine("Error parsing: " + err);
        return kExitFailure;
    }

    const Str baseFolder = path::GetDirectoryName(checkFile).ValueOr(Str());
    FileStore store;
    const Vec<int> order = PopulateVerificationStore(store, parsed, baseFolder);
    console::WriteLine("Verifying " + checkFile + " (" + ToStr(store.Count()) + " files)...");
    const bool isHdd = ResolveDrive(drives, storage, args, settings, baseFolder.Empty() ? Str(".") : baseFolder);

    ProgressThrottle throttle;
    throttle.ResetMetrics();
    InstallPresenter(throttle, settings.showThroughputStats);
    Engine engine(store, throttle);
    engine.SetExternalCancel(Interrupted);
    InterruptScope interrupts;

    HashRunOptions opt;
    opt.algo = algo;
    opt.isHdd = isHdd;
    const i64 t0 = TickMs();
    const RunResult r = engine.Verify(order, opt, [&](const Counters& c) {
        throttle.Update(c.completed, c.total, c.ok, c.bad, c.missing);
    });
    PrintSummary(r.completed, r.ok, r.bad, r.missing, TickMs() - t0);
    if (algo == HashType::Crc32 && r.crcStandardMatches + r.crcReversedMatches > 0) {
        console::WriteLine("CRC-32 byte order: " + ToStr(r.crcStandardMatches) + " standard (QuickSFV-style), " +
                           ToStr(r.crcReversedMatches) + " reversed (C# SharpSFV-style)");
        if (r.crcStandardMatches > 0 && r.crcReversedMatches > 0) console::WriteLine("Note: this file mixes both byte orders.");
    }
    if (r.cancelled) console::WriteLine("Cancelled.");
    return ExitCodeFor(r);
}

int RunCreate(const Args& args, AppSettings& settings, const DriveResolver& drives, const DataStorage& storage) {
    const Vec<Str>& paths = args.paths;
    if (args.crcOrder) settings.crc32ByteOrder = *args.crcOrder; // this run only (not saved)

    // Base directory: the folder itself, or the deepest folder shared by all parents.
    Str baseDirectory;
    if (paths.Size() == 1 && fs::DirectoryExists(paths[0])) {
        baseDirectory = paths[0];
    } else {
        Vec<Str> parents;
        for (const Str& p : paths) parents.Push(path::GetDirectoryName(p).ValueOr(p));
        baseDirectory = path::FindCommonBasePath(parents);
    }

    const bool writeOutput = args.create || !args.out.Empty();
    HashType algo = args.algo ? *args.algo : settings.defaultAlgo;
    if (!args.algo && !args.out.Empty()) algo = DetectAlgoFromExtension(args.out, algo);

    Str outPath = args.out;
    if (writeOutput && outPath.Empty()) {
        if (baseDirectory.Empty()) {
            console::WriteError("The inputs share no common folder; use -out <file> to choose where to save.");
            return kExitFailure;
        }
        outPath = path::Combine(baseDirectory, Str("checksums") + ExtensionForAlgo(algo));
    }

    Str probe;
    if (!paths.Empty())
        probe = fs::DirectoryExists(paths[0]) ? paths[0] : path::GetDirectoryName(paths[0]).ValueOr(Str());
    const bool isHdd = ResolveDrive(drives, storage, args, settings, probe.Empty() ? Str(".") : probe);

    FileStore store;
    ProgressThrottle throttle;
    throttle.ResetMetrics();
    InstallPresenter(throttle, settings.showThroughputStats);
    Engine engine(store, throttle);
    engine.SetExternalCancel(Interrupted);
    InterruptScope interrupts;

    HashRunOptions opt;
    opt.algo = algo;
    opt.isHdd = isHdd;
    opt.applyFilters = settings.showAdvancedBar;
    opt.includePattern = settings.includePattern;
    opt.excludePattern = settings.excludePattern;
    opt.recursive = (args.create || settings.showAdvancedBar) ? settings.scanRecursive : true;
    opt.skipPath = outPath;

    const i64 t0 = TickMs();
    const RunResult r = engine.HashFiles(paths, baseDirectory, opt, [&](const Counters& c) {
        throttle.Update(c.completed, c.total, c.ok, c.bad, 0);
    });
    PrintSummary(r.completed, r.ok, r.bad, 0, TickMs() - t0);

    if (r.cancelled) {
        console::WriteLine("Cancelled.");
        return kExitCancelled;
    }
    if (writeOutput) {
        Str err;
        if (!WriteSaveAs(outPath, store, settings, algo, &err)) {
            console::WriteError("Error saving checksum file: " + err);
            return kExitFailure;
        }
        Str what = ToString(algo);
        if (algo == HashType::Crc32)
            what += settings.crc32ByteOrder == Crc32ByteOrder::Standard ? ", standard byte order" : ", reversed byte order";
        console::WriteLine("Saved: " + outPath + " [" + what + "]");
    }
    return ExitCodeFor(r);
}

// ------------------------------------------------------------------ Management commands

Str ConsentText(DataConsent c) {
    switch (c) {
        case DataConsent::Allowed: return "Allowed";
        case DataConsent::Declined: return "Declined";
        default: return "Not allowed yet (never asked)";
    }
}

int RunDataStorage(const Str& action, DataStorage& storage, const AppSettings& settings) {
    Str err;
    if (storage.Directory().Empty()) {
        console::WriteError("The local app data folder could not be determined.");
        return kExitFailure;
    }
    if (action == "allow" || action == "ask") {
        DataConsent result = DataConsent::Allowed;
        if (action == "ask") {
            result = storage.Ask(&err);
        } else if (!storage.SetConsent(DataConsent::Allowed, nullptr, &err)) {
            result = storage.Consent();
        }
        if (!err.Empty()) {
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
        Vec<Str> removed;
        if (!storage.SetConsent(DataConsent::Declined, &removed, &err)) {
            console::WriteError(err);
            return kExitFailure;
        }
        console::WriteLine("Local data storage declined. Nothing will be stored.");
        for (const Str& f : removed) console::WriteLine("Deleted: " + f);
        return kExitOk;
    }
    // status
    console::WriteLine("Local data storage: " + ConsentText(storage.Consent()));
    console::WriteLine("Folder:             " + storage.Directory());
    if (storage.Allowed()) {
        const Vec<DriveRecord> records = DriveTypeStore(storage.DriveTypeFile()).Load();
        console::WriteLine("DriveType.ini:      " + (fs::FileExists(storage.DriveTypeFile())
                                                         ? ToStr(static_cast<i64>(records.Size())) + " drive(s) recorded"
                                                         : Str("not created yet")));
    }
    console::WriteLine("Decision kept in:   " + settings.IniPath());
    if (storage.Consent() != DataConsent::Allowed)
        console::WriteLine(Str("Change it with:     ") + kProg + " -datastorage allow | deny | ask");
    return kExitOk;
}

// "has not been allowed yet" / "was declined", for use inside sentences.
Str ConsentPhrase(DataConsent c) {
    switch (c) {
        case DataConsent::Allowed: return "is allowed";
        case DataConsent::Declined: return "was declined";
        default: return "has not been allowed yet";
    }
}

Str RecordSummary(const DriveRecord& r) {
    Str s = r.isHdd ? "HDD" : "SSD";
    if (r.userSet) {
        s += "  your setting";
        if (!r.changed.Empty()) s += " (" + r.changed + ")";
        s += Str("; auto-detection said ") + (r.detectedIsHdd ? "HDD" : "SSD");
        if (!r.detectedBy.Empty()) s += " - " + r.detectedBy;
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
    const Vec<DriveRecord> records = drives.List();
    console::WriteLine(drives.StoreFile() + ": " + ToStr(static_cast<i64>(records.Size())) + " drive(s)");
    for (const DriveRecord& r : records) {
        console::WriteLine();
        Str where = r.mountPoint.Empty() ? r.section : r.mountPoint;
        if (!r.label.Empty() || !r.fileSystem.Empty())
            where += "  (" + r.label + (r.label.Empty() ? "" : ", ") + r.fileSystem + ")";
        console::WriteLine(where);
        console::WriteLine("    " + RecordSummary(r));
        if (!r.guid.Empty() || !r.serial.Empty())
            console::WriteLine("    GUID " + (r.guid.Empty() ? Str("n/a") : r.guid) + "   Serial " +
                               (r.serial.Empty() ? Str("n/a") : r.serial));
    }
    return kExitOk;
}

int RunDriveStatus(const Str& p, const DriveResolver& drives, const DataStorage& storage, const AppSettings& settings,
                   const Args& args) {
    const ProcessingMode mode = args.mode ? *args.mode : settings.processingMode;
    const DriveResolver::Status st = drives.Inspect(mode, p, args.mode.HasValue());
    auto orNA = [](const Str& s) { return s.Empty() ? Str("n/a") : s; };
    console::WriteLine("Path:           " + orNA(st.volume.probePath));
    console::WriteLine("Mount point:    " + orNA(st.volume.mountPoint) + "  (" + st.volume.driveType + ")");
    console::WriteLine("Volume GUID:    " + orNA(st.volume.guid));
    console::WriteLine("Serial number:  " + orNA(st.volume.serial));
    console::WriteLine("File system:    " + orNA(st.volume.fileSystem) + "   Label: " + orNA(st.volume.label));
    if (!storage.Allowed())
        console::WriteLine("Stored:         - (local data storage " + ConsentPhrase(storage.Consent()) + ")");
    else
        console::WriteLine("Stored:         " + (st.stored ? RecordSummary(*st.stored) : Str("not yet (detected on first use)")));
    console::WriteLine(Str("Detection now:  ") + (st.detectedNow.isHdd ? "HDD" : "SSD") + " - " + st.detectedNow.method);
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
    Opt<bool> isHdd;
    if (text::EqualsIgnoreCase(args.driveSetValue, "hdd")) isHdd = true;
    else if (text::EqualsIgnoreCase(args.driveSetValue, "ssd")) isHdd = false;
    DriveDecision d;
    Str err;
    if (!drives.SetDrive(args.driveSetPath, isHdd, &d, &err)) {
        console::WriteError(err);
        return kExitFailure;
    }
    console::WriteLine("Saved: " + (d.volume.mountPoint.Empty() ? args.driveSetPath : d.volume.mountPoint) + " -> " +
                       DescribeDecision(d));
    console::WriteLine("In: " + drives.StoreFile());
    return kExitOk;
}

} // namespace

Str HeadlessUsage() {
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

int RunHeadless(const Vec<Str>& argv, StrView iniDirectory) {
    // Settings first: the first run creates SharpSFV.ini with the defaults, so there is a
    // file to edit (the standard build's CLI does the same).
    AppSettings settings(iniDirectory);
    settings.Load();
    settings.CreateIfMissing();

    const Args args = ParseArgs(argv);
    if (!args.error.Empty()) {
        console::WriteError(args.error);
        return kExitFailure;
    }
    DataStorage storage(settings);
    DriveResolver drives(&storage);

    // Management commands.
    if (args.dataStorage) return RunDataStorage(*args.dataStorage, storage, settings);
    if (args.listDrives) return RunListDrives(drives, storage);
    if (!args.driveStatusPath.Empty()) return RunDriveStatus(args.driveStatusPath, drives, storage, settings, args);
    if (!args.driveSetPath.Empty()) return RunDriveSet(args, drives, storage);

    if (args.help || args.paths.Empty()) {
        console::Write(HeadlessUsage());
        return args.help ? kExitOk : kExitFailure;
    }

    // A typo in a script must not look like success: report missing inputs, fail if none.
    int existing = 0;
    for (const Str& p : args.paths) {
        if (fs::FileExists(p) || fs::DirectoryExists(p))
            ++existing;
        else
            console::WriteError("Not found: " + p);
    }
    if (existing == 0) return kExitFailure;

    // HandleDroppedPaths: a single existing checksum file means "verify".
    const Str& first = args.paths[0];
    const bool verify = !args.create && args.out.Empty() && args.paths.Size() == 1 && !fs::DirectoryExists(first) &&
                        IsVerificationExtension(first) && fs::FileExists(first);
    return verify ? RunVerify(first, args, settings, drives, storage) : RunCreate(args, settings, drives, storage);
}

} // namespace sfv
