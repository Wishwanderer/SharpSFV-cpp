// SharpSFV (C++ port) - per-drive SSD/HDD classification (DriveType.ini).
// New in the C++ port.
//
// Which reading strategy a run uses is decided in this order; the first that applies wins:
//   1. -mode hdd|ssd on the command line                         (this run only)
//   2. ProcessingMode=HDD|SSD in SharpSFV.ini (Options menu)      (every drive)
//   3. Your own setting for this drive in DriveType.ini           (Source=User)
//   4. The stored result of this drive's one-time detection       (Source=Auto)
//   5. Detect now; with consent, store the result (so it happens once per drive)
// Steps 3-5 need the user's consent to local data storage (DataStorage); without it the
// drive is detected on every run and nothing is stored. 1 and 2 never need it.
//
// Matching a drive to its DriveType.ini entry (VolumeInfo has a GUID and a serial):
//   a. same volume GUID and same serial number, else
//   b. same serial number and file system.
// Rule (b) exists for encrypted volumes such as VeraCrypt. Their mount-manager GUID can
// depend on the drive letter the volume is mounted at, so different containers mounted
// at E: may share a GUID, and one container mounted at F: may get a new GUID. The serial
// number belongs to the file system inside the volume and travels with it. Network shares
// are matched by their \\server\share root.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the C++20 port.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "DataStorage.h"
#include "DriveDetector.h"
#include "Types.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sfv {

// One DriveType.ini section.
struct DriveRecord {
    std::string section;
    std::string guid, serial, fileSystem, label, mountPoint;
    bool isHdd = true;          // Type=HDD|SSD: the classification SharpSFV uses
    bool userSet = false;       // Source=User (never re-detected) | Auto
    bool detectedIsHdd = true;  // Detected=: what automatic detection said
    bool detectionFailed = false;
    std::string detectedBy;     // how it was detected
    std::string recorded;       // when it was detected
    std::string changed;        // when the user set Type (userSet only)
};

class DriveTypeStore {
public:
    explicit DriveTypeStore(std::string file) : file_(std::move(file)) {}
    const std::string& File() const { return file_; }

    std::vector<DriveRecord> Load() const; // empty if the file does not exist

    // Locked read-modify-write: `update` receives the current records; return true to
    // write them back (atomically). Creates the folder if needed.
    bool Update(const std::function<bool(std::vector<DriveRecord>&)>& update, std::string* error = nullptr) const;

    // Index of the record for this volume, or -1 (rules a/b above).
    static int Match(const std::vector<DriveRecord>& records, const drive::VolumeInfo& v);
    // A new record (fresh section name, unique within `existing`) describing `v`.
    static DriveRecord NewRecord(const std::vector<DriveRecord>& existing, const drive::VolumeInfo& v);

    // Edits by section name (the drive-settings dialog, which can also edit drives that are
    // not connected right now). SetType makes it a user setting; Remove forgets the drive,
    // so it is detected again the next time it is used.
    bool SetType(const std::string& section, bool isHdd, std::string* error = nullptr) const;
    bool Remove(const std::string& section, std::string* error = nullptr) const;

    static std::string Serialize(const std::vector<DriveRecord>& records);
    static std::vector<DriveRecord> Parse(const std::string& text);

private:
    std::string file_;
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
    std::string detectedBy; // detection method (stored or fresh)
    bool detectionFailed = false;
    std::string recorded;   // date of the stored detection / user setting
    std::string note;       // why nothing was stored, or a save error
};

// "SSD (Parallel) - your setting for this drive" etc.
std::string DescribeDecision(const DriveDecision& d);

class DriveResolver {
public:
    using IdentifyFn = std::function<drive::VolumeInfo(const std::string&)>;
    using DetectFn = std::function<drive::Detection(const drive::VolumeInfo&)>;

    // storage may be null (no per-drive data at all). identify/detect are injectable for tests.
    explicit DriveResolver(const DataStorage* storage, IdentifyFn identify = drive::IdentifyVolume,
                           DetectFn detect = drive::Detect);

    // fromCommandLine: `mode` came from -mode rather than SharpSFV.ini.
    // mayStore=false never writes (used for status reports).
    DriveDecision Resolve(ProcessingMode mode, const std::string& path, bool fromCommandLine = false,
                          bool mayStore = true) const;

    // Read-only report for one path.
    struct Status {
        drive::VolumeInfo volume;
        std::optional<DriveRecord> stored;
        drive::Detection detectedNow;
        DriveDecision inUse;
    };
    Status Inspect(ProcessingMode mode, const std::string& path, bool fromCommandLine = false) const;

    // Sets this drive's classification (Source=User). isHdd = nullopt clears the user
    // setting: the drive is detected again now and stored as Source=Auto.
    // Requires consent. Returns the resulting decision.
    bool SetDrive(const std::string& path, std::optional<bool> isHdd, DriveDecision* result, std::string* error) const;

    std::vector<DriveRecord> List() const;
    std::string StoreFile() const;

private:
    bool CanUseStore() const;
    std::string StorageNote() const;

    const DataStorage* storage_;
    IdentifyFn identify_;
    DetectFn detect_;
};

} // namespace sfv
