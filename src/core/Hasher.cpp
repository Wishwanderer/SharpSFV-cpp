// SharpSFV (C++ port) - hashing algorithms.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
// Modified 2026-10-03: CRC tables built at run time (smaller executable).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Hasher.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <iterator>

#define XXH_INLINE_ALL
#define XXH_STATIC_LINKING_ONLY
#include "xxhash.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

namespace sfv {

static_assert(std::endian::native == std::endian::little, "CRC-32 slicing tables assume a little-endian host");

namespace {

// ---------------------------------------------------------------- xxHash3-128

class Xxh3Hasher final : public Hasher {
public:
    Xxh3Hasher() : state_(XXH3_createState()) { Reset(); }
    ~Xxh3Hasher() override { XXH3_freeState(state_); }
    Xxh3Hasher(const Xxh3Hasher&) = delete;
    Xxh3Hasher& operator=(const Xxh3Hasher&) = delete;

    void Reset() override { XXH3_128bits_reset(state_); }
    void Update(const std::uint8_t* data, std::size_t len) override { XXH3_128bits_update(state_, data, len); }
    bool Final(Digest& out) override {
        XXH128_canonical_t canon;
        XXH128_canonicalFromHash(&canon, XXH3_128bits_digest(state_));
        std::memcpy(out.bytes.data(), canon.digest, 16);
        out.size = 16;
        Reset();
        return true;
    }

private:
    XXH3_state_t* state_;
};

// ---------------------------------------------------------------- CRC-32 (IEEE 802.3)

struct Crc32Tables {
    std::uint32_t t[16][256];
};

// Built once at run time on the heap. As a static object the table would be stored in the
// executable as 16 KB of initialised data (MinGW does not put such objects in .bss with
// -fdata-sections, and an optimiser may also fold the loops into constants).
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void BuildCrcTables(Crc32Tables& tb) {
    auto& t = tb.t;
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
        t[0][i] = c;
    }
    for (std::uint32_t i = 0; i < 256; ++i)
        for (int s = 1; s < 16; ++s) t[s][i] = (t[s - 1][i] >> 8) ^ t[0][t[s - 1][i] & 0xFF];
}

const Crc32Tables& CrcTables() {
    static const Crc32Tables* const tables = [] { // thread-safe one-time init; never freed
        auto* t = new Crc32Tables;
        BuildCrcTables(*t);
        return t;
    }();
    return *tables;
}

class Crc32Hasher final : public Hasher {
public:
    Crc32Hasher() : t_(CrcTables().t) {}

    void Reset() override { crc_ = 0xFFFFFFFFu; }

    void Update(const std::uint8_t* p, std::size_t len) override {
        std::uint32_t crc = crc_;
        while (len >= 16) {
            std::uint32_t a, b, c, d;
            std::memcpy(&a, p, 4);
            std::memcpy(&b, p + 4, 4);
            std::memcpy(&c, p + 8, 4);
            std::memcpy(&d, p + 12, 4);
            a ^= crc;
            crc = t_[15][a & 0xFF] ^ t_[14][(a >> 8) & 0xFF] ^ t_[13][(a >> 16) & 0xFF] ^ t_[12][a >> 24] ^
                  t_[11][b & 0xFF] ^ t_[10][(b >> 8) & 0xFF] ^ t_[9][(b >> 16) & 0xFF] ^ t_[8][b >> 24] ^
                  t_[7][c & 0xFF] ^ t_[6][(c >> 8) & 0xFF] ^ t_[5][(c >> 16) & 0xFF] ^ t_[4][c >> 24] ^
                  t_[3][d & 0xFF] ^ t_[2][(d >> 8) & 0xFF] ^ t_[1][(d >> 16) & 0xFF] ^ t_[0][d >> 24];
            p += 16;
            len -= 16;
        }
        while (len--) crc = (crc >> 8) ^ t_[0][(crc ^ *p++) & 0xFF];
        crc_ = crc;
    }

    bool Final(Digest& out) override {
        const std::uint32_t v = ~crc_; // Standard order: most-significant byte first
        out.bytes = {static_cast<std::uint8_t>(v >> 24), static_cast<std::uint8_t>(v >> 16),
                     static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v)};
        out.size = 4;
        Reset();
        return true;
    }

private:
    const std::uint32_t (*t_)[256];
    std::uint32_t crc_ = 0xFFFFFFFFu;
};

// ---------------------------------------------------------------- MD5 / SHA-1 / SHA-256

#ifdef _WIN32

// One CNG provider per algorithm, shared by all threads (providers are thread-safe;
// hash objects are not, so every Hasher owns its own).
struct CngProvider {
    BCRYPT_ALG_HANDLE alg = nullptr;
    bool reusable = false;

    explicit CngProvider(LPCWSTR algId) {
        if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, algId, nullptr, BCRYPT_HASH_REUSABLE_FLAG))) {
            reusable = true;
        } else if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, algId, nullptr, 0))) {
            alg = nullptr;
        }
    }
    ~CngProvider() {
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    }
};

CngProvider* ProviderFor(HashType t) {
    static CngProvider md5(BCRYPT_MD5_ALGORITHM);
    static CngProvider sha1(BCRYPT_SHA1_ALGORITHM);
    static CngProvider sha256(BCRYPT_SHA256_ALGORITHM);
    switch (t) {
        case HashType::MD5: return md5.alg ? &md5 : nullptr;
        case HashType::SHA1: return sha1.alg ? &sha1 : nullptr;
        case HashType::SHA256: return sha256.alg ? &sha256 : nullptr;
        default: return nullptr;
    }
}

class CngHasher final : public Hasher {
public:
    CngHasher(CngProvider* p, int digestSize) : p_(p), size_(digestSize) { Create(); }
    ~CngHasher() override {
        if (h_) BCryptDestroyHash(h_);
    }
    CngHasher(const CngHasher&) = delete;
    CngHasher& operator=(const CngHasher&) = delete;

    bool Valid() const { return h_ != nullptr; }

    void Reset() override {
        if (!dirty_) return;
        if (p_->reusable) {
            std::uint8_t scratch[32];
            BCryptFinishHash(h_, scratch, static_cast<ULONG>(size_), 0); // discards state
        } else {
            BCryptDestroyHash(h_);
            h_ = nullptr;
            Create();
        }
        dirty_ = false;
    }

    void Update(const std::uint8_t* data, std::size_t len) override {
        dirty_ = true;
        while (len > 0) {
            const ULONG chunk = static_cast<ULONG>(len > (1u << 30) ? (1u << 30) : len);
            BCryptHashData(h_, const_cast<PUCHAR>(data), chunk, 0);
            data += chunk;
            len -= chunk;
        }
    }

    bool Final(Digest& out) override {
        const bool ok = BCRYPT_SUCCESS(BCryptFinishHash(h_, out.bytes.data(), static_cast<ULONG>(size_), 0));
        out.size = ok ? static_cast<std::uint8_t>(size_) : 0;
        if (!p_->reusable) {
            BCryptDestroyHash(h_);
            h_ = nullptr;
            Create();
        }
        dirty_ = false;
        return ok;
    }

private:
    void Create() {
        if (!BCRYPT_SUCCESS(BCryptCreateHash(p_->alg, &h_, nullptr, 0, nullptr, 0,
                                             p_->reusable ? BCRYPT_HASH_REUSABLE_FLAG : 0)))
            h_ = nullptr;
    }

    CngProvider* p_;
    BCRYPT_HASH_HANDLE h_ = nullptr;
    int size_;
    bool dirty_ = false;
};

#else

class EvpHasher final : public Hasher {
public:
    explicit EvpHasher(const EVP_MD* md) : md_(md), ctx_(EVP_MD_CTX_new()) { Reset(); }
    ~EvpHasher() override { EVP_MD_CTX_free(ctx_); }
    EvpHasher(const EvpHasher&) = delete;
    EvpHasher& operator=(const EvpHasher&) = delete;

    void Reset() override { EVP_DigestInit_ex(ctx_, md_, nullptr); }
    void Update(const std::uint8_t* data, std::size_t len) override { EVP_DigestUpdate(ctx_, data, len); }
    bool Final(Digest& out) override {
        unsigned int n = 0;
        const bool ok = EVP_DigestFinal_ex(ctx_, out.bytes.data(), &n) == 1;
        out.size = ok ? static_cast<std::uint8_t>(n) : 0;
        Reset();
        return ok;
    }

private:
    const EVP_MD* md_;
    EVP_MD_CTX* ctx_;
};

#endif

} // namespace

std::unique_ptr<Hasher> Hasher::Create(HashType type) {
    switch (type) {
        case HashType::XXHASH3: return std::make_unique<Xxh3Hasher>();
        case HashType::Crc32: return std::make_unique<Crc32Hasher>();
        case HashType::MD5:
        case HashType::SHA1:
        case HashType::SHA256: {
#ifdef _WIN32
            CngProvider* p = ProviderFor(type);
            if (!p) return nullptr;
            auto h = std::make_unique<CngHasher>(p, DigestSize(type));
            if (!h->Valid()) return nullptr;
            return h;
#else
            const EVP_MD* md = type == HashType::MD5 ? EVP_md5() : type == HashType::SHA1 ? EVP_sha1() : EVP_sha256();
            return std::make_unique<EvpHasher>(md);
#endif
        }
    }
    return nullptr;
}

bool HashBuffer(HashType type, const void* data, std::size_t len, Digest& out) {
    auto h = Hasher::Create(type);
    if (!h) return false;
    h->Update(static_cast<const std::uint8_t*>(data), len);
    return h->Final(out);
}

std::string FormatDigest(const Digest& d, Crc32ByteOrder order) {
    if (d.size == 4 && order == Crc32ByteOrder::Reversed) {
        const std::uint8_t r[4] = {d.bytes[3], d.bytes[2], d.bytes[1], d.bytes[0]};
        return ToHex(r, 4);
    }
    return ToHex(d);
}

DigestMatch CompareDigest(const Digest& calc, const std::vector<std::uint8_t>& expected, bool allowByteReversal) {
    if (calc.empty() || expected.size() != calc.size) return DigestMatch::Mismatch;
    if (std::equal(expected.begin(), expected.end(), calc.data())) return DigestMatch::Exact;
    if (allowByteReversal &&
        std::equal(expected.begin(), expected.end(), std::make_reverse_iterator(calc.data() + calc.size)))
        return DigestMatch::ByteReversed;
    return DigestMatch::Mismatch;
}

std::string ToHex(const std::uint8_t* data, std::size_t len) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string s(len * 2, '\0');
    for (std::size_t i = 0; i < len; ++i) {
        s[2 * i] = kDigits[data[i] >> 4];
        s[2 * i + 1] = kDigits[data[i] & 0xF];
    }
    return s;
}

bool TryFromHex(std::string_view hex, std::vector<std::uint8_t>& out) {
    out.clear();
    if (hex.size() % 2 != 0) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]), lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            out.clear();
            return false;
        }
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return true;
}

} // namespace sfv
