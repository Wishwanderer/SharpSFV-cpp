// SharpSFV (C++ port) - per-file data store (Structure of Arrays).
// Ported from Models/FileStore.cs.
//
// Layout: the C# version keeps parallel arrays and doubles them with Array.Resize. That is
// safe in .NET because a reader still holding the old array keeps it alive. In C++ a
// reallocation under a concurrent reader is a crash, so the parallel arrays are split into
// fixed-size chunks that are never moved: index i lives in chunk i / kChunkSize. Each chunk
// is itself a structure of arrays, which keeps the cache-locality benefit.
//
// Threading contract (same as the original):
//   * Add / AddSummary may run on the producer thread while readers run elsewhere; a slot is
//     fully written before Count() makes it visible.
//   * SetResult is called by exactly one worker per index. It writes the digest and time,
//     then publishes the status with release semantics; readers that see a final status
//     (OK/Bad/Error) also see the digest.
//   * Clear, RemoveAt and the Set* mutators for names must only run on the owning (UI)
//     thread while no hashing is in progress.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
// Modified 2026-10-03: native mutex (Threading.h); CRC match per entry.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#pragma once

#include "Hasher.h"
#include "StringPool.h"
#include "Types.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include "Threading.h"
#include <string>
#include <string_view>
#include <vector>

namespace sfv {

class FileStore {
public:
    static constexpr int kChunkBits = 14;
    static constexpr int kChunkSize = 1 << kChunkBits; // 16384 items per chunk
    static constexpr int kMaxChunks = 8192;            // ~134 million items

    FileStore();
    ~FileStore();
    FileStore(const FileStore&) = delete;
    FileStore& operator=(const FileStore&) = delete;

    int Count() const { return count_.load(std::memory_order_acquire); }

    // Thread-safe append. Strings are interned in the store's pool. Returns the index.
    int Add(std::string_view fileName, std::string_view relativePath, std::string_view baseDir, int originalIndex,
            ItemStatus status = ItemStatus::Queued, const std::vector<std::uint8_t>* expectedHash = nullptr);

    // Virtual "summary" row (bold/blue, ignored by hashing and saving).
    void AddSummary(std::string_view name, std::int64_t timeMs);

    void Clear();
    void RemoveAt(int index);
    // Removes several items in one O(N) pass (the C# version called RemoveAt per item,
    // which is O(N) each). `sortedIndices` must be ascending; duplicates are ignored.
    void RemoveMany(const std::vector<int>& sortedIndices);

    // --- Accessors
    const std::string& FileName(int i) const { return *Slot(i).c->fileNames[Slot(i).o]; }
    const std::string& RelativePath(int i) const { return *Slot(i).c->relativePaths[Slot(i).o]; }
    const std::string& BaseDirectory(int i) const { return *Slot(i).c->baseDirs[Slot(i).o]; }
    int OriginalIndex(int i) const { return Slot(i).c->originalIndices[Slot(i).o]; }
    bool IsSummaryRow(int i) const { return Slot(i).c->isSummary[Slot(i).o]; }

    ItemStatus Status(int i) const {
        auto s = Slot(i);
        return static_cast<ItemStatus>(s.c->statuses[s.o].load(std::memory_order_acquire));
    }
    void SetStatus(int i, ItemStatus st) {
        auto s = Slot(i);
        s.c->statuses[s.o].store(static_cast<std::uint8_t>(st), std::memory_order_release);
    }

    // Empty when no hash has been calculated.
    const Digest& Calculated(int i) const { return Slot(i).c->calculated[Slot(i).o]; }
    // Empty vector when there is no expected hash (creation mode).
    const std::vector<std::uint8_t>& Expected(int i) const { return Slot(i).c->expected[Slot(i).o]; }
    void SetExpected(int i, std::vector<std::uint8_t> bytes) { Slot(i).c->expected[Slot(i).o] = std::move(bytes); }

    // Elapsed hashing time in ms; -1 when not processed.
    std::int64_t TimeMs(int i) const { return Slot(i).c->timeMs[Slot(i).o]; }
    std::string TimeStr(int i) const; // "123 ms" or ""

    void SetFileName(int i, std::string_view name);
    void SetRelativePath(int i, std::string_view rel);

    // Path.Combine(BaseDirectory, RelativePath); "" for summary rows.
    std::string GetFullPath(int i) const;

    // Hex of the calculated hash, "Pending" while queued/pending, otherwise "". CRC-32
    // values are formatted in `order`; the overload without it uses the display order.
    std::string GetCalculatedHashString(int i) const { return GetCalculatedHashString(i, DisplayCrcOrder()); }
    std::string GetCalculatedHashString(int i, Crc32ByteOrder order) const;
    std::string GetExpectedHashString(int i) const;

    // Byte order used to display CRC-32 values (the GUI sets it from the settings).
    void SetDisplayCrcOrder(Crc32ByteOrder o) { displayOrder_.store(o, std::memory_order_relaxed); }
    Crc32ByteOrder DisplayCrcOrder() const { return displayOrder_.load(std::memory_order_relaxed); }

    // Byte order a verified CRC-32 entry matched in (None if not CRC-32 or not matched).
    CrcMatch CrcMatchOf(int i) const { return static_cast<CrcMatch>(Slot(i).c->crcMatch[Slot(i).o]); }
    void SetCrcMatch(int i, CrcMatch m) { Slot(i).c->crcMatch[Slot(i).o] = static_cast<std::uint8_t>(m); } // UI thread, idle

    // Worker-side result publication (lock-free: one writer per index).
    void SetResult(int i, const Digest* hash, std::int64_t timeMs, ItemStatus status, CrcMatch match = CrcMatch::None);

private:
    struct Chunk {
        const std::string* fileNames[kChunkSize];
        const std::string* relativePaths[kChunkSize];
        const std::string* baseDirs[kChunkSize];
        std::atomic<std::uint8_t> statuses[kChunkSize];
        std::int32_t originalIndices[kChunkSize];
        std::vector<std::uint8_t> expected[kChunkSize];
        Digest calculated[kChunkSize];
        std::int64_t timeMs[kChunkSize];
        bool isSummary[kChunkSize];
        std::uint8_t crcMatch[kChunkSize];
    };
    struct SlotRef {
        Chunk* c;
        int o;
    };

    SlotRef Slot(int i) const { return {chunks_[i >> kChunkBits].get(), i & (kChunkSize - 1)}; }
    int ReserveSlot(); // under lock_
    void CopySlot(int from, int to);

    Mutex lock_;
    std::atomic<int> count_{0};
    std::atomic<Crc32ByteOrder> displayOrder_{Crc32ByteOrder::Standard};
    std::unique_ptr<std::unique_ptr<Chunk>[]> chunksStorage_;
    std::unique_ptr<Chunk>* chunks_;
    StringPool pool_;
};

} // namespace sfv
