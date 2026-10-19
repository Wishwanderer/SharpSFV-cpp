// SharpSFV (lean C++ rewrite) - the list of files being hashed or verified.
// Port of src/core/FileStore.*. (Its "summary row" was never used by the program, so it is
// left out here.)
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: what the GUI needs (display byte order, renaming, removal, setters).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
//
// Items live in fixed-size chunks that are never moved, so an item's address stays valid
// while more are added (the multi-threaded engine relies on that, as the
// standard build does). Base folders are shared: most runs use exactly one.
#pragma once

#include "../base/Str.h"
#include "../base/Vec.h"
#include "Hash.h"
#include "Types.h"

namespace sfv {

class FileStore {
public:
    static constexpr int kChunkBits = 14;
    static constexpr int kChunkSize = 1 << kChunkBits; // 16384 items per chunk
    static constexpr int kMaxChunks = 8192;            // ~134 million items
    static constexpr int kMaxBaseDirs = 65536;

    FileStore();
    ~FileStore();
    FileStore(const FileStore&) = delete;
    FileStore& operator=(const FileStore&) = delete;

    // Items are added by one thread (the engine's producer) while others read: an item is
    // complete before Count() includes it. Clear, RemoveMany and the setters run only while
    // nothing is hashing.
    int Count() const { return __atomic_load_n(&count_, __ATOMIC_ACQUIRE); }
    // Appends an item; returns its index, or -1 when the store is full.
    int Add(StrView fileName, StrView relativePath, StrView baseDir, int originalIndex,
            ItemStatus status = ItemStatus::Queued, const Vec<u8>* expectedHash = nullptr);
    void Clear();

    const Str& FileName(int i) const { return At(i).fileName; }
    const Str& RelativePath(int i) const { return At(i).relativePath; }
    const Str& BaseDirectory(int i) const { return *baseDirs_[At(i).baseDir]; }
    int OriginalIndex(int i) const { return At(i).originalIndex; }
    ItemStatus Status(int i) const { return static_cast<ItemStatus>(__atomic_load_n(&At(i).status, __ATOMIC_ACQUIRE)); }
    void SetStatus(int i, ItemStatus st) { __atomic_store_n(&At(i).status, static_cast<u8>(st), __ATOMIC_RELEASE); }
    const Digest& Calculated(int i) const { return At(i).calculated; }
    const Vec<u8>& Expected(int i) const { return At(i).expected; }
    void SetExpected(int i, const Vec<u8>& bytes) { At(i).expected = bytes; }
    i64 TimeMs(int i) const { return At(i).timeMs; }
    Str TimeStr(int i) const; // "123 ms", or "" when not processed
    CrcMatch CrcMatchOf(int i) const { return static_cast<CrcMatch>(At(i).crcMatch); }
    void SetCrcMatch(int i, CrcMatch m) { At(i).crcMatch = static_cast<u8>(m); }
    void SetFileName(int i, StrView name) { At(i).fileName = name; }
    void SetRelativePath(int i, StrView rel) { At(i).relativePath = rel; }

    // Removes several items in one pass; `sortedIndices` ascending (duplicates ignored).
    void RemoveMany(const Vec<int>& sortedIndices);

    // Byte order used to display CRC-32 values (the GUI sets it from the settings).
    void SetDisplayCrcOrder(Crc32ByteOrder o) { __atomic_store_n(&displayOrder_, static_cast<int>(o), __ATOMIC_RELAXED); }
    Crc32ByteOrder DisplayCrcOrder() const {
        return static_cast<Crc32ByteOrder>(__atomic_load_n(&displayOrder_, __ATOMIC_RELAXED));
    }

    // Path.Combine(BaseDirectory, RelativePath).
    Str GetFullPath(int i) const;
    // Hex of the calculated hash in `order`; "Pending" while queued/pending, else "".
    Str GetCalculatedHashString(int i, Crc32ByteOrder order) const;
    Str GetCalculatedHashString(int i) const { return GetCalculatedHashString(i, DisplayCrcOrder()); }
    Str GetExpectedHashString(int i) const; // hex, or ""

    // Publishes a result: digest and time first, then the status (release).
    void SetResult(int i, const Digest* hash, i64 timeMs, ItemStatus status, CrcMatch match = CrcMatch::None);

private:
    struct Item {
        Str fileName;
        Str relativePath;
        u32 baseDir = 0;
        i32 originalIndex = 0;
        u8 status = 0;
        u8 crcMatch = 0;
        i64 timeMs = -1;
        Vec<u8> expected;
        Digest calculated;
    };
    Item& At(int i) const { return chunks_[i >> kChunkBits][i & (kChunkSize - 1)]; }
    u32 BaseDirIndex(StrView dir);

    Item** chunks_;     // kMaxChunks pointers, chunks allocated on demand
    Str** baseDirs_;    // kMaxBaseDirs pointers
    int baseDirCount_ = 0;
    int count_ = 0;
    int displayOrder_ = 0;
};

} // namespace sfv
