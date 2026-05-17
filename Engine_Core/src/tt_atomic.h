#pragma once
#ifndef TT_ATOMIC_H
#define TT_ATOMIC_H

#include "types.h"
#include <atomic>
#include <cstring>
#include <memory>
#include <algorithm>

/* =========================
   LOCK-FREE TRANSPOSITION TABLE
   Uses unique_ptr<T[]> for RAII - avoids std::vector copy-ctor
   issues with atomics on MSVC debug CRT + ASAN
   ========================= */

struct alignas(16) TTEntryAtomic {
    std::atomic<uint64_t> keyData{0};
    std::atomic<uint64_t> scoreMove{0};
    uint8_t generation = 0;
    uint8_t padding[7] = {};

    TTEntryAtomic() = default;

    // Explicitly deleted - atomics are not copyable
    TTEntryAtomic(const TTEntryAtomic&) = delete;
    TTEntryAtomic& operator=(const TTEntryAtomic&) = delete;
    TTEntryAtomic(TTEntryAtomic&&) = delete;
    TTEntryAtomic& operator=(TTEntryAtomic&&) = delete;

    // ===========================================================
    // LOCKLESS XOR-CHECKSUM (Hyatt's trick)
    // -----------------------------------------------------------
    // Without locking, a 16-byte TTEntry written by one thread can be
    // read in a half-updated ("torn") state by another, where one
    // 8-byte word matches a new write and the other still holds the
    // old write. A naive `keyData == key` check would then pass, and
    // we'd return a `Move` payload from a *different* position --
    // which causes illegal moves, mailbox/bitboard corruption, and
    // crashes at high depths under multi-thread Lazy SMP.
    //
    // Fix: store `keyData XOR scoreMove` instead of `keyData`. On
    // read, reconstruct `key = stored_keyData XOR stored_scoreMove`.
    // If a torn read mixes new/old halves, the XOR no longer equals
    // the original `key`, so the entry is correctly rejected.
    //
    // Cost: one extra XOR per probe/store. Same memory layout.
    // ===========================================================

    void pack(uint64_t key, int16_t score, Move move, int8_t depth,
              uint8_t flag, uint8_t gen) {
        // Build payload word (score, move, depth)
        uint64_t packed_score = (static_cast<uint64_t>(static_cast<uint16_t>(score))) |
                                (static_cast<uint64_t>(move.from) << 16) |
                                (static_cast<uint64_t>(move.to) << 24) |
                                (static_cast<uint64_t>(move.flags) << 32) |
                                (static_cast<uint64_t>(move.promotion) << 40) |
                                (static_cast<uint64_t>(static_cast<uint8_t>(depth)) << 48);

        // Build key word (full 48-bit key + flag + gen)
        uint64_t packed_key = (key & 0xFFFFFFFFFFFFULL) |
                              (static_cast<uint64_t>(flag) << 48) |
                              (static_cast<uint64_t>(gen) << 56);

        // XOR the payload into the key word for lockless verification.
        // Write payload first, then the checksummed key word with release
        // semantics so any thread that sees the new key word is also
        // guaranteed to see the new payload.
        uint64_t checksummed_key = packed_key ^ packed_score;

        scoreMove.store(packed_score, std::memory_order_relaxed);
        keyData.store(checksummed_key, std::memory_order_release);
        generation = gen;
    }

    // Atomically read and verify the entry. Returns true if the entry
    // is for `wanted_key` and was not torn during the read; false in
    // every other case (empty slot, different position, or torn read).
    bool readVerified(uint64_t wanted_key, int16_t& score, Move& move,
                      int8_t& depth, uint8_t& flag, uint8_t& gen) const {
        uint64_t k = keyData.load(std::memory_order_acquire);
        uint64_t s = scoreMove.load(std::memory_order_relaxed);

        // Reconstruct the originally-stored key word
        uint64_t reconstructed = k ^ s;

        if ((reconstructed & 0xFFFFFFFFFFFFULL) != (wanted_key & 0xFFFFFFFFFFFFULL))
            return false;

        flag = (reconstructed >> 48) & 0xFF;
        gen  = (reconstructed >> 56) & 0xFF;

        score = static_cast<int16_t>(s & 0xFFFF);
        move.from      = static_cast<Square>((s >> 16) & 0xFF);
        move.to        = static_cast<Square>((s >> 24) & 0xFF);
        move.flags     = static_cast<MoveFlags>((s >> 32) & 0xFF);
        move.promotion = static_cast<Piece>((s >> 40) & 0xFF);
        depth          = static_cast<int8_t>((s >> 48) & 0xFF);

        return true;
    }

    uint8_t getGeneration() const { return generation; }

    // Best-effort depth for replacement policy. Caller is replacing
    // anyway so a torn read here is harmless -- worst case we pick
    // a suboptimal replacement victim.
    int8_t getDepth() const {
        uint64_t s = scoreMove.load(std::memory_order_relaxed);
        return static_cast<int8_t>((s >> 48) & 0xFF);
    }

    // Best-effort key match for replacement policy only.
    // NOT for probing: use readVerified() instead.
    bool keyMatchesApprox(uint64_t key) const {
        uint64_t k = keyData.load(std::memory_order_relaxed);
        uint64_t s = scoreMove.load(std::memory_order_relaxed);
        uint64_t reconstructed = k ^ s;
        return (reconstructed & 0xFFFFFFFFFFFFULL) == (key & 0xFFFFFFFFFFFFULL);
    }
};

class TranspositionTableAtomic {
private:
    // RAII via unique_ptr<T[]> - no copy ctor needed, no vector overhead
    std::unique_ptr<TTEntryAtomic[]> table_;
    size_t size_ = 0;
    uint8_t generation_ = 0;

    // Helper: allocate and zero-init a new table
    static std::unique_ptr<TTEntryAtomic[]> allocTable(size_t count) {
        // new T[n]() value-initializes each element (calls default ctor)
        return std::unique_ptr<TTEntryAtomic[]>(new TTEntryAtomic[count]());
    }

public:
    explicit TranspositionTableAtomic(size_t sizeMB = 512) {
        resize(sizeMB);
    }

    // Non-copyable, non-movable
    TranspositionTableAtomic(const TranspositionTableAtomic&) = delete;
    TranspositionTableAtomic& operator=(const TranspositionTableAtomic&) = delete;
    TranspositionTableAtomic(TranspositionTableAtomic&&) = delete;
    TranspositionTableAtomic& operator=(TranspositionTableAtomic&&) = delete;

    // unique_ptr destructor handles deallocation
    ~TranspositionTableAtomic() = default;

    void clear() {
        for (size_t i = 0; i < size_; i++) {
            table_[i].keyData.store(0, std::memory_order_relaxed);
            table_[i].scoreMove.store(0, std::memory_order_relaxed);
            table_[i].generation = 0;
        }
        generation_ = 0;
    }

    void nextGeneration() {
        generation_++;
        if (generation_ == 0) {
            clear();
            generation_ = 1;
        }
    }

    TTEntryAtomic& getEntry(uint64_t key) {
        return table_[key % size_];
    }

    bool probe(uint64_t key, int16_t& score, Move& move, int8_t& depth,
               uint8_t& flag, int ply) {
        TTEntryAtomic& entry = getEntry(key);

        uint8_t stored_gen;
        // readVerified() rejects torn reads atomically via XOR checksum.
        // After this returns true, score/move/depth/flag are guaranteed
        // to be a consistent snapshot of one single store.
        if (!entry.readVerified(key, score, move, depth, flag, stored_gen))
            return false;

        // Adjust mate scores to be relative to the current ply
        if (score > 28000) score -= ply;
        else if (score < -28000) score += ply;

        return true;
    }

    void store(uint64_t key, int16_t score, Move move, int8_t depth,
               uint8_t flag, int ply) {
        TTEntryAtomic& entry = getEntry(key);

        if (score > 28000) score += ply;
        else if (score < -28000) score -= ply;

        // Replacement policy uses best-effort reads -- a torn read here
        // only affects which entry we choose to overwrite, not correctness.
        uint8_t entryGen = entry.getGeneration();
        int8_t entryDepth = entry.getDepth();

        bool replace = false;
        if (entryGen != generation_)
            replace = true;
        else if (entry.keyMatchesApprox(key))
            replace = (depth + 2 >= entryDepth);
        else
            replace = (depth >= entryDepth);

        if (replace) {
            entry.pack(key, score, move, depth, flag, generation_);
        }
    }

    int hashfull() const {
        int count = 0;
        constexpr int sample = 1000;
        size_t n = std::min(static_cast<size_t>(sample), size_);
        for (size_t i = 0; i < n; i++) {
            if (table_[i].getGeneration() == generation_)
                count++;
        }
        return count;
    }

    void resize(size_t sizeMB) {
        size_ = (sizeMB * 1024 * 1024) / sizeof(TTEntryAtomic);
        if (size_ == 0) size_ = 1;
        // Release old table, allocate new one (RAII - old one freed automatically)
        table_ = allocTable(size_);
        generation_ = 0;
    }

    size_t getSize() const { return size_; }
    size_t getSizeMB() const { return (size_ * sizeof(TTEntryAtomic)) / (1024 * 1024); }
};

// Global TT - RAII via unique_ptr
extern std::unique_ptr<TranspositionTableAtomic> globalTT;

inline void initTTAtomic(size_t sizeMB = 512) {
    globalTT = std::make_unique<TranspositionTableAtomic>(sizeMB);
}

#endif // TT_ATOMIC_H
