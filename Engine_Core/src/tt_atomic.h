#pragma once
#ifndef TT_ATOMIC_H
#define TT_ATOMIC_H

#include "types.h"
#include <atomic>
#include <climits>
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

/* =========================
   4-WAY BUCKETED LOCK-FREE TT
   -----------------------------------------------------------
   Each bucket holds 4 TTEntryAtomic entries (4 * 16 B = 64 B,
   one x86-64 cache line). Probing iterates all 4 in a single
   cache-line load. Store picks a victim by a 3-tier policy:

     1. Exact key match (refresh-in-place at same depth or deeper)
     2. Empty / stale-generation slot
     3. Lowest (depth + age_bonus) entry in the bucket

   Compared to a single-entry-per-slot scheme this gives:
     - ~4x more tolerance to hash collisions (4 candidate slots)
     - No extra memory bandwidth (same cache line either way)
     - Significantly higher useful-entry retention for deep PV
       lines, which is where collision loss hurts most

   References:
     - Hyatt & Cozzie, "The Effect of Hash Signature Collisions
       in a Chess Program," ICGA Journal 2005.
     - Stockfish TT cluster design (cluster of 3 entries since
       2014; bucket sizes of 4-8 are common in modern engines).
   ========================= */

struct alignas(64) TTBucket {
    static constexpr int BUCKET_SIZE = 4;
    TTEntryAtomic entries[BUCKET_SIZE];

    TTBucket() = default;

    // Non-copyable (contains non-copyable atomic entries)
    TTBucket(const TTBucket&) = delete;
    TTBucket& operator=(const TTBucket&) = delete;
    TTBucket(TTBucket&&) = delete;
    TTBucket& operator=(TTBucket&&) = delete;
};

class TranspositionTableAtomic {
private:
    std::unique_ptr<TTBucket[]> table_;
    size_t numBuckets_ = 0;
    uint8_t generation_ = 0;

    static std::unique_ptr<TTBucket[]> allocTable(size_t count) {
        return std::unique_ptr<TTBucket[]>(new TTBucket[count]());
    }

public:
    explicit TranspositionTableAtomic(size_t sizeMB = 512) {
        resize(sizeMB);
    }

    TranspositionTableAtomic(const TranspositionTableAtomic&) = delete;
    TranspositionTableAtomic& operator=(const TranspositionTableAtomic&) = delete;
    TranspositionTableAtomic(TranspositionTableAtomic&&) = delete;
    TranspositionTableAtomic& operator=(TranspositionTableAtomic&&) = delete;

    ~TranspositionTableAtomic() = default;

    void clear() {
        for (size_t b = 0; b < numBuckets_; b++) {
            for (int i = 0; i < TTBucket::BUCKET_SIZE; i++) {
                TTEntryAtomic& e = table_[b].entries[i];
                e.keyData.store(0, std::memory_order_relaxed);
                e.scoreMove.store(0, std::memory_order_relaxed);
                e.generation = 0;
            }
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

    TTBucket& getBucket(uint64_t key) {
        return table_[key % numBuckets_];
    }

    // Probe: scan all 4 entries in the bucket. Return the first one
    // that passes lockless XOR verification against `key`.
    bool probe(uint64_t key, int16_t& score, Move& move, int8_t& depth,
               uint8_t& flag, int ply) {
        TTBucket& bucket = getBucket(key);

        for (int i = 0; i < TTBucket::BUCKET_SIZE; i++) {
            uint8_t stored_gen;
            if (bucket.entries[i].readVerified(key, score, move, depth, flag, stored_gen)) {
                // Adjust mate scores to be relative to the current ply
                if (score > 28000) score -= ply;
                else if (score < -28000) score += ply;
                return true;
            }
        }
        return false;
    }

    // Store: choose a victim in the bucket by replacement policy.
    // Priority order:
    //   1. Same-key slot (refresh)
    //   2. Empty / stale-generation slot (any depth)
    //   3. Lowest "value" slot where value = depth - 4 * age_penalty
    void store(uint64_t key, int16_t score, Move move, int8_t depth,
               uint8_t flag, int ply) {
        TTBucket& bucket = getBucket(key);

        if (score > 28000) score += ply;
        else if (score < -28000) score -= ply;

        // Phase 1: look for same-key entry to refresh.
        // Refreshing the same key keeps the deepest known result for
        // this position regardless of which slot it landed in.
        for (int i = 0; i < TTBucket::BUCKET_SIZE; i++) {
            TTEntryAtomic& e = bucket.entries[i];
            if (e.keyMatchesApprox(key)) {
                // Same position. Replace only if new is at least as
                // useful (depth + 2 fudge factor accounts for the new
                // entry having fresh info even if same nominal depth).
                if (depth + 2 >= e.getDepth()) {
                    e.pack(key, score, move, depth, flag, generation_);
                }
                return;
            }
        }

        // Phase 2: find best victim.
        // Score each entry by (depth - age_penalty) and replace the
        // lowest-scoring one. Stale-generation entries get a large
        // penalty so they're preferentially evicted.
        int victimIdx = 0;
        int victimScore = INT32_MAX;
        for (int i = 0; i < TTBucket::BUCKET_SIZE; i++) {
            TTEntryAtomic& e = bucket.entries[i];
            int entryDepth = e.getDepth();
            uint8_t entryGen = e.getGeneration();

            // Age penalty in "depth units". A stale entry from an old
            // generation is worth ~8 fewer depth points than a fresh
            // entry at the same nominal depth, so it gets evicted first.
            int agePenalty = (entryGen == generation_) ? 0 : 8;
            int entryValue = entryDepth - agePenalty;

            if (entryValue < victimScore) {
                victimScore = entryValue;
                victimIdx = i;
            }
        }

        bucket.entries[victimIdx].pack(key, score, move, depth, flag, generation_);
    }

    int hashfull() const {
        // Sample first 250 buckets * 4 entries = 1000 entries.
        int count = 0;
        constexpr int sample_buckets = 250;
        size_t n = std::min(static_cast<size_t>(sample_buckets), numBuckets_);
        for (size_t b = 0; b < n; b++) {
            for (int i = 0; i < TTBucket::BUCKET_SIZE; i++) {
                if (table_[b].entries[i].getGeneration() == generation_)
                    count++;
            }
        }
        return count;
    }

    void resize(size_t sizeMB) {
        // Each bucket is 64 bytes (one cache line).
        numBuckets_ = (sizeMB * 1024 * 1024) / sizeof(TTBucket);
        if (numBuckets_ == 0) numBuckets_ = 1;
        table_ = allocTable(numBuckets_);
        generation_ = 0;
    }

    size_t getSize() const { return numBuckets_ * TTBucket::BUCKET_SIZE; }
    size_t getSizeMB() const { return (numBuckets_ * sizeof(TTBucket)) / (1024 * 1024); }
};

// Global TT - RAII via unique_ptr
extern std::unique_ptr<TranspositionTableAtomic> globalTT;

inline void initTTAtomic(size_t sizeMB = 512) {
    globalTT = std::make_unique<TranspositionTableAtomic>(sizeMB);
}

#endif // TT_ATOMIC_H
