// SharpSFV (lean C++ rewrite) - DriveType.ini and local data storage consent (see DriveStore.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "DriveStore.h"

#include "../base/FileSystem.h"
#include "../base/Path.h"
#include "../base/System.h"
#include "../base/Text.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

namespace sfv {

namespace {

constexpr const char* kLockName = "SharpSFV_DataStorage";
// Every file SharpSFV may create in the data folder. Withdrawing consent deletes these.
constexpr const char* kStoredFiles[] = {"DriveType.ini", "DriveType.ini.tmp"};

Str Pad2(int n) {
    Str s;
    s.Append(static_cast<char>('0' + n / 10 % 10)).Append(static_cast<char>('0' + n % 10));
    return s;
}

Str NowStamp() { // "%Y-%m-%d %H:%M", local time
    int y, mo, d, h, mi;
#ifdef _WIN32
    SYSTEMTIME t;
    GetLocalTime(&t);
    y = t.wYear, mo = t.wMonth, d = t.wDay, h = t.wHour, mi = t.wMinute;
#else
    const time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    y = tm.tm_year + 1900, mo = tm.tm_mon + 1, d = tm.tm_mday, h = tm.tm_hour, mi = tm.tm_min;
#endif
    return ToStr(y) + "-" + Pad2(mo) + "-" + Pad2(d) + " " + Pad2(h) + ":" + Pad2(mi);
}

const char* TypeName(bool isHdd) { return isHdd ? "HDD" : "SSD"; }

Opt<bool> ParseType(StrView v) {
    if (text::EqualsIgnoreCase(text::Trim(v), "HDD")) return true;
    if (text::EqualsIgnoreCase(text::Trim(v), "SSD")) return false;
    return {};
}

Str NetworkSection(const drive::VolumeInfo& v) { return "Network:" + v.mountPoint; }

void FillIdentity(DriveRecord& r, const drive::VolumeInfo& v) {
    r.guid = v.network ? Str() : v.guid;
    r.serial = v.network ? Str() : v.serial;
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

// ------------------------------------------------------------------ DataStorage

DataStorage::DataStorage(AppSettings& settings) : settings_(settings), dirKnown_(false) {}
DataStorage::DataStorage(AppSettings& settings, StrView directory)
    : settings_(settings), dir_(directory), dirKnown_(true) {}

const Str& DataStorage::Directory() const {
    if (!dirKnown_) {
        const Str base = fs::LocalAppDataDirectory();
        dir_ = base.Empty() ? Str() : path::Combine(base, "SharpSFV");
        dirKnown_ = true;
    }
    return dir_;
}

Str DataStorage::DriveTypeFile() const {
    return Directory().Empty() ? Str() : path::Combine(Directory(), "DriveType.ini");
}

bool DataStorage::SetConsent(DataConsent c, Vec<Str>* removed, Str* error) {
    const DataConsent previous = settings_.dataStorageConsent;
    settings_.dataStorageConsent = c;
    if (!settings_.SaveCurrent()) {
        settings_.dataStorageConsent = previous;
        if (error) *error = "Could not save " + settings_.IniPath() + " (is the program folder writable?)";
        return false;
    }
    if (c == DataConsent::Declined && !Directory().Empty()) {
        fs::CrossProcessLock lock(kLockName, Directory());
        for (const char* name : kStoredFiles) {
            const Str f = path::Combine(Directory(), name);
            if (fs::FileExists(f) && fs::RemoveFile(f) && removed) removed->Push(f);
        }
        // Remove the folder too if nothing else lives there.
        fs::RemoveDirectoryIfEmpty(Directory());
    }
    return true;
}

Str DataStorage::ConsentTitle() { return "SharpSFV - Local Data Storage"; }

Str DataStorage::ConsentMessage() const {
    const Str file = DriveTypeFile().Empty() ? Str("(local app data folder)\\SharpSFV\\DriveType.ini") : DriveTypeFile();
    // No hard line breaks inside paragraphs: the message box wraps text to its own width.
    return "SharpSFV would like to save a small settings file on this computer:\n"
           "\n" + file + "\n"
           "\n"
           "For each drive you check files on, it records:\n"
           "• the drive's volume GUID, serial number, label and drive letter\n"
           "• whether to read it as an SSD (in parallel) or an HDD (one file at a time)\n"
           "• whether that came from automatic detection or from your own setting\n"
           "\n"
           "Each drive is then detected only once, and your own setting for a drive is always used. "
           "This helps with encrypted drives (such as VeraCrypt) that can't be detected reliably.\n"
           "\n"
           "The file stays on this computer. You can withdraw permission at any time, which deletes it. "
           "If you choose No, nothing is stored: drives are detected every time, and the Processing Mode "
           "option (Auto / HDD / SSD) still works.\n"
           "\n"
           "Allow SharpSFV to store this file?";
}

DataConsent DataStorage::AskOwned(void* ownerWindow, Str* error) {
#ifdef _WIN32
    // user32.dll is loaded only for this prompt, not at every start.
    DataConsent answer = DataConsent::Declined;
    if (HMODULE user = LoadLibraryW(L"user32.dll")) {
        using BoxFn = int(WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT);
        if (auto box = reinterpret_cast<BoxFn>(reinterpret_cast<void*>(GetProcAddress(user, "MessageBoxW")))) {
            const text::WStr msg(ConsentMessage()), title(ConsentTitle());
            const int r = box(static_cast<HWND>(ownerWindow), msg.C(), title.C(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_SETFOREGROUND);
            answer = r == IDYES ? DataConsent::Allowed : DataConsent::Declined;
        }
        FreeLibrary(user);
    }
#else
    (void)ownerWindow;
    console::WriteLine(ConsentTitle());
    console::WriteLine();
    console::WriteLine(ConsentMessage());
    console::Write("[y/N] ");
    char buf[64] = {};
    usize n = 0;
    while (n < sizeof buf - 1) { // one line from stdin
        char c;
        if (::read(0, &c, 1) != 1 || c == '\n') break;
        buf[n++] = c;
    }
    const StrView a = text::Trim(StrView(buf, n));
    const DataConsent answer = (text::EqualsIgnoreCase(a, "y") || text::EqualsIgnoreCase(a, "yes"))
                                   ? DataConsent::Allowed
                                   : DataConsent::Declined;
#endif
    if (!SetConsent(answer, nullptr, error)) return Consent();
    return answer;
}

// ------------------------------------------------------------------ DriveTypeStore

Vec<DriveRecord> DriveTypeStore::Load() const {
    if (file_.Empty() || !fs::FileExists(file_)) return {};
    Str bytes;
    if (!fs::ReadAllBytes(file_, bytes)) return {};
    return Parse(text::DecodeTextFile(bytes));
}

Vec<DriveRecord> DriveTypeStore::Parse(StrView content) {
    Vec<DriveRecord> out;
    DriveRecord cur;
    bool inSection = false, typeValid = false;
    auto flush = [&] {
        if (inSection && typeValid) out.Push(Move(cur));
        cur = DriveRecord();
        inSection = false;
        typeValid = false;
    };
    for (const Str& raw : text::SplitLines(content)) {
        const StrView line = text::Trim(raw);
        if (line.Empty() || line.StartsWith(";") || line.StartsWith("#")) continue;
        if (line.StartsWith("[") && line.EndsWith("]")) {
            flush();
            inSection = true;
            cur.section = line.Sub(1, line.Size() - 2);
            continue;
        }
        if (!inSection) continue;
        const usize eq = line.Find('=');
        if (eq == kNpos) continue;
        const StrView key = text::Trim(line.Sub(0, eq));
        const Str val(text::Trim(line.Sub(eq + 1)));
        auto is = [&](const char* k) { return text::EqualsIgnoreCase(key, k); };
        if (is("VolumeGUID")) cur.guid = val;
        else if (is("SerialNumber")) cur.serial = val;
        else if (is("FileSystem")) cur.fileSystem = val;
        else if (is("Label")) cur.label = val;
        else if (is("MountPoint")) cur.mountPoint = val;
        else if (is("Type")) {
            if (Opt<bool> t = ParseType(val)) {
                cur.isHdd = *t;
                typeValid = true;
            }
        } else if (is("Source")) cur.userSet = text::EqualsIgnoreCase(val, "User");
        else if (is("Detected")) { if (Opt<bool> t = ParseType(val)) cur.detectedIsHdd = *t; }
        else if (is("DetectionFailed")) cur.detectionFailed = val == "1";
        else if (is("DetectedBy")) cur.detectedBy = val;
        else if (is("Recorded")) cur.recorded = val;
        else if (is("Changed")) cur.changed = val;
    }
    flush();
    return out;
}

Str DriveTypeStore::Serialize(const Vec<DriveRecord>& records) {
    Str o;
    auto line = [&](StrView l) {
        o += l;
        o += fs::kNewLine;
    };
    line("; SharpSFV drive classification - one section per volume. Safe to edit by hand.");
    line(";   Type   = SSD (files read in parallel) or HDD (files read one at a time). This is what SharpSFV uses.");
    line(";   Source = Auto (detected automatically, once) or User (your own setting; never re-detected).");
    line("; Delete a section to have that drive detected again; delete this file to forget every drive.");
    for (const DriveRecord& r : records) {
        line("");
        line("[" + r.section + "]");
        if (!r.guid.Empty()) line("VolumeGUID=" + r.guid);
        if (!r.serial.Empty()) line("SerialNumber=" + r.serial);
        if (!r.fileSystem.Empty()) line("FileSystem=" + r.fileSystem);
        if (!r.label.Empty()) line("Label=" + r.label);
        if (!r.mountPoint.Empty()) line("MountPoint=" + r.mountPoint);
        line(Str("Type=") + TypeName(r.isHdd));
        line(Str("Source=") + (r.userSet ? "User" : "Auto"));
        line(Str("Detected=") + TypeName(r.detectedIsHdd));
        if (r.detectionFailed) line("DetectionFailed=1");
        if (!r.detectedBy.Empty()) line("DetectedBy=" + r.detectedBy);
        if (!r.recorded.Empty()) line("Recorded=" + r.recorded);
        if (r.userSet && !r.changed.Empty()) line("Changed=" + r.changed);
    }
    return o;
}

bool DriveTypeStore::Update(FnRef<bool(Vec<DriveRecord>&)> update, Str* error) const {
    const Str dir = path::GetDirectoryName(file_).ValueOr(Str());
    if (dir.Empty() || !fs::CreateDirectories(dir)) {
        if (error) *error = "Could not create " + dir;
        return false;
    }
    fs::CrossProcessLock lock(kLockName, dir);
    Vec<DriveRecord> records = Load(); // re-read under the lock
    if (!update(records)) return true;
    return fs::WriteFileAtomic(file_, Serialize(records), error);
}

bool DriveTypeStore::SetType(StrView section, bool isHdd, Str* error) const {
    bool found = false;
    const bool ok = Update(
        [&](Vec<DriveRecord>& records) {
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
    if (ok && !found && error) *error = "No drive named " + Str(section);
    return ok && found;
}

bool DriveTypeStore::Remove(StrView section, Str* error) const {
    bool found = false;
    const bool ok = Update(
        [&](Vec<DriveRecord>& records) {
            Vec<DriveRecord> kept;
            for (DriveRecord& r : records)
                if (!text::EqualsIgnoreCase(r.section, section)) kept.Push(Move(r));
            found = kept.Size() != records.Size();
            records = Move(kept);
            return found;
        },
        error);
    return ok && found;
}

int DriveTypeStore::Match(const Vec<DriveRecord>& records, const drive::VolumeInfo& v) {
    const int n = static_cast<int>(records.Size());
    if (v.network) {
        const Str section = NetworkSection(v);
        for (int i = 0; i < n; ++i)
            if (text::EqualsIgnoreCase(records[static_cast<usize>(i)].section, section)) return i;
        return -1;
    }
    // a. same GUID and same serial number
    if (!v.guid.Empty()) {
        for (int i = 0; i < n; ++i) {
            const DriveRecord& r = records[static_cast<usize>(i)];
            if (text::EqualsIgnoreCase(r.guid, v.guid) && text::EqualsIgnoreCase(r.serial, v.serial)) return i;
        }
    }
    // b. same serial number and file system (GUID changed, or is tied to the drive letter)
    if (!v.serial.Empty()) {
        for (int i = 0; i < n; ++i) {
            const DriveRecord& r = records[static_cast<usize>(i)];
            if (!r.section.StartsWith("Network:") && text::EqualsIgnoreCase(r.serial, v.serial) &&
                text::EqualsIgnoreCase(r.fileSystem, v.fileSystem))
                return i;
        }
    }
    return -1;
}

DriveRecord DriveTypeStore::NewRecord(const Vec<DriveRecord>& existing, const drive::VolumeInfo& v) {
    DriveRecord r;
    FillIdentity(r, v);
    const Str base = v.network ? NetworkSection(v)
                               : (v.guid.Empty() ? Str("NoGUID") : "Volume" + v.guid) + "/" +
                                     (v.serial.Empty() ? Str("NoSerial") : v.serial);
    Str name = base;
    for (int k = 2;; ++k) {
        bool clash = false;
        for (const DriveRecord& e : existing)
            if (text::EqualsIgnoreCase(e.section, name)) clash = true;
        if (!clash) break;
        name = base + "#" + ToStr(k);
    }
    r.section = name;
    return r;
}

// ------------------------------------------------------------------ decisions

Str DescribeDecision(const DriveDecision& d) {
    Str s = d.isHdd ? "HDD (Sequential)" : "SSD (Parallel)";
    switch (d.source) {
        case DriveDecision::Source::CommandLine: return s + " - set by -mode";
        case DriveDecision::Source::Setting: return s + " - ProcessingMode setting (all drives)";
        case DriveDecision::Source::UserOverride:
            s += " - your setting for this drive";
            if (!d.recorded.Empty()) s += " (" + d.recorded + ")";
            return s;
        case DriveDecision::Source::Stored:
            s += " - detected " + (d.recorded.Empty() ? Str("earlier") : d.recorded) + ": " + d.detectedBy;
            break;
        case DriveDecision::Source::DetectedSaved: s += " - detected now and saved: " + d.detectedBy; break;
        case DriveDecision::Source::DetectedNotSaved:
            s += " - detected now: " + d.detectedBy;
            if (!d.note.Empty()) s += " (not saved: " + d.note + ")";
            break;
    }
    return s;
}

bool DriveResolver::CanUseStore() const { return storage_ && storage_->Allowed(); }

Str DriveResolver::StoreFile() const { return storage_ ? storage_->DriveTypeFile() : Str(); }

Str DriveResolver::StorageNote() const {
    if (!storage_) return "local data storage unavailable";
    switch (storage_->Consent()) {
        case DataConsent::NotAsked: return "local data storage not allowed yet";
        case DataConsent::Declined: return "local data storage declined";
        default: return storage_->Directory().Empty() ? "no local app data folder" : "";
    }
}

DriveDecision DriveResolver::Resolve(ProcessingMode mode, StrView p, bool fromCommandLine, bool mayStore) const {
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
        d.recorded = r.userSet && !r.changed.Empty() ? r.changed : r.recorded;
    };

    const bool useStore = CanUseStore() && d.volume.HasIdentity();
    if (useStore) {
        const Vec<DriveRecord> records = DriveTypeStore(storage_->DriveTypeFile()).Load();
        if (const int i = DriveTypeStore::Match(records, d.volume); i >= 0) {
            fromRecord(records[static_cast<usize>(i)]);
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
        d.note = d.volume.HasIdentity() ? StorageNote() : Str("the volume has no GUID or serial number");
        return d;
    }
    if (!mayStore) return d;

    const DriveTypeStore store(storage_->DriveTypeFile());
    Str err;
    bool raced = false;
    const bool ok = store.Update(
        [&](Vec<DriveRecord>& records) {
            if (const int i = DriveTypeStore::Match(records, d.volume); i >= 0) { // another instance just wrote it
                fromRecord(records[static_cast<usize>(i)]);
                raced = true;
                return false;
            }
            DriveRecord r = DriveTypeStore::NewRecord(records, d.volume);
            r.isHdd = det.isHdd;
            FillDetection(r, det);
            d.recorded = r.recorded;
            records.Push(Move(r));
            return true;
        },
        &err);
    if (raced) return d;
    if (ok)
        d.source = DriveDecision::Source::DetectedSaved;
    else
        d.note = err.Empty() ? Str("could not write DriveType.ini") : err;
    return d;
}

DriveResolver::Status DriveResolver::Inspect(ProcessingMode mode, StrView p, bool fromCommandLine) const {
    Status s;
    s.volume = identify_(p);
    if (CanUseStore() && s.volume.HasIdentity()) {
        const Vec<DriveRecord> records = DriveTypeStore(storage_->DriveTypeFile()).Load();
        if (const int i = DriveTypeStore::Match(records, s.volume); i >= 0) s.stored = records[static_cast<usize>(i)];
    }
    s.detectedNow = detect_(s.volume);
    s.inUse = Resolve(mode, p, fromCommandLine, false);
    return s;
}

bool DriveResolver::SetDrive(StrView p, Opt<bool> isHdd, DriveDecision* result, Str* error) const {
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
        [&](Vec<DriveRecord>& records) {
            const int i = DriveTypeStore::Match(records, v);
            DriveRecord* r;
            if (i >= 0) {
                r = &records[static_cast<usize>(i)];
                FillIdentity(*r, v); // refresh label / mount point
            } else {
                records.Push(DriveTypeStore::NewRecord(records, v));
                r = &records[records.Size() - 1];
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
                r->changed.Clear();
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

Vec<DriveRecord> DriveResolver::List() const {
    if (!CanUseStore()) return {};
    return DriveTypeStore(storage_->DriveTypeFile()).Load();
}

} // namespace sfv
