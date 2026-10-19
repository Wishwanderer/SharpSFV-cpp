// SharpSFV (C++ port) - hashing algorithms.
// Ported from IO/HashHelper.cs. Backends:
//   XXHASH3 : vendored xxHash (XXH3 128-bit), canonical big-endian output (= .NET XxHash128).
//   Crc32   : built-in slice-by-16 CRC-32 (IEEE), output byte order = .NET Crc32 (see below).
//   MD5/SHA1/SHA256 : Windows CNG (BCrypt) on Windows; OpenSSL on other platforms
//                     (non-Windows builds are for testing and the portable CLI).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "Types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sfv {

// CRC-32 digests are always held in Standard byte order (the CRC value, most-significant
// byte first: "123456789" -> CBF43926). The byte order only matters at the edges:
//   * FormatDigest(..., Crc32ByteOrder) when showing or writing a value;
//   * CompareDigest(..., allowByteReversal) when verifying, which accepts either order.
// CRC-32 is the only algorithm with a 4-byte digest, which is how FormatDigest knows when
// the byte order applies.

// A computed digest (at most 32 bytes: SHA-256).
struct Digest {
    std::array<std::uint8_t, 32> bytes{};
    std::uint8_t size = 0;

    const std::uint8_t* data() const { return bytes.data(); }
    bool empty() const { return size == 0; }
};

class Hasher {
public:
    virtual ~Hasher() = default;

    // Creates a reusable hasher for one thread. Returns nullptr if the platform
    // crypto provider is unavailable.
    static std::unique_ptr<Hasher> Create(HashType type);

    virtual void Reset() = 0;
    virtual void Update(const std::uint8_t* data, std::size_t len) = 0;
    // Finalizes into out and leaves the hasher ready for the next file.
    virtual bool Final(Digest& out) = 0;
};

// One-shot helper used by tests.
bool HashBuffer(HashType type, const void* data, std::size_t len, Digest& out);

// Hex text of a digest. A CRC-32 digest is written in the requested byte order; every
// other algorithm has a single canonical form and ignores `order`.
std::string FormatDigest(const Digest& d, Crc32ByteOrder order);

enum class DigestMatch { Mismatch, Exact, ByteReversed };

// Compares a calculated digest with an expected value read from a checksum file.
// allowByteReversal (CRC-32 only) also accepts the expected bytes in reverse order.
DigestMatch CompareDigest(const Digest& calculated, const std::vector<std::uint8_t>& expected, bool allowByteReversal);

// Convert.ToHexString (upper case).
std::string ToHex(const std::uint8_t* data, std::size_t len);
inline std::string ToHex(const Digest& d) { return ToHex(d.data(), d.size); }
inline std::string ToHex(const std::vector<std::uint8_t>& v) { return ToHex(v.data(), v.size()); }

// Convert.FromHexString: fails on odd length or non-hex characters.
bool TryFromHex(std::string_view hex, std::vector<std::uint8_t>& out);

} // namespace sfv
