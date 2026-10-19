// SharpSFV Lite - a stand-in for the Inno Setup script, for tests under Windows or Wine.
// It does what the spec's "What the Inno script does" section describes: creates the two
// events, starts SharpSFV-Lite.exe without waiting, reads the status file every 200 ms, and
// sets Pause / Stop. It then checks what it saw.
//
//   sfvlite_host <SharpSFV-Lite.exe> <list> <base> <work folder> full|pause|stop|parent
//
//   full    run to the end; states only move forward; worker rows constant; exit code = exit line
//   pause   pause once RUNNING; bytes and elapsed time must not move for 1 s; resume; run to the end
//   stop    stop once RUNNING; STOPPED with exit code 3
//   parent  start the tool, pause it, exit after 1.5 s (the caller checks for STOPPED in the status file)
//   A "-ssd" suffix (full-ssd, pause-ssd, stop-ssd) adds -mode ssd (Wine reports no drive type).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-06: added (SharpSFV Lite).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "base/FileSystem.h"
#include "base/Path.h"
#include "base/System.h"
#include "base/Text.h"

#include <windows.h>

using namespace sfv;

namespace {

int g_failures = 0;

void Check(bool ok, StrView what) {
    if (ok) return;
    ++g_failures;
    console::WriteLine(Str("FAIL ") + what);
}

struct Status {
    Str state, seq, progress, counts, bytes, time, exit, error;
    int workers = 0;
    bool valid = false;
};

Status ReadStatus(const Str& path) {
    Status st;
    // Opened like a script's file read would: read access, no delete sharing (so the tool's
    // replace fails while we hold it, and it must simply try again 200 ms later).
    HANDLE h = CreateFileW(text::WStr(path).C(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return st;
    Str raw;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(h, buf, sizeof buf, &got, nullptr) && got > 0) raw.Append(StrView(buf, got));
    Sleep(5); // hold it briefly, as a slow reader might
    CloseHandle(h);
    StrView v = raw;
    if (!v.StartsWith("\xEF\xBB\xBF")) return st;
    v = v.Sub(3);
    for (const Str& line : text::SplitLines(v)) {
        const usize sp = line.Find(' ');
        const StrView key = line.Sub(0, sp), val = sp == kNpos ? StrView() : line.Sub(sp + 1);
        if (key == "state") st.state = val;
        else if (key == "seq") st.seq = val;
        else if (key == "progress") st.progress = val;
        else if (key == "counts") st.counts = val;
        else if (key == "bytes") st.bytes = val;
        else if (key == "time") st.time = val;
        else if (key == "exit") st.exit = val;
        else if (key == "error") st.error = val;
        else if (key == "worker") ++st.workers;
    }
    st.valid = !st.state.Empty() && !st.exit.Empty();
    return st;
}

int Rank(StrView state) {
    if (state == "SCANNING") return 0;
    if (state == "RUNNING" || state == "PAUSED") return 1;
    return 2; // DONE, STOPPED, FAILED
}

bool Final(StrView state) { return state == "DONE" || state == "STOPPED" || state == "FAILED"; }

Str Quote(StrView s) { return "\"" + Str(s) + "\""; }

} // namespace

int main() {
    const Vec<Str> a = CommandLineArguments();
    if (a.Size() != 5) {
        console::WriteLine("usage: sfvlite_host <SharpSFV-Lite.exe> <list> <base> <work folder> full|pause|stop|parent");
        return 2;
    }
    const Str& exe = a[0];
    Str scenario = a[4];
    const bool ssd = scenario.EndsWith("-ssd");
    if (ssd) scenario = Str(scenario.Sub(0, scenario.Size() - 4));
    const Str statusPath = path::Combine(a[3], "status-" + scenario + ".txt");
    const Str logPath = path::Combine(a[3], "verify-" + scenario + ".log");
    const Str control = "SfvLiteHost-" + ToStr(static_cast<i64>(GetCurrentProcessId())) + "-" + scenario;
    fs::RemoveFile(statusPath);

    HANDLE pauseEv = CreateEventW(nullptr, TRUE, FALSE, text::WStr("Local\\" + control + ".pause").C());
    HANDLE stopEv = CreateEventW(nullptr, TRUE, FALSE, text::WStr("Local\\" + control + ".stop").C());
    Check(pauseEv && stopEv, "events created");

    Str cmd = Quote(exe) + " " + Quote(a[1]) + " -base " + Quote(a[2]) + " -status " + Quote(statusPath) + " -log " +
              Quote(logPath) + " -control " + Quote(control) + " -threads 4" + (ssd ? " -mode ssd" : "");
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    text::WStr wcmd(cmd);
    if (!CreateProcessW(nullptr, wcmd.Data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        console::WriteLine("FAIL cannot start " + exe);
        return 1;
    }
    CloseHandle(pi.hThread);
    if (scenario == "parent") { // pause it (so it is still running), then leave: the tool must stop
        SetEvent(pauseEv);
        Sleep(1500);
        console::WriteLine("parent: started and paused the tool, exiting");
        ExitProcess(0);
    }

    Str states, lastState;
    i64 lastSeq = -1;
    int rows = -1;
    bool paused = false, resumed = false, stopped = false;
    Status st;
    const i64 t0 = TickMs();
    for (;;) {
        Sleep(200);
        st = ReadStatus(statusPath);
        if (!st.valid) continue; // not written yet, or being replaced
        i64 seq = 0;
        for (char c : st.seq.View()) seq = seq * 10 + (c - '0');
        Check(seq >= lastSeq, "seq never goes back");
        lastSeq = seq;
        if (st.state != lastState) {
            Check(lastState.Empty() || Rank(st.state) >= Rank(lastState), "states only move forward: " + lastState + " -> " + st.state);
            states += (states.Empty() ? "" : " ") + st.state;
            lastState = st.state;
        }
        if (st.state == "RUNNING" || st.state == "PAUSED") {
            if (rows < 0) rows = st.workers;
            Check(st.workers == rows, "worker rows constant");
        }
        if (scenario == "pause" && st.state == "RUNNING" && !paused) {
            SetEvent(pauseEv);
            paused = true;
            const i64 asked = TickMs();
            Status p;
            do {
                Sleep(50);
                p = ReadStatus(statusPath);
            } while ((!p.valid || p.state != "PAUSED") && !Final(p.state) && TickMs() - asked < 3000);
            if (p.state == "PAUSED") {
                states += " PAUSED";
                lastState = "PAUSED";
                Sleep(1000);
                const Status q = ReadStatus(statusPath);
                Check(q.state == "PAUSED", "still paused after 1 s");
                Check(q.bytes == p.bytes, "bytes do not move while paused (" + p.bytes + " / " + q.bytes + ")");
                Check(q.time == p.time, "elapsed does not move while paused (" + p.time + " / " + q.time + ")");
                Check(q.time.EndsWith(" -"), "no remaining time while paused");
                console::WriteLine("paused: bytes " + q.bytes + ", time " + q.time + ", pause reaction " +
                                   ToStr(TickMs() - asked - 1000) + " ms");
                ResetEvent(pauseEv);
                resumed = true;
            } else {
                console::WriteLine("note: finished before the pause took effect (" + p.state + ")");
            }
        }
        if (scenario == "stop" && st.state == "RUNNING" && !stopped) {
            SetEvent(stopEv);
            stopped = true;
        }
        if (Final(st.state)) break;
        if (TickMs() - t0 > 600000) {
            Check(false, "finished within 10 minutes");
            TerminateProcess(pi.hProcess, 9);
            break;
        }
    }
    WaitForSingleObject(pi.hProcess, 10000);
    DWORD code = 99;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    Check(st.exit == ToStr(static_cast<i64>(code)), "status exit line (" + st.exit + ") = process exit code (" +
                                                         ToStr(static_cast<i64>(code)) + ")");
    if (scenario == "full" || scenario == "pause") Check(st.state == "DONE", "ends DONE");
    if (scenario == "pause") Check(resumed || st.state == "DONE", "paused and resumed");
    if (scenario == "stop") {
        Check(st.state == "STOPPED" && code == 3, "Stop gives STOPPED and exit code 3");
        Str log;
        fs::ReadAllBytes(logPath, log);
        Check(log.Find("Stopped by user.") != kNpos, "log says Stopped by user");
    }
    console::WriteLine(scenario + ": states " + states + "; counts " + st.counts + "; progress " + st.progress + "; rows " +
                       ToStr(rows < 0 ? 0 : rows) + "; exit " + ToStr(static_cast<i64>(code)) + "; " +
                       (g_failures == 0 ? "PASS" : "FAIL"));
    CloseHandle(pauseEv);
    CloseHandle(stopEv);
    return g_failures == 0 ? 0 : 1;
}
