// SharpSFV (C++ port) - per-file data store (Structure of Arrays).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-02: ported from C# (.NET 10) to C++20.
// Modified 2026-10-03: own exception type for a full store (smaller executables).
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "FileStore.h"

#include "PathUtil.h"

#include <exception>

namespace sfv {

namespace {
// Thrown when the store is full (the engine stops adding files). A plain std::exception: the
// std::length_error used before links libstdc++'s whole COW-string exception machinery.
struct StoreCapacityError final : std::exception {
    const char* what() const noexcept override { return "FileStore capacity exceeded"; }
};
} // namespace

FileStore::FileStore()
    : chunksStorage_(std::make_unique<std::unique_ptr<Chunk>[]>(kMaxChunks)), chunks_(chunksStorage_.get()) {}

FileStore::~FileStore() = default;

int FileStore::ReserveSlot() {
    const int index = count_.load(std::memory_order_relaxed);
    const int chunk = index >> kChunkBits;
    if (chunk >= kMaxChunks) throw StoreCapacityError();
    if (!chunks_[chunk]) chunks_[chunk] = std::make_unique<Chunk>();
    return index;
}

int FileStore::Add(std::string_view fileName, std::string_view relativePath, std::string_view baseDir,
                   int originalIndex, ItemStatus status, const std::vector<std::uint8_t>* expectedHash) {
    std::lock_guard lock(lock_);
    const int index = ReserveSlot();
    auto s = Slot(index);
    s.c->fileNames[s.o] = pool_.GetOrAdd(fileName);
    s.c->relativePaths[s.o] = pool_.GetOrAdd(relativePath);
    s.c->baseDirs[s.o] = pool_.GetOrAdd(baseDir);
    s.c->originalIndices[s.o] = originalIndex;
    s.c->statuses[s.o].store(static_cast<std::uint8_t>(status), std::memory_order_relaxed);
    if (expectedHash)
        s.c->expected[s.o] = *expectedHash;
    else
        s.c->expected[s.o].clear();
    s.c->calculated[s.o] = Digest{};
    s.c->timeMs[s.o] = -1;
    s.c->isSummary[s.o] = false;
    s.c->crcMatch[s.o] = 0;
    count_.store(index + 1, std::memory_order_release); // publish
    return index;
}

void FileStore::AddSummary(std::string_view name, std::int64_t timeMs) {
    std::lock_guard lock(lock_);
    const int index = ReserveSlot();
    auto s = Slot(index);
    s.c->fileNames[s.o] = pool_.GetOrAdd(name);
    s.c->relativePaths[s.o] = &StringPool::Empty();
    s.c->baseDirs[s.o] = &StringPool::Empty();
    s.c->originalIndices[s.o] = 0x7FFFFFFF; // int.MaxValue: always sorts last
    s.c->statuses[s.o].store(static_cast<std::uint8_t>(ItemStatus::OK), std::memory_order_relaxed);
    s.c->expected[s.o].clear();
    s.c->calculated[s.o] = Digest{};
    s.c->timeMs[s.o] = timeMs;
    s.c->isSummary[s.o] = true;
    s.c->crcMatch[s.o] = 0;
    count_.store(index + 1, std::memory_order_release);
}

void FileStore::Clear() {
    std::lock_guard lock(lock_);
    count_.store(0, std::memory_order_release);
    // Release memory (the C# version clears references so the GC can reclaim them).
    for (int i = 0; i < kMaxChunks && chunks_[i]; ++i) chunks_[i].reset();
    pool_.Clear();
}

void FileStore::CopySlot(int from, int to) {
    auto a = Slot(from), b = Slot(to);
    b.c->fileNames[b.o] = a.c->fileNames[a.o];
    b.c->relativePaths[b.o] = a.c->relativePaths[a.o];
    b.c->baseDirs[b.o] = a.c->baseDirs[a.o];
    b.c->statuses[b.o].store(a.c->statuses[a.o].load(std::memory_order_relaxed), std::memory_order_relaxed);
    b.c->originalIndices[b.o] = a.c->originalIndices[a.o];
    b.c->expected[b.o] = std::move(a.c->expected[a.o]);
    b.c->calculated[b.o] = a.c->calculated[a.o];
    b.c->timeMs[b.o] = a.c->timeMs[a.o];
    b.c->isSummary[b.o] = a.c->isSummary[a.o];
    b.c->crcMatch[b.o] = a.c->crcMatch[a.o];
}

void FileStore::RemoveAt(int index) {
    std::lock_guard lock(lock_);
    const int count = count_.load(std::memory_order_relaxed);
    if (index < 0 || index >= count) return;
    for (int i = index; i < count - 1; ++i) CopySlot(i + 1, i);
    auto last = Slot(count - 1);
    last.c->expected[last.o].clear();
    last.c->expected[last.o].shrink_to_fit();
    count_.store(count - 1, std::memory_order_release);
}

void FileStore::RemoveMany(const std::vector<int>& sortedIndices) {
    std::lock_guard lock(lock_);
    const int count = count_.load(std::memory_order_relaxed);
    std::size_t k = 0;
    int write = 0;
    for (int read = 0; read < count; ++read) {
        while (k < sortedIndices.size() && sortedIndices[k] < read) ++k;
        if (k < sortedIndices.size() && sortedIndices[k] == read) continue; // removed
        if (write != read) CopySlot(read, write);
        ++write;
    }
    for (int i = write; i < count; ++i) {
        auto s = Slot(i);
        s.c->expected[s.o].clear();
        s.c->expected[s.o].shrink_to_fit();
    }
    count_.store(write, std::memory_order_release);
}

std::string FileStore::TimeStr(int i) const {
    const std::int64_t ms = TimeMs(i);
    return ms < 0 ? std::string() : std::to_string(ms) + " ms";
}

void FileStore::SetFileName(int i, std::string_view name) { Slot(i).c->fileNames[Slot(i).o] = pool_.GetOrAdd(name); }

void FileStore::SetRelativePath(int i, std::string_view rel) {
    Slot(i).c->relativePaths[Slot(i).o] = pool_.GetOrAdd(rel);
}

std::string FileStore::GetFullPath(int i) const {
    if (IsSummaryRow(i)) return {};
    return path::Combine(BaseDirectory(i), RelativePath(i));
}

std::string FileStore::GetCalculatedHashString(int i, Crc32ByteOrder order) const {
    const ItemStatus st = Status(i); // acquire: digest is visible if status is final
    const Digest& d = Calculated(i);
    if (!d.empty() && st != ItemStatus::Queued && st != ItemStatus::Pending) return FormatDigest(d, order);
    return (st == ItemStatus::Queued || st == ItemStatus::Pending) ? "Pending" : "";
}

std::string FileStore::GetExpectedHashString(int i) const {
    const auto& e = Expected(i);
    return e.empty() ? std::string() : ToHex(e);
}

void FileStore::SetResult(int i, const Digest* hash, std::int64_t timeMs, ItemStatus status, CrcMatch match) {
    auto s = Slot(i);
    s.c->calculated[s.o] = hash ? *hash : Digest{};
    s.c->timeMs[s.o] = timeMs;
    s.c->crcMatch[s.o] = static_cast<std::uint8_t>(match);
    s.c->statuses[s.o].store(static_cast<std::uint8_t>(status), std::memory_order_release);
}

} // namespace sfv
