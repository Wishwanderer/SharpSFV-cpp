// SharpSFV (lean C++ rewrite) - files and folders.
// Port of src/core/FileSystem.* (the parts the hashing engine needs) onto Str/Vec.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: helpers for DriveType.ini (folders, atomic writes, cross-process lock).
// Modified 2026-10-06: TextWriter::Flush is public (SharpSFV Lite flushes its log as it goes).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "Str.h"
#include "Text.h"

namespace sfv::fs {

#ifdef _WIN32
inline constexpr const char* kNewLine = "\r\n";
// Path for Windows APIs: UTF-16, with the \\?\ prefix for long absolute paths.
text::WStr ToWinPath(StrView path);
#else
inline constexpr const char* kNewLine = "\n";
#endif

bool FileExists(StrView path);      // File.Exists (false for directories)
bool DirectoryExists(StrView path); // Directory.Exists
i64 GetFileSize(StrView path);      // -1 on error

// Breadth-first, like Directory.EnumerateFiles with IgnoreInaccessible and
// AttributesToSkip = ReparsePoint | Hidden | System. The callback returns false to stop.
void EnumerateFiles(const Str& root, bool recursive, FnRef<bool(const Str& fullPath, i64 size)> cb);

class FileReader {
public:
    FileReader() = default;
    ~FileReader() { Close(); }
    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;

    bool Open(StrView path, bool shareAll = false);
    i64 Size() const { return size_; }
    i64 Read(u8* buf, usize len); // bytes read, 0 at end, -1 on error
    void Close();

private:
#ifdef _WIN32
    void* h_ = nullptr;
#else
    int fd_ = -1;
#endif
    i64 size_ = -1;
};

class TextWriter {
public:
    TextWriter() = default;
    ~TextWriter() { Close(); }
    TextWriter(const TextWriter&) = delete;
    TextWriter& operator=(const TextWriter&) = delete;

    bool Open(StrView path, Str* error = nullptr);
    void Write(StrView s);
    void WriteLine(StrView s) {
        Write(s);
        Write(kNewLine);
    }
    bool Close(Str* error = nullptr);
    bool Flush(); // writes what is buffered; false once a write has failed

private:
#ifdef _WIN32
    void* h_ = nullptr;
#else
    int fd_ = -1;
#endif
    Str buf_;
    bool failed_ = false;
};

bool ReadAllBytes(StrView path, Str& out, Str* error = nullptr);
Str ExecutableDirectory();

bool RemoveFile(StrView path);             // File.Delete
bool CreateDirectories(StrView path);      // Directory.CreateDirectory (with parents)
bool RemoveDirectoryIfEmpty(StrView path);
// Writes `content` to `<path>.tmp`, then replaces `path` with it in one step, so a reader
// (or a crash mid-write) never sees a half-written file.
bool WriteFileAtomic(StrView path, StrView content, Str* error = nullptr);
// %LOCALAPPDATA% (C:\Users\<name>\AppData\Local) on Windows; $XDG_DATA_HOME or
// ~/.local/share elsewhere. "" if unknown.
Str LocalAppDataDirectory();

// Serializes read-modify-write cycles on shared files across SharpSFV processes
// (Explorer can start several at once). Windows: named mutex; POSIX: flock on `dir`.
class CrossProcessLock {
public:
    CrossProcessLock(StrView name, StrView dir);
    ~CrossProcessLock();
    CrossProcessLock(const CrossProcessLock&) = delete;
    CrossProcessLock& operator=(const CrossProcessLock&) = delete;

private:
#ifdef _WIN32
    void* h_ = nullptr;
#else
    int fd_ = -1;
#endif
};

} // namespace sfv::fs
