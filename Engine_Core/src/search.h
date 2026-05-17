#pragma once
#ifndef SEARCH_H
#define SEARCH_H

#include <cstdint>
#include <cstring>
#include <cmath>
#include <atomic>
#include <chrono>
#include "movegen.h"
#include "history.h"

constexpr int INF  = 30000;
constexpr int MATE = 29000;

extern std::atomic<bool> g_timeUp;
extern std::atomic<bool> g_stopSearch;
extern std::atomic<Move> g_bestMove;
extern std::chrono::steady_clock::time_point g_searchStartTime;
extern int g_allocatedTime;

// Precomputed LMR reduction table
extern int LMR_TABLE[64][64];
void initLMR();

// Thread-local TT cache to reduce main TT contention
struct TTCacheEntry {
    uint64_t key;
    int16_t score;
    Move move;
    int8_t depth;
    uint8_t flag;
    
    TTCacheEntry() : key(0), score(0), move(nullMove()), depth(0), flag(0) {}
    
    TTCacheEntry(uint64_t k, int16_t s, Move m, int8_t d, uint8_t f)
        : key(k), score(s), move(m), depth(d), flag(f) {}
};

struct ThreadLocalTTCache {
    static constexpr int SIZE = 4096;
    TTCacheEntry cache[SIZE];
    
    void clear();
    bool probe(uint64_t key, TTCacheEntry& entry);
    void store(const TTCacheEntry& entry);
};

struct SearchState {
    Move killerMoves[MAX_PLY][2];
    HistoryManager* history;
    Move previousMoves[MAX_PLY];
    Move counterMoves[6][64];
    int evalStack[MAX_PLY];
    uint64_t nodesSearched = 0;
    int threadID = 0;
    
    // PV tracking for full variation output
    Move pvTable[MAX_PLY][MAX_PLY];
    int pvLength[MAX_PLY];
    
    // Thread-local TT cache
    ThreadLocalTTCache ttCache;

    SearchState();
    ~SearchState();
    SearchState(const SearchState&) = delete;
    SearchState& operator=(const SearchState&) = delete;

    void clear();
    void updateKillers(Move move, int ply);
    void updateHistory(const Position& pos, Move move, int depth, int ply);
    void updateCountermove(const Position& pos, Move prevMove, Move response);
    bool isKiller(Move move, int ply) const;
    bool isCountermove(const Position& pos, Move move, int ply) const;
    void updatePV(Move move, int ply);
};

int negamax(Position& pos, SearchState& state, int depth, int ply,
            int alpha, int beta, bool allowNull);
int quiescence(Position& pos, SearchState& state, int ply, int alpha, int beta);

Move iterativeDeepening(Position& pos, int maxDepth);
Move iterativeDeepeningST(Position& pos, int maxDepth, SearchState& state);

int scoreMove(const Position& pos, SearchState& state, const Move& m, int ply);
void pickNextMove(MoveList& moves, int current,
                  const Position& pos, SearchState& state, int ply);
int see(const Position& pos, const Move& move);

inline int scoreToTT(int score, int ply) {
    if (score > MATE - 1000) return score + ply;
    if (score < -MATE + 1000) return score - ply;
    return score;
}

inline int scoreFromTT(int score, int ply) {
    if (score > MATE - 1000) return score - ply;
    if (score < -MATE + 1000) return score + ply;
    return score;
}

bool hasNonPawnMaterial(const Position& pos, Color side);

#endif // SEARCH_H
