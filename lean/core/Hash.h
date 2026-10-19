// SharpSFV (lean C++ rewrite) - hashing: CRC-32, XXH3-128, MD5, SHA-1, SHA-256.
// Port of src/core/Hasher.*: same digests, same formatting, same CRC-32 byte-order rules.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: the hashing entry points stay out of line (speed under LTO);
//                     CRC-32 uses PCLMULQDQ folding when available.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
//
// CRC-32 folds with PCLMULQDQ where the CPU has it (every x86-64 CPU since about 2010) and
// falls back to slice-by-16 with tables built on first use; XXH3-128 is the vendored xxHash 0.8.3;
// MD5/SHA come from Windows CNG (bcrypt.dll). The Linux test build uses OpenSSL for those.
// A CRC-32 digest is always held in Standard order (CBF43926 for "123456789"); the byte
// order is applied only when formatting (FormatDigest) or comparing (CompareDigest).
#pragma once

#include "../base/Str.h"
#include "../base/Vec.h"
#include "Types.h"

namespace sfv {

struct Digest {
    u8 bytes[32] = {};
    u8 size = 0;
    const u8* Data() const { return bytes; }
    bool Empty() const { return size == 0; }
};

class Hasher {
public:
    Hasher() = default;
    ~Hasher();
    Hasher(const Hasher&) = delete;
    Hasher& operator=(const Hasher&) = delete;

    // Prepares the hasher for `type`. False if the system crypto provider is unavailable
    // (files are then reported as ERROR, as the C# version did with a null algorithm).
    // Out of line even under link-time optimization: the hashing loops are compiled for speed
    // (core/Hash.cpp), and inlined into the size-optimized engine they would lose it.
    __attribute__((noinline)) bool Init(HashType type);
    __attribute__((noinline)) void Reset();
    __attribute__((noinline)) void Update(const u8* data, usize len);
    __attribute__((noinline)) bool Final(Digest& out); // leaves the hasher ready for the next file

private:
    void Release();
    HashType type_ = HashType::XXHASH3;
    bool ready_ = false;
    void* xxh_ = nullptr; // XXH3_state_t*
    u32 crc_ = 0xFFFFFFFFu;
#ifdef _WIN32
    void* alg_ = nullptr; // BCRYPT_ALG_HANDLE
    void* h_ = nullptr;   // BCRYPT_HASH_HANDLE
    bool reusable_ = false, dirty_ = false;
#else
    void* ctx_ = nullptr; // EVP_MD_CTX*
    const void* md_ = nullptr;
#endif
};

Str FormatDigest(const Digest& d, Crc32ByteOrder order);

namespace detail { // test hooks: both CRC-32 paths, on the internal (non-inverted) register
u32 CrcTableForTest(u32 crc, const u8* p, usize len);
u32 CrcFastForTest(u32 crc, const u8* p, usize len);
bool CrcHasClmulForTest();
} // namespace detail

enum class DigestMatch { Mismatch, Exact, ByteReversed };
// allowByteReversal: CRC-32, where both byte orders are accepted.
DigestMatch CompareDigest(const Digest& calculated, const Vec<u8>& expected, bool allowByteReversal);

Str ToHex(const u8* data, usize len);
inline Str ToHex(const Digest& d) { return ToHex(d.Data(), d.size); }
inline Str ToHex(const Vec<u8>& v) { return ToHex(v.Data(), v.Size()); }
bool TryFromHex(StrView hex, Vec<u8>& out); // Convert.FromHexString

} // namespace sfv
