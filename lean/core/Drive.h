// SharpSFV (lean C++ rewrite) - volume identification and HDD vs SSD detection.
// Port of src/core/DriveDetector.* (same probes, same decision rules, same report text).
//
//   HDD (rotational) -> 1 worker thread, 8 MB reads (avoids head thrashing)
//   SSD / NVMe       -> one worker per logical processor, 512 KB reads
//
// The standard build corrects two defects of the C# original here, and so does this port:
// the bogus "rotation rate" read from STORAGE_DEVICE_DESCRIPTOR is gone, and a drive that
// reports "no seek penalty" (the documented SSD signal) is treated as an SSD.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-04: added for the rewrite without the C++ standard library.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "../base/Str.h"

namespace sfv::drive {

// Who a volume is. On Windows:
//   guid       : "{6f1d5d6a-...}" from GetVolumeNameForVolumeMountPoint (mount manager).
//   serial     : "1A2B-3C4D", the file system's volume serial number.
//   mountPoint : "E:\" or a mounted folder "C:\Mount\Data\"; UNC root for network shares.
// Elsewhere: guid = file system UUID when known, serial = statfs fsid, mountPoint from
// /proc/self/mountinfo.
struct VolumeInfo {
    Str probePath; // the path that was asked about (full path)
    Str mountPoint;
    Str guid;
    Str serial;
    Str fileSystem;
    Str label;
    Str devicePath; // what the IOCTLs open: \\.\Volume{...} or \\.\E:
    bool network = false;
    Str driveType = "Unknown"; // Fixed, Removable, Network, ...

    // Identity usable for storing a classification (network shares use their UNC root).
    bool HasIdentity() const { return network ? !mountPoint.Empty() : (!guid.Empty() || !serial.Empty()); }
};

VolumeInfo IdentifyVolume(StrView path);

struct Detection {
    bool isHdd = true;
    bool failed = false; // nothing answered: isHdd is the safe default, not a measurement
    Str method;          // e.g. "volume reports no seek penalty", stored as DetectedBy=
};

// Probes the hardware behind a volume; falls back to HDD (sequential): running an SSD
// sequentially is slightly slower, running an HDD in parallel is much slower.
Detection Detect(const VolumeInfo& volume);

// Text report of every probing step (-drive-status).
Str GetDebugInfo(StrView path);

} // namespace sfv::drive
