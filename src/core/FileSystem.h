// SharpSFV (C++ port) - file system access.
// Replaces File.Exists / Directory.Exists / FileInfo.Length / Directory.EnumerateFiles
// (with the EnumerationOptions the original uses) / File.OpenHandle + RandomAccess.Read,
// and StreamWriter output.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace sfv::fs {

#ifdef _WIN32
inline constexpr const char* kNewLine = "\r\n";
// Converts a UTF-8 path to a wide path, adding the \\?\ prefix for long absolute paths
// (.NET does this transparently).
std::wstring ToWinPath(std::string_view path);
#else
inline constexpr const char* kNewLine = "\n";
#endif

bool FileExists(std::string_view path);      // File.Exists (false for directories)
bool DirectoryExists(std::string_view path); // Directory.Exists
std::int64_t GetFileSize(std::string_view path); // FileInfo.Length, -1 on error

// Directory.EnumerateFiles(root, "*", new EnumerationOptions {
//     IgnoreInaccessible = true, RecurseSubdirectories = recursive,
//     AttributesToSkip = ReparsePoint | Hidden | System })
// Yields files in the same order as .NET: directory order, breadth-first across
// subdirectories. Returned paths are root + separator + relative (root kept as given).
// The callback returns false to stop enumeration.
using EnumCallback = std::function<bool(const std::string& fullPath, std::int64_t size)>;
void EnumerateFiles(const std::string& root, bool recursive, const EnumCallback& cb);

// Sequential read-only file handle (FileShare.Read, FileOptions.SequentialScan).
class FileReader {
public:
    FileReader() = default;
    ~FileReader();
    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;

    // shareAll: let other processes write, rename or replace the file while it is open
    // (used for small settings files so an atomic replace is never blocked by a reader).
    // Hashing uses the default, FileShare.Read like the original.
    bool Open(std::string_view path, bool shareAll = false);
    std::int64_t Size() const { return size_; }
    // Returns bytes read, 0 at end of file, -1 on error.
    std::int64_t Read(std::uint8_t* buf, std::size_t len);
    void Close();

private:
#ifdef _WIN32
    void* h_ = nullptr;
#else
    int fd_ = -1;
#endif
    std::int64_t size_ = -1;
};

// Buffered UTF-8 (no BOM) text writer, truncating an existing file — StreamWriter(path).
class TextWriter {
public:
    TextWriter() = default;
    ~TextWriter();
    TextWriter(const TextWriter&) = delete;
    TextWriter& operator=(const TextWriter&) = delete;

    bool Open(std::string_view path, std::string* error = nullptr);
    void Write(std::string_view s);
    void WriteLine(std::string_view s) {
        Write(s);
        Write(kNewLine);
    }
    bool Close(std::string* error = nullptr); // flushes

private:
    bool Flush();
#ifdef _WIN32
    void* h_ = nullptr;
#else
    int fd_ = -1;
#endif
    std::string buf_;
    bool failed_ = false;
};

// Reads a whole file as bytes. nullopt on failure.
std::optional<std::string> ReadAllBytes(std::string_view path, std::string* error = nullptr);

// Directory containing the running executable.
std::string ExecutableDirectory();

bool RemoveFile(std::string_view path); // File.Delete

// Creates a directory and any missing parents (Directory.CreateDirectory).
bool CreateDirectories(std::string_view path);
// Removes a directory only if it is empty.
bool RemoveDirectoryIfEmpty(std::string_view path);

// Writes `content` to `<path>.tmp`, then atomically replaces `path` with it, so a reader
// (or a crash mid-write) never sees a half-written file.
bool WriteFileAtomic(const std::string& path, std::string_view content, std::string* error = nullptr);

// The per-user local application data folder: %LOCALAPPDATA% on Windows
// (C:\Users\<name>\AppData\Local), $XDG_DATA_HOME or ~/.local/share elsewhere.
std::string LocalAppDataDirectory();

// Serializes read-modify-write cycles on shared files across SharpSFV processes
// (Explorer can start several at once). Windows: named mutex; POSIX: flock on `dir`.
class CrossProcessLock {
public:
    CrossProcessLock(const std::string& name, const std::string& dir);
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
