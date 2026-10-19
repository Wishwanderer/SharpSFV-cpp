// SharpSFV (lean C++ rewrite) - per-drive SSD/HDD classification (DriveType.ini) and the
// user's consent to local data storage.
// Port of src/core/DriveClassifier.* and src/core/DataStorage.* (same file format, same
// decisions, same messages).
//
// Which reading strategy a run uses is decided in this order; the first that applies wins:
//   1. -mode hdd|ssd on the command line                         (this run only)
//   2. ProcessingMode=HDD|SSD in SharpSFV.ini                     (every drive)
//   3. Your own setting for this drive in DriveType.ini           (Source=User)
//   4. The stored result of this drive's one-time detection       (Source=Auto)
//   5. Detect now; with consent, store the result (so it happens once per drive)
// Steps 3-5 need consent to local data storage; without it the drive is detected on every
// run and nothing is stored. A drive matches its DriveType.ini entry by (a) the same volume
// GUID and serial number, else (b) the same serial number and file system (encrypted
// volumes such as VeraCrypt); network shares match by their \\server\share root.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "../base/Str.h"
#include "../base/Vec.h"
#include "Drive.h"
#include "Settings.h"
#include "Types.h"

namespace sfv {

// ------------------------------------------------------------------ consent (DataStorage)

// SharpSFV keeps per-user data (DriveType.ini) in %LOCALAPPDATA%\SharpSFV, and only after
// the user agreed. The decision is kept in SharpSFV.ini (DataStorageConsent=).
class DataStorage {
public:
    // `settings` must outlive this object; consent is read from and saved to it. The folder
    // is %LOCALAPPDATA%\SharpSFV unless one is given (tests), found on first use.
    explicit DataStorage(AppSettings& settings);
    DataStorage(AppSettings& settings, StrView directory);

    DataConsent Consent() const { return settings_.dataStorageConsent; }
    bool Allowed() const { return Consent() == DataConsent::Allowed && !Directory().Empty(); }
    const Str& Directory() const;
    Str DriveTypeFile() const; // <dir>\DriveType.ini

    // Records the decision in SharpSFV.ini. Withdrawing (Declined) also deletes everything
    // SharpSFV stored in the folder; the deleted paths are returned in `removed`.
    bool SetConsent(DataConsent c, Vec<Str>* removed = nullptr, Str* error = nullptr);

    static Str ConsentTitle();
    Str ConsentMessage() const;
    // Shows the Yes/No prompt (a message box on Windows, the console elsewhere) and records
    // the answer. Returns the decision (unchanged if it could not be recorded).
    DataConsent Ask(Str* error = nullptr) { return AskOwned(nullptr, error); }
    // The same, with the message box owned by a window (an HWND; the GUI passes its own).
    DataConsent AskOwned(void* ownerWindow, Str* error = nullptr);

private:
    AppSettings& settings_;
    mutable Str dir_;
    mutable bool dirKnown_;
};

// ------------------------------------------------------------------ DriveType.ini

struct DriveRecord {
    Str section;
    Str guid, serial, fileSystem, label, mountPoint;
    bool isHdd = true;         // Type=HDD|SSD: the classification SharpSFV uses
    bool userSet = false;      // Source=User (never re-detected) | Auto
    bool detectedIsHdd = true; // Detected=: what automatic detection said
    bool detectionFailed = false;
    Str detectedBy; // how it was detected
    Str recorded;   // when it was detected
    Str changed;    // when the user set Type (userSet only)
};

class DriveTypeStore {
public:
    explicit DriveTypeStore(StrView file) : file_(file) {}
    const Str& File() const { return file_; }

    Vec<DriveRecord> Load() const; // empty if the file does not exist

    // Locked read-modify-write: `update` receives the current records; return true to
    // write them back (atomically). Creates the folder if needed.
    bool Update(FnRef<bool(Vec<DriveRecord>&)> update, Str* error = nullptr) const;

    // Edits by section name (the drive dialog, which can also edit drives that are not
    // connected): SetType makes it a user setting; Remove forgets the drive (detected again).
    bool SetType(StrView section, bool isHdd, Str* error = nullptr) const;
    bool Remove(StrView section, Str* error = nullptr) const;

    static int Match(const Vec<DriveRecord>& records, const drive::VolumeInfo& v); // index or -1
    static DriveRecord NewRecord(const Vec<DriveRecord>& existing, const drive::VolumeInfo& v);

    static Str Serialize(const Vec<DriveRecord>& records);
    static Vec<DriveRecord> Parse(StrView text);

private:
    Str file_;
};

struct DriveDecision {
    enum class Source {
        CommandLine,      // -mode
        Setting,          // ProcessingMode in SharpSFV.ini
        UserOverride,     // DriveType.ini, Source=User
        Stored,           // DriveType.ini, Source=Auto (detected earlier)
        DetectedSaved,    // detected now, first time for this drive, stored
        DetectedNotSaved, // detected now, not stored (see note)
    };
    bool isHdd = true;
    Source source = Source::DetectedNotSaved;
    drive::VolumeInfo volume;
    Str detectedBy;
    bool detectionFailed = false;
    Str recorded; // date of the stored detection / user setting
    Str note;     // why nothing was stored, or a save error
};

// "SSD (Parallel) - your setting for this drive" etc.
Str DescribeDecision(const DriveDecision& d);

class DriveResolver {
public:
    using IdentifyFn = drive::VolumeInfo (*)(StrView);
    using DetectFn = drive::Detection (*)(const drive::VolumeInfo&);

    // storage may be null (no per-drive data at all). identify/detect are replaceable for tests.
    explicit DriveResolver(const DataStorage* storage, IdentifyFn identify = drive::IdentifyVolume,
                           DetectFn detect = drive::Detect)
        : storage_(storage), identify_(identify), detect_(detect) {}

    // fromCommandLine: `mode` came from -mode. mayStore=false never writes (status reports).
    DriveDecision Resolve(ProcessingMode mode, StrView path, bool fromCommandLine = false, bool mayStore = true) const;

    struct Status {
        drive::VolumeInfo volume;
        Opt<DriveRecord> stored;
        drive::Detection detectedNow;
        DriveDecision inUse;
    };
    Status Inspect(ProcessingMode mode, StrView path, bool fromCommandLine = false) const;

    // Sets this drive's classification (Source=User); no value clears the user setting (the
    // drive is detected again now and stored as Source=Auto). Requires consent.
    bool SetDrive(StrView path, Opt<bool> isHdd, DriveDecision* result, Str* error) const;

    Vec<DriveRecord> List() const;
    Str StoreFile() const;

private:
    bool CanUseStore() const;
    Str StorageNote() const;

    const DataStorage* storage_;
    IdentifyFn identify_;
    DetectFn detect_;
};

} // namespace sfv
