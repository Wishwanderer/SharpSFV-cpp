// SharpSFV Lite - operating-system services (see LitePlatform.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-06: added (SharpSFV Lite).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "LitePlatform.h"

#include "../base/FileSystem.h"
#include "../base/Text.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#else
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

namespace sfv::lite::plat {

namespace {

void AppendPadded(Str& s, int v, int width) {
    char digits[12];
    int n = 0;
    do {
        digits[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v > 0 && n < 11);
    for (int i = n; i < width; ++i) s.Append('0');
    while (n > 0) s.Append(digits[--n]);
}

Str FormatStamp(int y, int mo, int d, int h, int mi, int sec) {
    Str s;
    AppendPadded(s, y, 4);
    s.Append('-');
    AppendPadded(s, mo, 2);
    s.Append('-');
    AppendPadded(s, d, 2);
    s.Append(' ');
    AppendPadded(s, h, 2);
    s.Append(':');
    AppendPadded(s, mi, 2);
    s.Append(':');
    AppendPadded(s, sec, 2);
    return s;
}

} // namespace

#ifdef _WIN32

void SleepMs(u32 ms) { Sleep(ms); }

Str NowText() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    return FormatStamp(t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

Str LastErrorText() {
    const DWORD code = GetLastError();
    wchar_t* msg = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&msg), 0, nullptr);
    Str text;
    if (n > 0 && msg) {
        text = text::Narrow(msg, n);
        LocalFree(msg);
    }
    // One line, no trailing full stop: "The process cannot access the file ... process".
    while (!text.Empty() && (text.Back() == '\n' || text.Back() == '\r' || text.Back() == ' ' || text.Back() == '.'))
        text.PopBack();
    for (usize i = 0; i < text.Size(); ++i)
        if (text[i] == '\r' || text[i] == '\n') text[i] = ' ';
    if (text.Empty()) text = "Windows error " + ToStr(static_cast<i64>(code));
    return text;
}

bool MoveReplace(StrView from, StrView to) {
    const text::WStr wf = fs::ToWinPath(from), wt = fs::ToWinPath(to);
    // No MOVEFILE_WRITE_THROUGH: the status file is rewritten five times a second and is
    // worthless after a power cut; flushing it to the disk each time would only slow things.
    return MoveFileExW(wf.C(), wt.C(), MOVEFILE_REPLACE_EXISTING) != 0;
}

EventControl::EventControl(StrView name) {
    const text::WStr pauseName(Str("Local\\") + name + ".pause");
    const text::WStr stopName(Str("Local\\") + name + ".stop");
    pause_ = CreateEventW(nullptr, TRUE, FALSE, pauseName.C());
    stop_ = CreateEventW(nullptr, TRUE, FALSE, stopName.C());
    ok_ = pause_ && stop_;
}

EventControl::~EventControl() {
    if (pause_) CloseHandle(pause_);
    if (stop_) CloseHandle(stop_);
}

void EventControl::Poll(bool& pause, bool& stop) {
    pause = pause_ && WaitForSingleObject(pause_, 0) == WAIT_OBJECT_0;
    stop = stop_ && WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0;
}

ProcessParentWatch::ProcessParentWatch() {
    const DWORD self = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof pe;
    DWORD parentId = 0;
    FILETIME selfCreated{}, dummy{};
    GetProcessTimes(GetCurrentProcess(), &selfCreated, &dummy, &dummy, &dummy);
    for (BOOL more = Process32FirstW(snap, &pe); more; more = Process32NextW(snap, &pe))
        if (pe.th32ProcessID == self) {
            parentId = pe.th32ParentProcessID;
            break;
        }
    CloseHandle(snap);
    if (parentId == 0) return;
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parentId);
    if (!h) return;
    // Process ids are reused: a "parent" created after this process is not our parent (ours
    // has already exited), so there is nothing to watch.
    FILETIME created{};
    if (GetProcessTimes(h, &created, &dummy, &dummy, &dummy) && CompareFileTime(&created, &selfCreated) > 0) {
        CloseHandle(h);
        return;
    }
    parent_ = h;
}

ProcessParentWatch::~ProcessParentWatch() {
    if (parent_) CloseHandle(parent_);
}

bool ProcessParentWatch::ParentGone() { return parent_ && WaitForSingleObject(parent_, 0) == WAIT_OBJECT_0; }

#else // POSIX (the test build)

void SleepMs(u32 ms) {
    timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = static_cast<long>(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

Str NowText() {
    const time_t now = time(nullptr);
    tm t{};
    localtime_r(&now, &t);
    return FormatStamp(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}

Str LastErrorText() { return Str(strerror(errno)); }

bool MoveReplace(StrView from, StrView to) { return ::rename(Str(from).CStr(), Str(to).CStr()) == 0; }

namespace {
Str TempDir() {
    const char* t = getenv("TMPDIR");
    return Str(t && *t ? t : "/tmp");
}
bool Exists(const Str& p) {
    struct stat st;
    return ::stat(p.CStr(), &st) == 0;
}
} // namespace

EventControl::EventControl(StrView name) {
    const Str dir = TempDir();
    pausePath_ = dir + "/" + name + ".pause";
    stopPath_ = dir + "/" + name + ".stop";
    ok_ = true;
}

EventControl::~EventControl() = default;

void EventControl::Poll(bool& pause, bool& stop) {
    pause = Exists(pausePath_);
    stop = Exists(stopPath_);
}

ProcessParentWatch::ProcessParentWatch() : ppid_(static_cast<int>(getppid())) {}
ProcessParentWatch::~ProcessParentWatch() = default;
bool ProcessParentWatch::ParentGone() { return static_cast<int>(getppid()) != ppid_; }

#endif

} // namespace sfv::lite::plat
