// SharpSFV (C++ port) - file system access.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
// Modified 2026-10-03: atomic writes, AppData folder, cross-process lock; local GUID.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "FileSystem.h"

#include "PathUtil.h"
#include "Text.h"

#include <deque>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#else
#include <cerrno>
#include <cstdlib>
#include <sys/file.h>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <climits>
#endif

namespace sfv::fs {

namespace {
constexpr std::size_t kWriteBuffer = 1 << 20;

std::string ChildPath(const std::string& dir, std::string_view name) {
    std::string r = dir;
    if (!r.empty() && !path::IsSep(r.back())) r.push_back(path::kSep);
    r.append(name);
    return r;
}
} // namespace

#ifdef _WIN32

std::wstring ToWinPath(std::string_view p) {
    std::wstring w = text::Widen(p);
    if (w.size() < 248 || w.rfind(L"\\\\?\\", 0) == 0) return w;
    if (!path::IsPathRooted(p)) return w;

    DWORD n = GetFullPathNameW(w.c_str(), 0, nullptr, nullptr);
    if (n == 0) return w;
    std::wstring full(n, L'\0');
    n = GetFullPathNameW(w.c_str(), n, full.data(), nullptr);
    if (n == 0) return w;
    full.resize(n);
    if (full.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + full.substr(2);
    return L"\\\\?\\" + full;
}

bool FileExists(std::string_view p) {
    if (p.empty()) return false;
    const DWORD a = GetFileAttributesW(ToWinPath(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirectoryExists(std::string_view p) {
    if (p.empty()) return false;
    const DWORD a = GetFileAttributesW(ToWinPath(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

std::int64_t GetFileSize(std::string_view p) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(ToWinPath(p).c_str(), GetFileExInfoStandard, &d)) return -1;
    return (static_cast<std::int64_t>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
}

void EnumerateFiles(const std::string& root, bool recursive, const EnumCallback& cb) {
    constexpr DWORD kSkip = FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
    std::deque<std::string> pending{root};
    while (!pending.empty()) {
        const std::string dir = std::move(pending.front());
        pending.pop_front();

        std::wstring pattern = ToWinPath(dir);
        if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') pattern.push_back(L'\\');
        pattern.push_back(L'*');

        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                    FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) continue; // IgnoreInaccessible
        do {
            const wchar_t* n = fd.cFileName;
            if (n[0] == L'.' && (n[1] == 0 || (n[1] == L'.' && n[2] == 0))) continue;
            if (fd.dwFileAttributes & kSkip) continue;
            std::string child = ChildPath(dir, text::Narrow(n));
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (recursive) pending.push_back(std::move(child));
            } else {
                const std::int64_t size = (static_cast<std::int64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                if (!cb(child, size)) {
                    FindClose(h);
                    return;
                }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

FileReader::~FileReader() { Close(); }

bool FileReader::Open(std::string_view p, bool shareAll) {
    Close();
    const DWORD share = shareAll ? (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE) : FILE_SHARE_READ;
    HANDLE h = CreateFileW(ToWinPath(p).c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz)) {
        CloseHandle(h);
        return false;
    }
    h_ = h;
    size_ = sz.QuadPart;
    return true;
}

std::int64_t FileReader::Read(std::uint8_t* buf, std::size_t len) {
    if (!h_) return -1;
    DWORD got = 0;
    const DWORD want = static_cast<DWORD>(len > (1u << 30) ? (1u << 30) : len);
    if (!ReadFile(static_cast<HANDLE>(h_), buf, want, &got, nullptr)) return -1;
    return got;
}

void FileReader::Close() {
    if (h_) CloseHandle(static_cast<HANDLE>(h_));
    h_ = nullptr;
    size_ = -1;
}

TextWriter::~TextWriter() { Close(); }

bool TextWriter::Open(std::string_view p, std::string* error) {
    Close();
    failed_ = false;
    HANDLE h = CreateFileW(ToWinPath(p).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (error) {
            wchar_t* msg = nullptr;
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, GetLastError(), 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
            *error = msg ? std::string(text::Trim(text::Narrow(msg))) : "Cannot create file";
            if (msg) LocalFree(msg);
        }
        return false;
    }
    h_ = h;
    buf_.reserve(kWriteBuffer);
    return true;
}

bool TextWriter::Flush() {
    if (!h_ || buf_.empty()) return !failed_;
    std::size_t off = 0;
    while (off < buf_.size()) {
        DWORD wrote = 0;
        if (!WriteFile(static_cast<HANDLE>(h_), buf_.data() + off, static_cast<DWORD>(buf_.size() - off), &wrote,
                       nullptr)) {
            failed_ = true;
            break;
        }
        off += wrote;
    }
    buf_.clear();
    return !failed_;
}

bool TextWriter::Close(std::string* error) {
    if (!h_) return !failed_;
    const bool ok = Flush();
    CloseHandle(static_cast<HANDLE>(h_));
    h_ = nullptr;
    if (!ok && error) *error = "Write failed";
    return ok;
}

std::optional<std::string> ReadAllBytes(std::string_view p, std::string* error) {
    FileReader r;
    if (!r.Open(p, true)) {
        if (error) *error = "Could not open file";
        return std::nullopt;
    }
    std::string data;
    data.resize(static_cast<std::size_t>(r.Size()));
    std::size_t off = 0;
    while (off < data.size()) {
        const std::int64_t n = r.Read(reinterpret_cast<std::uint8_t*>(data.data() + off), data.size() - off);
        if (n < 0) {
            if (error) *error = "Read failed";
            return std::nullopt;
        }
        if (n == 0) break;
        off += static_cast<std::size_t>(n);
    }
    data.resize(off);
    return data;
}

std::string ExecutableDirectory() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return path::GetDirectoryName(text::Narrow(buf)).value_or("");
}

bool RemoveFile(std::string_view p) { return DeleteFileW(ToWinPath(p).c_str()) != 0; }

bool CreateDirectories(std::string_view p) {
    if (p.empty()) return false;
    if (DirectoryExists(p)) return true;
    if (auto parent = path::GetDirectoryName(p); parent && !parent->empty() && *parent != p) {
        if (!CreateDirectories(*parent)) return false;
    }
    return CreateDirectoryW(ToWinPath(p).c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool RemoveDirectoryIfEmpty(std::string_view p) { return RemoveDirectoryW(ToWinPath(p).c_str()) != 0; }

bool WriteFileAtomic(const std::string& p, std::string_view content, std::string* error) {
    const std::string tmp = p + ".tmp";
    {
        TextWriter w;
        if (!w.Open(tmp, error)) return false;
        w.Write(content);
        if (!w.Close(error)) {
            RemoveFile(tmp);
            return false;
        }
    }
    // A reader that opened the old file without FILE_SHARE_DELETE (another program, or an
    // older SharpSFV) briefly blocks the replace; readers only hold it for microseconds.
    const std::wstring wtmp = ToWinPath(tmp), wdst = ToWinPath(p);
    for (int attempt = 0;; ++attempt) {
        if (MoveFileExW(wtmp.c_str(), wdst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        const DWORD e = GetLastError();
        if ((e != ERROR_ACCESS_DENIED && e != ERROR_SHARING_VIOLATION) || attempt >= 100) {
            if (error) *error = "Could not replace " + p + " (error " + std::to_string(e) + ")";
            RemoveFile(tmp);
            return false;
        }
        Sleep(20);
    }
}

// FOLDERID_LocalAppData, defined here: referencing the SDK's copy links MinGW's whole libuuid
// GUID table (48 KB) into the executables.
static const GUID kFolderLocalAppData = {0xF1B32785, 0x6FBA, 0x4FCF, {0x9D, 0x55, 0x7B, 0x8E, 0x7F, 0x15, 0x70, 0x91}};

std::string LocalAppDataDirectory() {
    PWSTR p = nullptr;
    std::string result;
    if (SUCCEEDED(SHGetKnownFolderPath(kFolderLocalAppData, 0, nullptr, &p)) && p) result = text::Narrow(p);
    if (p) CoTaskMemFree(p);
    if (result.empty()) {
        wchar_t buf[MAX_PATH];
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) result = text::Narrow(std::wstring_view(buf, n));
    }
    return result;
}

CrossProcessLock::CrossProcessLock(const std::string& name, const std::string&) {
    h_ = CreateMutexW(nullptr, FALSE, (L"Local\\" + text::Widen(name)).c_str());
    if (h_) {
        const DWORD r = WaitForSingleObject(static_cast<HANDLE>(h_), 10000);
        if (r != WAIT_OBJECT_0 && r != WAIT_ABANDONED) { // timed out: proceed unlocked (atomic writes still protect the file)
            CloseHandle(static_cast<HANDLE>(h_));
            h_ = nullptr;
        }
    }
}

CrossProcessLock::~CrossProcessLock() {
    if (h_) {
        ReleaseMutex(static_cast<HANDLE>(h_));
        CloseHandle(static_cast<HANDLE>(h_));
    }
}

#else // POSIX

bool FileExists(std::string_view p) {
    struct stat st;
    return !p.empty() && stat(std::string(p).c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
}

bool DirectoryExists(std::string_view p) {
    struct stat st;
    return !p.empty() && stat(std::string(p).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::int64_t GetFileSize(std::string_view p) {
    struct stat st;
    if (stat(std::string(p).c_str(), &st) != 0) return -1;
    return st.st_size;
}

void EnumerateFiles(const std::string& root, bool recursive, const EnumCallback& cb) {
    std::deque<std::string> pending{root};
    while (!pending.empty()) {
        const std::string dir = std::move(pending.front());
        pending.pop_front();
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        const int dfd = dirfd(d);
        while (dirent* e = readdir(d)) {
            const char* n = e->d_name;
            if (n[0] == '.') continue; // ".", ".." and dot-files (.NET reports these as Hidden on Unix)
            struct stat st;
            if (fstatat(dfd, n, &st, AT_SYMLINK_NOFOLLOW) != 0) continue;
            if (S_ISLNK(st.st_mode)) continue; // ReparsePoint
            std::string child = ChildPath(dir, n);
            if (S_ISDIR(st.st_mode)) {
                if (recursive) pending.push_back(std::move(child));
            } else if (S_ISREG(st.st_mode)) {
                if (!cb(child, st.st_size)) {
                    closedir(d);
                    return;
                }
            }
        }
        closedir(d);
    }
}

FileReader::~FileReader() { Close(); }

bool FileReader::Open(std::string_view p, bool /*shareAll: POSIX never blocks renames*/) {
    Close();
    const int fd = open(std::string(p).c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || S_ISDIR(st.st_mode)) {
        ::close(fd);
        return false;
    }
#ifdef POSIX_FADV_SEQUENTIAL
    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
    fd_ = fd;
    size_ = st.st_size;
    return true;
}

std::int64_t FileReader::Read(std::uint8_t* buf, std::size_t len) {
    if (fd_ < 0) return -1;
    for (;;) {
        const ssize_t n = ::read(fd_, buf, len);
        if (n < 0 && errno == EINTR) continue;
        return n;
    }
}

void FileReader::Close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    size_ = -1;
}

TextWriter::~TextWriter() { Close(); }

bool TextWriter::Open(std::string_view p, std::string* error) {
    Close();
    failed_ = false;
    const int fd = open(std::string(p).c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        if (error) *error = std::strerror(errno);
        return false;
    }
    fd_ = fd;
    buf_.reserve(kWriteBuffer);
    return true;
}

bool TextWriter::Flush() {
    if (fd_ < 0 || buf_.empty()) return !failed_;
    std::size_t off = 0;
    while (off < buf_.size()) {
        const ssize_t n = ::write(fd_, buf_.data() + off, buf_.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            failed_ = true;
            break;
        }
        off += static_cast<std::size_t>(n);
    }
    buf_.clear();
    return !failed_;
}

bool TextWriter::Close(std::string* error) {
    if (fd_ < 0) return !failed_;
    const bool ok = Flush();
    ::close(fd_);
    fd_ = -1;
    if (!ok && error) *error = "Write failed";
    return ok;
}

std::optional<std::string> ReadAllBytes(std::string_view p, std::string* error) {
    FileReader r;
    if (!r.Open(p)) {
        if (error) *error = std::strerror(errno);
        return std::nullopt;
    }
    std::string data;
    std::uint8_t chunk[65536];
    for (;;) {
        const std::int64_t n = r.Read(chunk, sizeof chunk);
        if (n < 0) {
            if (error) *error = std::strerror(errno);
            return std::nullopt;
        }
        if (n == 0) break;
        data.append(reinterpret_cast<const char*>(chunk), static_cast<std::size_t>(n));
    }
    return data;
}

std::string ExecutableDirectory() {
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return {};
    buf[n] = '\0';
    return path::GetDirectoryName(buf).value_or("");
}

bool RemoveFile(std::string_view p) { return ::unlink(std::string(p).c_str()) == 0; }

bool CreateDirectories(std::string_view p) {
    if (p.empty()) return false;
    if (DirectoryExists(p)) return true;
    if (auto parent = path::GetDirectoryName(p); parent && !parent->empty() && *parent != p) {
        if (!CreateDirectories(*parent)) return false;
    }
    return ::mkdir(std::string(p).c_str(), 0700) == 0 || errno == EEXIST;
}

bool RemoveDirectoryIfEmpty(std::string_view p) { return ::rmdir(std::string(p).c_str()) == 0; }

bool WriteFileAtomic(const std::string& p, std::string_view content, std::string* error) {
    const std::string tmp = p + ".tmp";
    {
        TextWriter w;
        if (!w.Open(tmp, error)) return false;
        w.Write(content);
        if (!w.Close(error)) {
            RemoveFile(tmp);
            return false;
        }
    }
    if (::rename(tmp.c_str(), p.c_str()) != 0) {
        if (error) *error = std::strerror(errno);
        RemoveFile(tmp);
        return false;
    }
    return true;
}

std::string LocalAppDataDirectory() {
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return x;
    if (const char* h = std::getenv("HOME"); h && *h) return std::string(h) + "/.local/share";
    return {};
}

CrossProcessLock::CrossProcessLock(const std::string&, const std::string& dir) {
    fd_ = ::open(dir.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd_ >= 0 && ::flock(fd_, LOCK_EX) != 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

CrossProcessLock::~CrossProcessLock() {
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
    }
}

#endif

void TextWriter::Write(std::string_view s) {
    if (failed_) return;
    buf_.append(s);
    if (buf_.size() >= kWriteBuffer) Flush();
}

} // namespace sfv::fs
