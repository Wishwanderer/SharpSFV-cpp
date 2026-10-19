// SharpSFV (lean C++ rewrite) - the list of files being hashed or verified (see Store.h).
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the rewrite without the C++ standard library.
// Modified 2026-10-04: RemoveMany, TimeStr, GetExpectedHashString.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
#include "Store.h"

#include "../base/Path.h"

namespace sfv {

FileStore::FileStore() {
    chunks_ = static_cast<Item**>(MemAlloc(sizeof(Item*) * kMaxChunks));
    memset(chunks_, 0, sizeof(Item*) * kMaxChunks);
    baseDirs_ = static_cast<Str**>(MemAlloc(sizeof(Str*) * kMaxBaseDirs));
    memset(baseDirs_, 0, sizeof(Str*) * kMaxBaseDirs);
}

FileStore::~FileStore() {
    Clear();
    MemFree(chunks_);
    MemFree(baseDirs_);
}

void FileStore::Clear() {
    for (int c = 0; c < kMaxChunks && chunks_[c]; ++c) {
        const int used = Min(kChunkSize, count_ - (c << kChunkBits));
        for (int k = 0; k < used; ++k) chunks_[c][k].~Item();
        MemFree(chunks_[c]);
        chunks_[c] = nullptr;
    }
    for (int b = 0; b < baseDirCount_; ++b) {
        baseDirs_[b]->~Str();
        MemFree(baseDirs_[b]);
        baseDirs_[b] = nullptr;
    }
    baseDirCount_ = 0;
    count_ = 0;
}

u32 FileStore::BaseDirIndex(StrView dir) {
    for (int b = baseDirCount_; b-- > 0;) // newest first: runs add many items with one base
        if (*baseDirs_[b] == dir) return static_cast<u32>(b);
    if (baseDirCount_ == kMaxBaseDirs) return 0; // practically unreachable; keeps the first base
    Str* s = new (MemAlloc(sizeof(Str))) Str(dir);
    baseDirs_[baseDirCount_] = s;
    return static_cast<u32>(baseDirCount_++);
}

int FileStore::Add(StrView fileName, StrView relativePath, StrView baseDir, int originalIndex, ItemStatus status,
                   const Vec<u8>* expectedHash) {
    const int index = count_;
    const int chunk = index >> kChunkBits;
    if (chunk >= kMaxChunks) return -1;
    if (!chunks_[chunk]) chunks_[chunk] = static_cast<Item*>(MemAlloc(sizeof(Item) * kChunkSize));
    Item* it = new (&chunks_[chunk][index & (kChunkSize - 1)]) Item();
    it->fileName = fileName;
    it->relativePath = relativePath;
    it->baseDir = BaseDirIndex(baseDir);
    it->originalIndex = originalIndex;
    it->status = static_cast<u8>(status);
    if (expectedHash) it->expected = *expectedHash;
    __atomic_store_n(&count_, index + 1, __ATOMIC_RELEASE); // publish
    return index;
}

Str FileStore::GetFullPath(int i) const { return path::Combine(BaseDirectory(i), RelativePath(i)); }

Str FileStore::GetCalculatedHashString(int i, Crc32ByteOrder order) const {
    const ItemStatus st = Status(i); // acquire: the digest is visible once the status is final
    const Digest& d = Calculated(i);
    if (!d.Empty() && st != ItemStatus::Queued && st != ItemStatus::Pending) return FormatDigest(d, order);
    return (st == ItemStatus::Queued || st == ItemStatus::Pending) ? Str("Pending") : Str();
}

void FileStore::SetResult(int i, const Digest* hash, i64 timeMs, ItemStatus status, CrcMatch match) {
    Item& it = At(i);
    it.calculated = hash ? *hash : Digest{};
    it.timeMs = timeMs;
    it.crcMatch = static_cast<u8>(match);
    __atomic_store_n(&it.status, static_cast<u8>(status), __ATOMIC_RELEASE);
}

Str FileStore::TimeStr(int i) const {
    const i64 ms = TimeMs(i);
    return ms < 0 ? Str() : ToStr(ms) + " ms";
}

Str FileStore::GetExpectedHashString(int i) const {
    const Vec<u8>& e = Expected(i);
    return e.Empty() ? Str() : ToHex(e);
}

void FileStore::RemoveMany(const Vec<int>& sortedIndices) {
    const int count = count_;
    usize k = 0;
    int write = 0;
    for (int read = 0; read < count; ++read) {
        while (k < sortedIndices.Size() && sortedIndices[k] < read) ++k;
        if (k < sortedIndices.Size() && sortedIndices[k] == read) continue; // removed
        if (write != read) At(write) = Move(At(read));
        ++write;
    }
    for (int i = write; i < count; ++i) At(i).~Item(); // slots past the end are unconstructed
    __atomic_store_n(&count_, write, __ATOMIC_RELEASE);
}

} // namespace sfv
