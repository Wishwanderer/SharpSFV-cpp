// SharpSFV (C++ port) - per-drive SSD/HDD classification (DriveType.ini).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the C++20 port.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "DriveClassifier.h"

#include "FileSystem.h"
#include "PathUtil.h"
#include "Text.h"

#include <ctime>

namespace sfv {

namespace {

constexpr const char* kLockName = "SharpSFV_DataStorage";

std::string NowStamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

const char* TypeName(bool isHdd) { return isHdd ? "HDD" : "SSD"; }

std::optional<bool> ParseType(std::string_view v) {
    if (text::EqualsIgnoreCase(text::Trim(v), "HDD")) return true;
    if (text::EqualsIgnoreCase(text::Trim(v), "SSD")) return false;
    return std::nullopt;
}

std::string NetworkSection(const drive::VolumeInfo& v) { return "Network:" + v.mountPoint; }

void FillIdentity(DriveRecord& r, const drive::VolumeInfo& v) {
    r.guid = v.network ? std::string() : v.guid;
    r.serial = v.network ? std::string() : v.serial;
    r.fileSystem = v.fileSystem;
    r.label = v.label;
    r.mountPoint = v.mountPoint;
}

void FillDetection(DriveRecord& r, const drive::Detection& d) {
    r.detectedIsHdd = d.isHdd;
    r.detectionFailed = d.failed;
    r.detectedBy = d.method;
    r.recorded = NowStamp();
}

} // namespace

// ------------------------------------------------------------------ DriveTypeStore

std::vector<DriveRecord> DriveTypeStore::Load() const {
    if (file_.empty() || !fs::FileExists(file_)) return {};
    auto bytes = fs::ReadAllBytes(file_);
    return bytes ? Parse(text::DecodeTextFile(*bytes)) : std::vector<DriveRecord>{};
}

std::vector<DriveRecord> DriveTypeStore::Parse(const std::string& content) {
    std::vector<DriveRecord> out;
    std::optional<DriveRecord> cur;
    bool typeValid = false;
    auto flush = [&] {
        if (cur && typeValid) out.push_back(std::move(*cur));
        cur.reset();
        typeValid = false;
    };
    for (const std::string& raw : text::SplitLines(content)) {
        const std::string_view line = text::Trim(raw);
        if (line.empty() || line.starts_with(';') || line.starts_with('#')) continue;
        if (line.starts_with('[') && line.ends_with(']')) {
            flush();
            cur.emplace();
            cur->section = std::string(line.substr(1, line.size() - 2));
            continue;
        }
        if (!cur) continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view key = text::Trim(line.substr(0, eq));
        const std::string val(text::Trim(line.substr(eq + 1)));
        auto is = [&](const char* k) { return text::EqualsIgnoreCase(key, k); };
        if (is("VolumeGUID")) cur->guid = val;
        else if (is("SerialNumber")) cur->serial = val;
        else if (is("FileSystem")) cur->fileSystem = val;
        else if (is("Label")) cur->label = val;
        else if (is("MountPoint")) cur->mountPoint = val;
        else if (is("Type")) {
            if (auto t = ParseType(val)) {
                cur->isHdd = *t;
                typeValid = true;
            }
        } else if (is("Source")) cur->userSet = text::EqualsIgnoreCase(val, "User");
        else if (is("Detected")) { if (auto t = ParseType(val)) cur->detectedIsHdd = *t; }
        else if (is("DetectionFailed")) cur->detectionFailed = val == "1";
        else if (is("DetectedBy")) cur->detectedBy = val;
        else if (is("Recorded")) cur->recorded = val;
        else if (is("Changed")) cur->changed = val;
    }
    flush();
    return out;
}

std::string DriveTypeStore::Serialize(const std::vector<DriveRecord>& records) {
    const char* nl = fs::kNewLine;
    std::string o;
    auto line = [&](const std::string& l) {
        o += l;
        o += nl;
    };
    line("; SharpSFV drive classification - one section per volume. Safe to edit by hand.");
    line(";   Type   = SSD (files read in parallel) or HDD (files read one at a time). This is what SharpSFV uses.");
    line(";   Source = Auto (detected automatically, once) or User (your own setting; never re-detected).");
    line("; Delete a section to have that drive detected again; delete this file to forget every drive.");
    for (const DriveRecord& r : records) {
        line("");
        line("[" + r.section + "]");
        if (!r.guid.empty()) line("VolumeGUID=" + r.guid);
        if (!r.serial.empty()) line("SerialNumber=" + r.serial);
        if (!r.fileSystem.empty()) line("FileSystem=" + r.fileSystem);
        if (!r.label.empty()) line("Label=" + r.label);
        if (!r.mountPoint.empty()) line("MountPoint=" + r.mountPoint);
        line(std::string("Type=") + TypeName(r.isHdd));
        line(std::string("Source=") + (r.userSet ? "User" : "Auto"));
        line(std::string("Detected=") + TypeName(r.detectedIsHdd));
        if (r.detectionFailed) line("DetectionFailed=1");
        if (!r.detectedBy.empty()) line("DetectedBy=" + r.detectedBy);
        if (!r.recorded.empty()) line("Recorded=" + r.recorded);
        if (r.userSet && !r.changed.empty()) line("Changed=" + r.changed);
    }
    return o;
}

bool DriveTypeStore::Update(const std::function<bool(std::vector<DriveRecord>&)>& update, std::string* error) const {
    const std::string dir = path::GetDirectoryName(file_).value_or("");
    if (dir.empty() || !fs::CreateDirectories(dir)) {
        if (error) *error = "Could not create " + dir;
        return false;
    }
    fs::CrossProcessLock lock(kLockName, dir);
    std::vector<DriveRecord> records = Load(); // re-read under the lock
    if (!update(records)) return true;
    return fs::WriteFileAtomic(file_, Serialize(records), error);
}

bool DriveTypeStore::SetType(const std::string& section, bool isHdd, std::string* error) const {
    bool found = false;
    const bool ok = Update(
        [&](std::vector<DriveRecord>& records) {
            for (DriveRecord& r : records) {
                if (!text::EqualsIgnoreCase(r.section, section)) continue;
                r.isHdd = isHdd;
                r.userSet = true;
                r.changed = NowStamp();
                found = true;
            }
            return found;
        },
        error);
    if (ok && !found && error) *error = "No drive named " + section;
    return ok && found;
}

bool DriveTypeStore::Remove(const std::string& section, std::string* error) const {
    bool found = false;
    const bool ok = Update(
        [&](std::vector<DriveRecord>& records) {
            const auto before = records.size();
            std::erase_if(records, [&](const DriveRecord& r) { return text::EqualsIgnoreCase(r.section, section); });
            found = records.size() != before;
            return found;
        },
        error);
    return ok && found;
}

int DriveTypeStore::Match(const std::vector<DriveRecord>& records, const drive::VolumeInfo& v) {
    const int n = static_cast<int>(records.size());
    if (v.network) {
        const std::string section = NetworkSection(v);
        for (int i = 0; i < n; ++i)
            if (text::EqualsIgnoreCase(records[static_cast<std::size_t>(i)].section, section)) return i;
        return -1;
    }
    // a. same GUID and same serial number
    if (!v.guid.empty()) {
        for (int i = 0; i < n; ++i) {
            const DriveRecord& r = records[static_cast<std::size_t>(i)];
            if (text::EqualsIgnoreCase(r.guid, v.guid) && text::EqualsIgnoreCase(r.serial, v.serial)) return i;
        }
    }
    // b. same serial number and file system (GUID changed, or is tied to the drive letter)
    if (!v.serial.empty()) {
        for (int i = 0; i < n; ++i) {
            const DriveRecord& r = records[static_cast<std::size_t>(i)];
            if (!r.section.starts_with("Network:") && text::EqualsIgnoreCase(r.serial, v.serial) &&
                text::EqualsIgnoreCase(r.fileSystem, v.fileSystem))
                return i;
        }
    }
    return -1;
}

DriveRecord DriveTypeStore::NewRecord(const std::vector<DriveRecord>& existing, const drive::VolumeInfo& v) {
    DriveRecord r;
    FillIdentity(r, v);
    std::string base = v.network ? NetworkSection(v)
                                 : (v.guid.empty() ? std::string("NoGUID") : "Volume" + v.guid) + "/" +
                                       (v.serial.empty() ? std::string("NoSerial") : v.serial);
    std::string name = base;
    for (int k = 2;; ++k) {
        bool clash = false;
        for (const DriveRecord& e : existing)
            if (text::EqualsIgnoreCase(e.section, name)) clash = true;
        if (!clash) break;
        name = base + "#" + std::to_string(k);
    }
    r.section = name;
    return r;
}

// ------------------------------------------------------------------ decisions

std::string DescribeDecision(const DriveDecision& d) {
    std::string s = d.isHdd ? "HDD (Sequential)" : "SSD (Parallel)";
    switch (d.source) {
        case DriveDecision::Source::CommandLine: return s + " - set by -mode";
        case DriveDecision::Source::Setting: return s + " - ProcessingMode setting (all drives)";
        case DriveDecision::Source::UserOverride:
            s += " - your setting for this drive";
            if (!d.recorded.empty()) s += " (" + d.recorded + ")";
            return s;
        case DriveDecision::Source::Stored:
            s += " - detected " + (d.recorded.empty() ? std::string("earlier") : d.recorded) + ": " + d.detectedBy;
            break;
        case DriveDecision::Source::DetectedSaved: s += " - detected now and saved: " + d.detectedBy; break;
        case DriveDecision::Source::DetectedNotSaved:
            s += " - detected now: " + d.detectedBy;
            if (!d.note.empty()) s += " (not saved: " + d.note + ")";
            break;
    }
    return s;
}

DriveResolver::DriveResolver(const DataStorage* storage, IdentifyFn identify, DetectFn detect)
    : storage_(storage), identify_(std::move(identify)), detect_(std::move(detect)) {}

bool DriveResolver::CanUseStore() const { return storage_ && storage_->Allowed(); }

std::string DriveResolver::StoreFile() const { return storage_ ? storage_->DriveTypeFile() : std::string(); }

std::string DriveResolver::StorageNote() const {
    if (!storage_) return "local data storage unavailable";
    switch (storage_->Consent()) {
        case DataConsent::NotAsked: return "local data storage not allowed yet";
        case DataConsent::Declined: return "local data storage declined";
        default: return storage_->Directory().empty() ? "no local app data folder" : "";
    }
}

DriveDecision DriveResolver::Resolve(ProcessingMode mode, const std::string& p, bool fromCommandLine,
                                     bool mayStore) const {
    DriveDecision d;
    if (mode != ProcessingMode::Auto) {
        d.isHdd = mode == ProcessingMode::HDD;
        d.source = fromCommandLine ? DriveDecision::Source::CommandLine : DriveDecision::Source::Setting;
        return d;
    }

    d.volume = identify_(p);
    auto fromRecord = [&](const DriveRecord& r) {
        d.isHdd = r.isHdd;
        d.source = r.userSet ? DriveDecision::Source::UserOverride : DriveDecision::Source::Stored;
        d.detectedBy = r.detectedBy;
        d.detectionFailed = r.detectionFailed;
        d.recorded = r.userSet && !r.changed.empty() ? r.changed : r.recorded;
    };

    const bool useStore = CanUseStore() && d.volume.HasIdentity();
    if (useStore) {
        const DriveTypeStore store(storage_->DriveTypeFile());
        const auto records = store.Load();
        if (const int i = DriveTypeStore::Match(records, d.volume); i >= 0) {
            fromRecord(records[static_cast<std::size_t>(i)]);
            return d;
        }
    }

    // First time for this drive (or no storage): detect.
    const drive::Detection det = detect_(d.volume);
    d.isHdd = det.isHdd;
    d.detectedBy = det.method;
    d.detectionFailed = det.failed;
    d.source = DriveDecision::Source::DetectedNotSaved;

    if (!useStore) {
        d.note = d.volume.HasIdentity() ? StorageNote() : "the volume has no GUID or serial number";
        return d;
    }
    if (!mayStore) return d;

    const DriveTypeStore store(storage_->DriveTypeFile());
    std::string err;
    bool raced = false;
    const bool ok = store.Update(
        [&](std::vector<DriveRecord>& records) {
            if (const int i = DriveTypeStore::Match(records, d.volume); i >= 0) { // another instance just wrote it
                fromRecord(records[static_cast<std::size_t>(i)]);
                raced = true;
                return false;
            }
            DriveRecord r = DriveTypeStore::NewRecord(records, d.volume);
            r.isHdd = det.isHdd;
            FillDetection(r, det);
            d.recorded = r.recorded;
            records.push_back(std::move(r));
            return true;
        },
        &err);
    if (raced) return d;
    if (ok)
        d.source = DriveDecision::Source::DetectedSaved;
    else
        d.note = err.empty() ? std::string("could not write DriveType.ini") : err;
    return d;
}

DriveResolver::Status DriveResolver::Inspect(ProcessingMode mode, const std::string& p, bool fromCommandLine) const {
    Status s;
    s.volume = identify_(p);
    if (CanUseStore() && s.volume.HasIdentity()) {
        const auto records = DriveTypeStore(storage_->DriveTypeFile()).Load();
        if (const int i = DriveTypeStore::Match(records, s.volume); i >= 0) s.stored = records[static_cast<std::size_t>(i)];
    }
    s.detectedNow = detect_(s.volume);
    s.inUse = Resolve(mode, p, fromCommandLine, false);
    return s;
}

bool DriveResolver::SetDrive(const std::string& p, std::optional<bool> isHdd, DriveDecision* result,
                             std::string* error) const {
    if (!CanUseStore()) {
        if (error) *error = "Local data storage is not allowed, so drive settings cannot be saved.";
        return false;
    }
    const drive::VolumeInfo v = identify_(p);
    if (!v.HasIdentity()) {
        if (error) *error = "This volume has no GUID or serial number, so a setting for it cannot be saved.";
        return false;
    }
    const drive::Detection det = detect_(v); // outside the lock (IOCTLs can be slow)

    const DriveTypeStore store(storage_->DriveTypeFile());
    DriveRecord saved;
    const bool ok = store.Update(
        [&](std::vector<DriveRecord>& records) {
            const int i = DriveTypeStore::Match(records, v);
            DriveRecord* r;
            if (i >= 0) {
                r = &records[static_cast<std::size_t>(i)];
                FillIdentity(*r, v); // refresh label / mount point
            } else {
                records.push_back(DriveTypeStore::NewRecord(records, v));
                r = &records.back();
                FillDetection(*r, det);
            }
            if (isHdd) {
                r->isHdd = *isHdd;
                r->userSet = true;
                r->changed = NowStamp();
            } else { // back to automatic: detect again now
                FillDetection(*r, det);
                r->isHdd = det.isHdd;
                r->userSet = false;
                r->changed.clear();
            }
            saved = *r;
            return true;
        },
        error);
    if (!ok) return false;
    if (result) {
        result->volume = v;
        result->isHdd = saved.isHdd;
        result->source = saved.userSet ? DriveDecision::Source::UserOverride : DriveDecision::Source::DetectedSaved;
        result->detectedBy = saved.detectedBy;
        result->detectionFailed = saved.detectionFailed;
        result->recorded = saved.userSet ? saved.changed : saved.recorded;
    }
    return true;
}

std::vector<DriveRecord> DriveResolver::List() const {
    if (!CanUseStore()) return {};
    return DriveTypeStore(storage_->DriveTypeFile()).Load();
}

} // namespace sfv
