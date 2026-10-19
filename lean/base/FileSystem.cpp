// SharpSFV (lean C++ rewrite) - files and folders (see FileSystem.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: helpers for DriveType.ini (folders, atomic writes, cross-process lock).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "FileSystem.h"

#include "Path.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace sfv::fs {

namespace {
constexpr usize kWriteBuffer = 1 << 20;

Str ChildPath(const Str& dir, StrView name) {
    Str r = dir;
    if (!r.Empty() && !path::IsSep(r.Back())) r.Append(path::kSep);
    r.Append(name);
    return r;
}
} // namespace

#ifdef _WIN32

text::WStr ToWinPath(StrView p) {
    text::WStr w(p);
    if (w.Size() < 248 || (w.Size() >= 4 && wcsncmp(w.C(), L"\\\\?\\", 4) == 0)) return w;
    if (!path::IsPathRooted(p)) return w;
    DWORD n = GetFullPathNameW(w.C(), 0, nullptr, nullptr);
    if (n == 0) return w;
    text::WStr full;
    full.Resize(n);
    n = GetFullPathNameW(w.C(), n, full.Data(), nullptr);
    if (n == 0) return w;
    full.Resize(n);
    text::WStr r;
    if (n >= 2 && full.C()[0] == L'\\' && full.C()[1] == L'\\') {
        r.Append(L"\\\\?\\UNC\\");
        r.Append(full.C() + 2);
    } else {
        r.Append(L"\\\\?\\");
        r.Append(full.C());
    }
    return r;
}

bool FileExists(StrView p) {
    if (p.Empty()) return false;
    const DWORD a = GetFileAttributesW(ToWinPath(p).C());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirectoryExists(StrView p) {
    if (p.Empty()) return false;
    const DWORD a = GetFileAttributesW(ToWinPath(p).C());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

i64 GetFileSize(StrView p) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(ToWinPath(p).C(), GetFileExInfoStandard, &d)) return -1;
    return (static_cast<i64>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
}

void EnumerateFiles(const Str& root, bool recursive, FnRef<bool(const Str&, i64)> cb) {
    constexpr DWORD kSkip = FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
    Vec<Str> pending;
    pending.Push(root);
    for (usize head = 0; head < pending.Size(); ++head) {
        const Str dir = Move(pending[head]);
        if (head >= 1024 && head * 2 >= pending.Size()) { // keep the queue from growing without bound
            pending.RemoveFront(head + 1);
            head = static_cast<usize>(-1);
        }
        text::WStr pattern = ToWinPath(dir);
        if (pattern.Size() > 0 && pattern.Back() != L'\\' && pattern.Back() != L'/') pattern.Append(L'\\');
        pattern.Append(L'*');

        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW(pattern.C(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                    FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) continue; // IgnoreInaccessible
        do {
            const wchar_t* n = fd.cFileName;
            if (n[0] == L'.' && (n[1] == 0 || (n[1] == L'.' && n[2] == 0))) continue;
            if (fd.dwFileAttributes & kSkip) continue;
            Str child = ChildPath(dir, text::Narrow(n));
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (recursive) pending.Push(Move(child));
            } else {
                const i64 size = (static_cast<i64>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                if (!cb(child, size)) {
                    FindClose(h);
                    return;
                }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

bool FileReader::Open(StrView p, bool shareAll) {
    Close();
    const DWORD share = shareAll ? (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE) : FILE_SHARE_READ;
    HANDLE h = CreateFileW(ToWinPath(p).C(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN,
                           nullptr);
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

i64 FileReader::Read(u8* buf, usize len) {
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

bool TextWriter::Open(StrView p, Str* error) {
    Close();
    failed_ = false;
    HANDLE h = CreateFileW(ToWinPath(p).C(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (error) {
            wchar_t* msg = nullptr;
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, GetLastError(), 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
            *error = msg ? Str(text::Trim(text::Narrow(msg))) : Str("Cannot create file");
            if (msg) LocalFree(msg);
        }
        return false;
    }
    h_ = h;
    buf_.Reserve(kWriteBuffer);
    return true;
}

bool TextWriter::Flush() {
    if (!h_ || buf_.Empty()) return !failed_;
    usize off = 0;
    while (off < buf_.Size()) {
        DWORD wrote = 0;
        if (!WriteFile(static_cast<HANDLE>(h_), buf_.Data() + off, static_cast<DWORD>(buf_.Size() - off), &wrote,
                       nullptr)) {
            failed_ = true;
            break;
        }
        off += wrote;
    }
    buf_.Clear();
    return !failed_;
}

bool TextWriter::Close(Str* error) {
    if (!h_) return !failed_;
    const bool ok = Flush();
    CloseHandle(static_cast<HANDLE>(h_));
    h_ = nullptr;
    if (!ok && error) *error = "Write failed";
    return ok;
}

bool ReadAllBytes(StrView p, Str& data, Str* error) {
    FileReader r;
    if (!r.Open(p, true)) {
        if (error) *error = "Could not open file";
        return false;
    }
    data.Resize(static_cast<usize>(r.Size()));
    usize off = 0;
    while (off < data.Size()) {
        const i64 n = r.Read(reinterpret_cast<u8*>(data.MutableData() + off), data.Size() - off);
        if (n < 0) {
            if (error) *error = "Read failed";
            return false;
        }
        if (n == 0) break;
        off += static_cast<usize>(n);
    }
    data.Resize(off);
    return true;
}

Str ExecutableDirectory() {
    text::WStr buf;
    buf.Resize(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.Data(), static_cast<DWORD>(buf.Size()));
        if (n == 0) return {};
        if (n < buf.Size()) return path::GetDirectoryName(text::Narrow(buf.C(), n)).ValueOr(Str());
        buf.Resize(buf.Size() * 2);
    }
}

bool RemoveFile(StrView p) { return DeleteFileW(ToWinPath(p).C()) != 0; }

bool CreateDirectories(StrView p) {
    if (p.Empty()) return false;
    if (DirectoryExists(p)) return true;
    if (Opt<Str> parent = path::GetDirectoryName(p); parent && !parent->Empty() && *parent != p) {
        if (!CreateDirectories(*parent)) return false;
    }
    return CreateDirectoryW(ToWinPath(p).C(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool RemoveDirectoryIfEmpty(StrView p) { return RemoveDirectoryW(ToWinPath(p).C()) != 0; }

bool WriteFileAtomic(StrView p, StrView content, Str* error) {
    const Str tmp = Str(p) + ".tmp";
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
    const text::WStr wtmp = ToWinPath(tmp), wdst = ToWinPath(p);
    for (int attempt = 0;; ++attempt) {
        if (MoveFileExW(wtmp.C(), wdst.C(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        const DWORD e = GetLastError();
        if ((e != ERROR_ACCESS_DENIED && e != ERROR_SHARING_VIOLATION) || attempt >= 100) {
            if (error) *error = "Could not replace " + Str(p) + " (error " + ToStr(static_cast<i64>(e)) + ")";
            RemoveFile(tmp);
            return false;
        }
        Sleep(20);
    }
}

Str LocalAppDataDirectory() {
    // The Local AppData known folder, as the standard build asks for it. shell32 is loaded
    // only here (only when local data storage is in use), not at every start.
    Str result;
    if (HMODULE shell = LoadLibraryW(L"shell32.dll")) {
        using GetFolderFn = HRESULT(WINAPI*)(const GUID&, DWORD, HANDLE, PWSTR*);
        using FreeFn = void(WINAPI*)(void*);
        auto getFolder = reinterpret_cast<GetFolderFn>(reinterpret_cast<void*>(GetProcAddress(shell, "SHGetKnownFolderPath")));
        HMODULE ole = LoadLibraryW(L"ole32.dll");
        auto freeMem = ole ? reinterpret_cast<FreeFn>(reinterpret_cast<void*>(GetProcAddress(ole, "CoTaskMemFree"))) : nullptr;
        static const GUID kLocalAppData = {0xF1B32785, 0x6FBA, 0x4FCF, {0x9D, 0x55, 0x7B, 0x8E, 0x7F, 0x15, 0x70, 0x91}};
        PWSTR p = nullptr;
        if (getFolder && freeMem && SUCCEEDED(getFolder(kLocalAppData, 0, nullptr, &p)) && p) result = text::Narrow(p);
        if (p && freeMem) freeMem(p);
        if (ole) FreeLibrary(ole);
        FreeLibrary(shell);
    }
    if (result.Empty()) {
        wchar_t buf[MAX_PATH];
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) result = text::Narrow(buf, n);
    }
    return result;
}

CrossProcessLock::CrossProcessLock(StrView name, StrView) {
    text::WStr full(Str("Local\\") + name);
    h_ = CreateMutexW(nullptr, FALSE, full.C());
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

#else // POSIX (the Linux test build)

namespace {
Str Z(StrView p) { return Str(p); } // NUL-terminated copy for libc
} // namespace

bool FileExists(StrView p) {
    struct stat st;
    return !p.Empty() && stat(Z(p).CStr(), &st) == 0 && !S_ISDIR(st.st_mode);
}

bool DirectoryExists(StrView p) {
    struct stat st;
    return !p.Empty() && stat(Z(p).CStr(), &st) == 0 && S_ISDIR(st.st_mode);
}

i64 GetFileSize(StrView p) {
    struct stat st;
    if (stat(Z(p).CStr(), &st) != 0) return -1;
    return st.st_size;
}

void EnumerateFiles(const Str& root, bool recursive, FnRef<bool(const Str&, i64)> cb) {
    Vec<Str> pending;
    pending.Push(root);
    for (usize head = 0; head < pending.Size(); ++head) {
        const Str dir = Move(pending[head]);
        if (head >= 1024 && head * 2 >= pending.Size()) {
            pending.RemoveFront(head + 1);
            head = static_cast<usize>(-1);
        }
        DIR* d = opendir(dir.CStr());
        if (!d) continue;
        const int dfd = dirfd(d);
        while (dirent* e = readdir(d)) {
            const char* n = e->d_name;
            if (n[0] == '.') continue; // ".", ".." and dot-files (.NET reports these as Hidden on Unix)
            struct stat st;
            if (fstatat(dfd, n, &st, AT_SYMLINK_NOFOLLOW) != 0) continue;
            if (S_ISLNK(st.st_mode)) continue; // ReparsePoint
            Str child = ChildPath(dir, n);
            if (S_ISDIR(st.st_mode)) {
                if (recursive) pending.Push(Move(child));
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

bool FileReader::Open(StrView p, bool) {
    Close();
    const int fd = open(Z(p).CStr(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || S_ISDIR(st.st_mode)) {
        ::close(fd);
        return false;
    }
    fd_ = fd;
    size_ = st.st_size;
    return true;
}

i64 FileReader::Read(u8* buf, usize len) {
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

bool TextWriter::Open(StrView p, Str* error) {
    Close();
    failed_ = false;
    const int fd = open(Z(p).CStr(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        if (error) *error = strerror(errno);
        return false;
    }
    fd_ = fd;
    buf_.Reserve(kWriteBuffer);
    return true;
}

bool TextWriter::Flush() {
    if (fd_ < 0 || buf_.Empty()) return !failed_;
    usize off = 0;
    while (off < buf_.Size()) {
        const ssize_t n = ::write(fd_, buf_.Data() + off, buf_.Size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            failed_ = true;
            break;
        }
        off += static_cast<usize>(n);
    }
    buf_.Clear();
    return !failed_;
}

bool TextWriter::Close(Str* error) {
    if (fd_ < 0) return !failed_;
    const bool ok = Flush();
    ::close(fd_);
    fd_ = -1;
    if (!ok && error) *error = "Write failed";
    return ok;
}

bool ReadAllBytes(StrView p, Str& data, Str* error) {
    FileReader r;
    if (!r.Open(p)) {
        if (error) *error = strerror(errno);
        return false;
    }
    data.Clear();
    u8 chunk[65536];
    for (;;) {
        const i64 n = r.Read(chunk, sizeof chunk);
        if (n < 0) {
            if (error) *error = strerror(errno);
            return false;
        }
        if (n == 0) break;
        data.Append(StrView(reinterpret_cast<const char*>(chunk), static_cast<usize>(n)));
    }
    return true;
}

Str ExecutableDirectory() {
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return {};
    buf[n] = '\0';
    return path::GetDirectoryName(buf).ValueOr(Str());
}

bool RemoveFile(StrView p) { return ::unlink(Z(p).CStr()) == 0; }

bool CreateDirectories(StrView p) {
    if (p.Empty()) return false;
    if (DirectoryExists(p)) return true;
    if (Opt<Str> parent = path::GetDirectoryName(p); parent && !parent->Empty() && *parent != p) {
        if (!CreateDirectories(*parent)) return false;
    }
    return ::mkdir(Z(p).CStr(), 0700) == 0 || errno == EEXIST;
}

bool RemoveDirectoryIfEmpty(StrView p) { return ::rmdir(Z(p).CStr()) == 0; }

bool WriteFileAtomic(StrView p, StrView content, Str* error) {
    const Str tmp = Str(p) + ".tmp";
    {
        TextWriter w;
        if (!w.Open(tmp, error)) return false;
        w.Write(content);
        if (!w.Close(error)) {
            RemoveFile(tmp);
            return false;
        }
    }
    if (::rename(tmp.CStr(), Z(p).CStr()) != 0) {
        if (error) *error = strerror(errno);
        RemoveFile(tmp);
        return false;
    }
    return true;
}

Str LocalAppDataDirectory() {
    if (const char* x = getenv("XDG_DATA_HOME"); x && *x) return x;
    if (const char* h = getenv("HOME"); h && *h) return Str(h) + "/.local/share";
    return {};
}

CrossProcessLock::CrossProcessLock(StrView, StrView dir) {
    fd_ = ::open(Z(dir).CStr(), O_RDONLY | O_CLOEXEC);
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

void TextWriter::Write(StrView s) {
    if (failed_) return;
    buf_.Append(s);
    if (buf_.Size() >= kWriteBuffer) Flush();
}

} // namespace sfv::fs
