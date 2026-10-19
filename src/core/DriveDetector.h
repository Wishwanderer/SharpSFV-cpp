// SharpSFV (C++ port) - volume identification and HDD vs SSD detection.
// Ported from Utils/DriveDetector.cs and the IOCTL parts of Interop/Win32Storage.cs;
// volume identification is new (used by DriveClassifier / DriveType.ini).
//
//   HDD (rotational) -> 1 worker thread, 8 MB reads (avoid head thrashing)
//   SSD / NVMe       -> N worker threads, 512 KB reads
//
// Two defects in the original are corrected here (see README "Deviations"):
//   1. It read a "rotation rate" from offset 36 of STORAGE_DEVICE_DESCRIPTOR. That
//      structure has no rotation-rate field; offset 36 lands in the vendor/product string
//      area, so the value was almost always > 1 and nearly every drive was classed as an
//      HDD. That bogus check is removed.
//   2. A drive reporting "no seek penalty" (the documented SSD signal, also used by the
//      Windows defragmenter) was ignored, so SSDs fell through to a name heuristic. It is
//      now treated as an SSD. (The heuristic's "Samsung" check also compared against an
//      upper-cased string and could never match; it now checks "SAMSUNG".)
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
// Modified 2026-10-03: volume identification; detection reports its method.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include <string>

namespace sfv::drive {

// Who a volume is. On Windows:
//   guid       : "{6f1d5d6a-...}" from GetVolumeNameForVolumeMountPoint (mount manager).
//   serial     : "1A2B-3C4D", the file system's volume serial number (set when the volume
//                was formatted; for an encrypted container it is the inner file system's).
//   mountPoint : "E:\" or a mounted folder "C:\Mount\Data\"; UNC root for network shares.
// Elsewhere: guid = file system UUID when known, serial = statfs fsid, mountPoint from
// /proc/self/mountinfo.
struct VolumeInfo {
    std::string probePath;  // the path that was asked about (full path)
    std::string mountPoint;
    std::string guid;
    std::string serial;
    std::string fileSystem;
    std::string label;
    std::string devicePath; // what the IOCTLs open: \\.\Volume{...} or \\.\E:
    bool network = false;
    std::string driveType = "Unknown"; // Fixed, Removable, Network, ...

    // Identity usable for storing a classification (network shares use their UNC root).
    bool HasIdentity() const { return network ? !mountPoint.empty() : (!guid.empty() || !serial.empty()); }
};

VolumeInfo IdentifyVolume(const std::string& path);

struct Detection {
    bool isHdd = true;
    bool failed = false; // nothing answered: isHdd is the safe default, not a measurement
    std::string method;  // e.g. "volume reports no seek penalty", stored as DetectedBy=
};

// Probes the hardware behind a volume. Never throws; falls back to HDD (sequential): running
// an SSD sequentially is slightly slower, running an HDD in parallel is much slower.
Detection Detect(const VolumeInfo& volume);

// Convenience: IdentifyVolume + Detect.
bool IsRotational(const std::string& path);

// Text report of every probing step ("Help > View current Disk Data", -drive-status).
std::string GetDebugInfo(const std::string& path);

} // namespace sfv::drive
