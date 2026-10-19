// SharpSFV (lean C++ rewrite) - hashing (see Hash.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: CRC-32 uses carry-less multiplication (PCLMULQDQ) when the CPU has it.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Hash.h"

#define XXH_INLINE_ALL
#define XXH_STATIC_LINKING_ONLY
#include "xxhash.h"

#if defined(__x86_64__) || defined(__i386__)
#define SFV_CRC_CLMUL 1
#include <cpuid.h>
#include <smmintrin.h>
#include <wmmintrin.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

namespace sfv {

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "CRC-32 slicing tables assume a little-endian host");

namespace {

// ---------------------------------------------------------------- CRC-32 (IEEE 802.3)

struct CrcTables {
    u32 t[16][256];
};
CrcTables* g_crc = nullptr; // built on first use, never freed

void BuildCrcTables(CrcTables& tb) {
    auto& t = tb.t;
    for (u32 i = 0; i < 256; ++i) {
        u32 c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
        t[0][i] = c;
    }
    for (u32 i = 0; i < 256; ++i)
        for (int s = 1; s < 16; ++s) t[s][i] = (t[s - 1][i] >> 8) ^ t[0][t[s - 1][i] & 0xFF];
}

const CrcTables& Crc() {
    CrcTables* t = __atomic_load_n(&g_crc, __ATOMIC_ACQUIRE);
    if (t) return *t;
    CrcTables* fresh = static_cast<CrcTables*>(MemAlloc(sizeof(CrcTables)));
    BuildCrcTables(*fresh);
    CrcTables* expected = nullptr;
    if (__atomic_compare_exchange_n(&g_crc, &expected, fresh, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return *fresh;
    MemFree(fresh); // another thread got there first
    return *expected;
}

u32 CrcUpdate(u32 crc, const u8* p, usize len) {
    const u32(*t)[256] = Crc().t;
    while (len >= 16) {
        u32 a, b, c, d;
        memcpy(&a, p, 4);
        memcpy(&b, p + 4, 4);
        memcpy(&c, p + 8, 4);
        memcpy(&d, p + 12, 4);
        a ^= crc;
        crc = t[15][a & 0xFF] ^ t[14][(a >> 8) & 0xFF] ^ t[13][(a >> 16) & 0xFF] ^ t[12][a >> 24] ^
              t[11][b & 0xFF] ^ t[10][(b >> 8) & 0xFF] ^ t[9][(b >> 16) & 0xFF] ^ t[8][b >> 24] ^
              t[7][c & 0xFF] ^ t[6][(c >> 8) & 0xFF] ^ t[5][(c >> 16) & 0xFF] ^ t[4][c >> 24] ^
              t[3][d & 0xFF] ^ t[2][(d >> 8) & 0xFF] ^ t[1][(d >> 16) & 0xFF] ^ t[0][d >> 24];
        p += 16;
        len -= 16;
    }
    while (len--) crc = (crc >> 8) ^ t[0][(crc ^ *p++) & 0xFF];
    return crc;
}

#ifdef SFV_CRC_CLMUL
// CRC-32 by folding with carry-less multiplication: Gopal et al., "Fast CRC Computation for
// Generic Polynomials Using PCLMULQDQ Instruction" (Intel, 2009), in the form used by zlib
// (crc32_simd.c in Chromium). `crc` is the internal register (not inverted); `len` is a
// multiple of 16 and at least 64. About 3-4x faster than slice-by-16 and independent of how
// the compiler schedules table loads.
__attribute__((target("pclmul,sse4.1"))) u32 CrcFold(u32 crc, const u8* buf, usize len) {
    alignas(16) static const u64 k1k2[] = {0x0154442bd4ull, 0x01c6e41596ull};
    alignas(16) static const u64 k3k4[] = {0x01751997d0ull, 0x00ccaa009eull};
    alignas(16) static const u64 k5k0[] = {0x0163cd6124ull, 0x0000000000ull};
    alignas(16) static const u64 poly[] = {0x01db710641ull, 0x01f7011641ull};

    __m128i x0, x1, x2, x3, x4, x5, x6, x7, x8, y5, y6, y7, y8;
    x1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(buf + 0x00));
    x2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(buf + 0x10));
    x3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(buf + 0x20));
    x4 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(buf + 0x30));
    x1 = _mm_xor_si128(x1, _mm_cvtsi32_si128(static_cast<int>(crc)));
    x0 = _mm_load_si128(reinterpret_cast<const __m128i*>(k1k2));
    buf += 64;
    len -= 64;

    while (len >= 64) { // four 128-bit lanes folded in parallel
        x5 = _mm_clmulepi64_si128(x1, x0, 0x00);
        x6 = _mm_clmulepi64_si128(x2, x0, 0x00);
        x7 = _mm_clmulepi64_si128(x3, x0, 0x00);
        x8 = _mm_clmulepi64_si128(x4, x0, 0x00);
        x1 = _mm_clmulepi64_si128(x1, x0, 0x11);
        x2 = _mm_clmulepi64_si128(x2, x0, 0x11);
        x3 = _mm_clmulepi64_si128(x3, x0, 0x11);
        x4 = _mm_clmulepi64_si128(x4, x0, 0x11);
        y5 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(buf + 0x00));
        y6 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(buf + 0x10));
        y7 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(buf + 0x20));
        y8 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(buf + 0x30));
        x1 = _mm_xor_si128(_mm_xor_si128(x1, x5), y5);
        x2 = _mm_xor_si128(_mm_xor_si128(x2, x6), y6);
        x3 = _mm_xor_si128(_mm_xor_si128(x3, x7), y7);
        x4 = _mm_xor_si128(_mm_xor_si128(x4, x8), y8);
        buf += 64;
        len -= 64;
    }

    // Fold the four lanes into one.
    x0 = _mm_load_si128(reinterpret_cast<const __m128i*>(k3k4));
    const __m128i lanes[] = {x2, x3, x4};
    for (const __m128i& next : lanes) {
        x5 = _mm_clmulepi64_si128(x1, x0, 0x00);
        x1 = _mm_clmulepi64_si128(x1, x0, 0x11);
        x1 = _mm_xor_si128(_mm_xor_si128(x1, next), x5);
    }
    while (len >= 16) { // remaining 16-byte blocks
        x2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(buf));
        x5 = _mm_clmulepi64_si128(x1, x0, 0x00);
        x1 = _mm_clmulepi64_si128(x1, x0, 0x11);
        x1 = _mm_xor_si128(_mm_xor_si128(x1, x2), x5);
        buf += 16;
        len -= 16;
    }

    // 128 -> 64 bits.
    x2 = _mm_clmulepi64_si128(x1, x0, 0x10);
    x3 = _mm_setr_epi32(~0, 0, ~0, 0);
    x1 = _mm_srli_si128(x1, 8);
    x1 = _mm_xor_si128(x1, x2);
    x0 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(k5k0));
    x2 = _mm_srli_si128(x1, 4);
    x1 = _mm_and_si128(x1, x3);
    x1 = _mm_clmulepi64_si128(x1, x0, 0x00);
    x1 = _mm_xor_si128(x1, x2);

    // Barrett reduction to 32 bits.
    x0 = _mm_load_si128(reinterpret_cast<const __m128i*>(poly));
    x2 = _mm_and_si128(x1, x3);
    x2 = _mm_clmulepi64_si128(x2, x0, 0x10);
    x2 = _mm_and_si128(x2, x3);
    x2 = _mm_clmulepi64_si128(x2, x0, 0x00);
    x1 = _mm_xor_si128(x1, x2);
    return static_cast<u32>(_mm_extract_epi32(x1, 1));
}

int g_clmul = 0; // 0 unknown, 1 absent, 2 present

bool HasClmul() {
    int v = __atomic_load_n(&g_clmul, __ATOMIC_RELAXED);
    if (v == 0) {
        unsigned a = 0, b = 0, c = 0, d = 0;
        const bool ok = __get_cpuid(1, &a, &b, &c, &d) && (c & bit_PCLMUL) && (c & bit_SSE4_1);
        v = ok ? 2 : 1;
        __atomic_store_n(&g_clmul, v, __ATOMIC_RELAXED);
    }
    return v == 2;
}
#endif

u32 CrcUpdateFast(u32 crc, const u8* p, usize len) {
#ifdef SFV_CRC_CLMUL
    if (len >= 64 && HasClmul()) {
        const usize chunk = len & ~static_cast<usize>(15);
        crc = CrcFold(crc, p, chunk);
        p += chunk;
        len -= chunk;
    }
#endif
    return CrcUpdate(crc, p, len);
}

#ifdef _WIN32
const wchar_t* CngId(HashType t) {
    switch (t) {
        case HashType::MD5: return BCRYPT_MD5_ALGORITHM;
        case HashType::SHA1: return BCRYPT_SHA1_ALGORITHM;
        default: return BCRYPT_SHA256_ALGORITHM;
    }
}
#endif

} // namespace

namespace detail {
u32 CrcTableForTest(u32 crc, const u8* p, usize len) { return CrcUpdate(crc, p, len); }
u32 CrcFastForTest(u32 crc, const u8* p, usize len) { return CrcUpdateFast(crc, p, len); }
bool CrcHasClmulForTest() {
#ifdef SFV_CRC_CLMUL
    return HasClmul();
#else
    return false;
#endif
}
} // namespace detail

Hasher::~Hasher() { Release(); }

void Hasher::Release() {
    if (xxh_) XXH3_freeState(static_cast<XXH3_state_t*>(xxh_));
    xxh_ = nullptr;
#ifdef _WIN32
    if (h_) BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(h_));
    if (alg_) BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(alg_), 0);
    h_ = alg_ = nullptr;
#else
    if (ctx_) EVP_MD_CTX_free(static_cast<EVP_MD_CTX*>(ctx_));
    ctx_ = nullptr;
#endif
    ready_ = false;
}

bool Hasher::Init(HashType type) {
    Release();
    type_ = type;
    switch (type) {
        case HashType::XXHASH3:
            xxh_ = XXH3_createState();
            if (!xxh_) return false;
            break;
        case HashType::Crc32:
            (void)Crc();
            break;
        default: {
#ifdef _WIN32
            BCRYPT_ALG_HANDLE alg = nullptr;
            if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, CngId(type), nullptr, BCRYPT_HASH_REUSABLE_FLAG)))
                reusable_ = true;
            else if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, CngId(type), nullptr, 0)))
                return false;
            alg_ = alg;
            BCRYPT_HASH_HANDLE h = nullptr;
            if (!BCRYPT_SUCCESS(BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, reusable_ ? BCRYPT_HASH_REUSABLE_FLAG : 0)))
                return false;
            h_ = h;
#else
            md_ = type == HashType::MD5 ? EVP_md5() : type == HashType::SHA1 ? EVP_sha1() : EVP_sha256();
            ctx_ = EVP_MD_CTX_new();
            if (!ctx_) return false;
#endif
            break;
        }
    }
    ready_ = true;
    Reset();
    return true;
}

void Hasher::Reset() {
    if (!ready_) return;
    switch (type_) {
        case HashType::XXHASH3: XXH3_128bits_reset(static_cast<XXH3_state_t*>(xxh_)); break;
        case HashType::Crc32: crc_ = 0xFFFFFFFFu; break;
        default:
#ifdef _WIN32
            if (!dirty_) break;
            if (reusable_) {
                u8 scratch[32];
                BCryptFinishHash(static_cast<BCRYPT_HASH_HANDLE>(h_), scratch, static_cast<ULONG>(DigestSize(type_)), 0);
            } else {
                BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(h_));
                BCRYPT_HASH_HANDLE h = nullptr;
                BCryptCreateHash(static_cast<BCRYPT_ALG_HANDLE>(alg_), &h, nullptr, 0, nullptr, 0, 0);
                h_ = h;
            }
            dirty_ = false;
#else
            EVP_DigestInit_ex(static_cast<EVP_MD_CTX*>(ctx_), static_cast<const EVP_MD*>(md_), nullptr);
#endif
            break;
    }
}

void Hasher::Update(const u8* data, usize len) {
    switch (type_) {
        case HashType::XXHASH3: XXH3_128bits_update(static_cast<XXH3_state_t*>(xxh_), data, len); break;
        case HashType::Crc32: crc_ = CrcUpdateFast(crc_, data, len); break;
        default:
#ifdef _WIN32
            dirty_ = true;
            while (len > 0) {
                const ULONG chunk = static_cast<ULONG>(len > (1u << 30) ? (1u << 30) : len);
                BCryptHashData(static_cast<BCRYPT_HASH_HANDLE>(h_), const_cast<PUCHAR>(data), chunk, 0);
                data += chunk;
                len -= chunk;
            }
#else
            EVP_DigestUpdate(static_cast<EVP_MD_CTX*>(ctx_), data, len);
#endif
            break;
    }
}

bool Hasher::Final(Digest& out) {
    if (!ready_) return false;
    bool ok = true;
    switch (type_) {
        case HashType::XXHASH3: {
            XXH128_canonical_t canon;
            XXH128_canonicalFromHash(&canon, XXH3_128bits_digest(static_cast<XXH3_state_t*>(xxh_)));
            memcpy(out.bytes, canon.digest, 16);
            out.size = 16;
            break;
        }
        case HashType::Crc32: {
            const u32 v = ~crc_; // Standard order: most-significant byte first
            out.bytes[0] = static_cast<u8>(v >> 24);
            out.bytes[1] = static_cast<u8>(v >> 16);
            out.bytes[2] = static_cast<u8>(v >> 8);
            out.bytes[3] = static_cast<u8>(v);
            out.size = 4;
            break;
        }
        default: {
            const int size = DigestSize(type_);
#ifdef _WIN32
            ok = BCRYPT_SUCCESS(BCryptFinishHash(static_cast<BCRYPT_HASH_HANDLE>(h_), out.bytes, static_cast<ULONG>(size), 0));
            if (!reusable_) {
                BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(h_));
                BCRYPT_HASH_HANDLE h = nullptr;
                BCryptCreateHash(static_cast<BCRYPT_ALG_HANDLE>(alg_), &h, nullptr, 0, nullptr, 0, 0);
                h_ = h;
            }
            dirty_ = false;
#else
            unsigned int n = 0;
            ok = EVP_DigestFinal_ex(static_cast<EVP_MD_CTX*>(ctx_), out.bytes, &n) == 1 && static_cast<int>(n) == size;
#endif
            out.size = ok ? static_cast<u8>(size) : 0;
            break;
        }
    }
    Reset();
    return ok;
}

Str FormatDigest(const Digest& d, Crc32ByteOrder order) {
    if (d.size == 4 && order == Crc32ByteOrder::Reversed) {
        const u8 r[4] = {d.bytes[3], d.bytes[2], d.bytes[1], d.bytes[0]};
        return ToHex(r, 4);
    }
    return ToHex(d);
}

DigestMatch CompareDigest(const Digest& calc, const Vec<u8>& expected, bool allowByteReversal) {
    if (calc.Empty() || expected.Size() != calc.size) return DigestMatch::Mismatch;
    if (memcmp(expected.Data(), calc.bytes, calc.size) == 0) return DigestMatch::Exact;
    if (allowByteReversal) {
        bool same = true;
        for (usize i = 0; i < calc.size && same; ++i) same = expected[i] == calc.bytes[calc.size - 1 - i];
        if (same) return DigestMatch::ByteReversed;
    }
    return DigestMatch::Mismatch;
}

Str ToHex(const u8* data, usize len) {
    static const char kDigits[] = "0123456789ABCDEF";
    Str s;
    s.Resize(len * 2);
    for (usize i = 0; i < len; ++i) {
        s[2 * i] = kDigits[data[i] >> 4];
        s[2 * i + 1] = kDigits[data[i] & 0xF];
    }
    return s;
}

bool TryFromHex(StrView hex, Vec<u8>& out) {
    out.Clear();
    if (hex.Size() % 2 != 0) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out.Reserve(hex.Size() / 2);
    for (usize i = 0; i < hex.Size(); i += 2) {
        const int hi = nibble(hex[i]), lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            out.Clear();
            return false;
        }
        out.Push(static_cast<u8>((hi << 4) | lo));
    }
    return true;
}

} // namespace sfv
