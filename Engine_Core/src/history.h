#pragma once
#ifndef HISTORY_H
#define HISTORY_H

#include "types.h"
#include "position.h"
#include <algorithm>
#include <cstring>

class ButterflyHistory {
private:
    static constexpr int MAX_HISTORY = 16384;
    int history[2][64][64];
public:
    ButterflyHistory() { clear(); }
    void clear() { std::memset(history, 0, sizeof(history)); }
    void update(Color c, Square from, Square to, int bonus) {
        history[c][from][to] += bonus - history[c][from][to] * abs(bonus) / MAX_HISTORY;
    }
    int get(Color c, Square from, Square to) const { return history[c][from][to]; }
    void age() {
        for (int c = 0; c < 2; c++)
            for (int from = 0; from < 64; from++)
                for (int to = 0; to < 64; to++)
                    history[c][from][to] /= 2;
    }
};

class CountermoveHistory {
private:
    Move countermoves[6][64];
public:
    CountermoveHistory() { clear(); }
    void clear() {
        for (int p = 0; p < 6; p++)
            for (int sq = 0; sq < 64; sq++)
                countermoves[p][sq] = nullMove();
    }
    void update(Piece piece, Square to, Move counter) { countermoves[piece][to] = counter; }
    Move get(Piece piece, Square to) const { return countermoves[piece][to]; }
};

class ContinuationHistory {
private:
    static constexpr int MAX_HISTORY = 16384;
    int history[6][64][6][64];
public:
    ContinuationHistory() { clear(); }
    void clear() { std::memset(history, 0, sizeof(history)); }
    void update(Piece p1, Square sq1, Piece p2, Square sq2, int bonus) {
        history[p1][sq1][p2][sq2] += bonus - history[p1][sq1][p2][sq2] * abs(bonus) / MAX_HISTORY;
    }
    int get(Piece p1, Square sq1, Piece p2, Square sq2) const { return history[p1][sq1][p2][sq2]; }
    void age() {
        for (int p1 = 0; p1 < 6; p1++)
            for (int sq1 = 0; sq1 < 64; sq1++)
                for (int p2 = 0; p2 < 6; p2++)
                    for (int sq2 = 0; sq2 < 64; sq2++)
                        history[p1][sq1][p2][sq2] /= 2;
    }
};

class CaptureHistory {
private:
    static constexpr int MAX_HISTORY = 16384;
    int history[6][64][6];
public:
    CaptureHistory() { clear(); }
    void clear() { std::memset(history, 0, sizeof(history)); }
    void update(Piece attacker, Square to, Piece victim, int bonus) {
        history[attacker][to][victim] += bonus - history[attacker][to][victim] * abs(bonus) / MAX_HISTORY;
    }
    int get(Piece attacker, Square to, Piece victim) const { return history[attacker][to][victim]; }
};

class PieceSquareHistory {
private:
    static constexpr int MAX_HISTORY = 16384;
    int history[2][6][64];
public:
    PieceSquareHistory() { clear(); }
    void clear() { std::memset(history, 0, sizeof(history)); }
    void update(Color c, Piece p, Square sq, int bonus) {
        history[c][p][sq] += bonus - history[c][p][sq] * abs(bonus) / MAX_HISTORY;
    }
    int get(Color c, Piece p, Square sq) const { return history[c][p][sq]; }
};

/* =============================================================
   CORRECTION HISTORY
   -------------------------------------------------------------
   Tracks the signed difference between static eval and the
   actual search-returned score, indexed by various position
   features. At eval-call time, the corresponding correction
   is added to the raw static eval to produce a "corrected"
   evaluation that the search uses for pruning decisions.

   Intuition: positional patterns (pawn structures, piece
   placements, recent-move context) where our hand-crafted
   eval is systematically biased get corrected. This is the
   single biggest search improvement of the 2024-2025 SF era
   (~40 Elo on top of an already-strong baseline).

   References to cite in your report:
     [1] Disservin, "Introduce correction histories,"
         official-stockfish/Stockfish PR #5048, Jan 2024.
         https://github.com/official-stockfish/Stockfish/pull/5048
     [2] Tutkun (OmerFarukTutkun), "Continuation correction
         history," Stockfish PR #5617, Oct 2024.
         https://github.com/official-stockfish/Stockfish/pull/5617
     [3] Viren6, "Introduce correction history quad extensions,"
         Stockfish PR #5767, Jan 2025.
     [4] Novak (martinnovaak), Motor engine PR #162 -- earliest
         continuation correction history implementation.
     [5] Stockfish release notes for SF18 (Jan 2026): "heavily
         refined search, utilising 'Correction History' to
         dynamically adjust evaluations based on patterns found
         during the search itself."
   ============================================================= */

class CorrectionHistory {
public:
    // Stored at 8x scale internally (i.e. correction[idx] = 8 * cp_bias)
    // so we get smooth low-rate updates without floating point.
    // MAX_CORRECTION caps the raw stored value; the eval-side scale
    // factor divides by 8 to get the actual centipawn correction.
    static constexpr int MAX_CORRECTION = 16384;
    static constexpr int CORRECTION_GRAIN = 8;

    // Indexed by [side-to-move][feature_hash & MASK].
    // 16k entries × 2 sides × 4 bytes = 128 KB per CorrectionHistory.
    static constexpr int SIZE = 16384;
    static constexpr uint64_t MASK = SIZE - 1;

private:
    int table[2][SIZE];

public:
    CorrectionHistory() { clear(); }
    void clear() { std::memset(table, 0, sizeof(table)); }

    // Update with a "diff" already scaled by CORRECTION_GRAIN.
    // weight = how strongly this update influences the running
    // average (1..256). Higher search depths/scores produce
    // higher weights. The formula is the standard EMA-style
    // gravity update: new = old + weight * (target - old) / 256.
    void update(Color c, uint64_t featureHash, int scaledDiff, int weight) {
        int idx = featureHash & MASK;
        int& entry = table[c][idx];
        int adjusted = entry + (weight * (scaledDiff - entry)) / 256;
        if (adjusted >  MAX_CORRECTION) adjusted =  MAX_CORRECTION;
        if (adjusted < -MAX_CORRECTION) adjusted = -MAX_CORRECTION;
        entry = adjusted;
    }

    // Return correction in centipawns (already scaled back).
    int get(Color c, uint64_t featureHash) const {
        int idx = featureHash & MASK;
        return table[c][idx] / CORRECTION_GRAIN;
    }

    // Raw (unscaled) value -- exposed for combined-source weighting
    // where the caller wants to average several signals before
    // dividing back to centipawns.
    int getRaw(Color c, uint64_t featureHash) const {
        int idx = featureHash & MASK;
        return table[c][idx];
    }
};

/* Continuation correction history: indexed by (piece, to-square) of
   the previous move(s) rather than a position hash. Captures "eval
   bias after a move of this kind to this square." Smaller table. */
class ContinuationCorrectionHistory {
public:
    static constexpr int MAX_CORRECTION = 16384;
    static constexpr int CORRECTION_GRAIN = 8;

private:
    // [side-to-move][prev_piece][prev_to_square]
    int table[2][6][64];

public:
    ContinuationCorrectionHistory() { clear(); }
    void clear() { std::memset(table, 0, sizeof(table)); }

    void update(Color c, Piece prevPiece, Square prevTo, int scaledDiff, int weight) {
        if (prevPiece >= 6 || prevTo >= 64) return;
        int& entry = table[c][prevPiece][prevTo];
        int adjusted = entry + (weight * (scaledDiff - entry)) / 256;
        if (adjusted >  MAX_CORRECTION) adjusted =  MAX_CORRECTION;
        if (adjusted < -MAX_CORRECTION) adjusted = -MAX_CORRECTION;
        entry = adjusted;
    }

    int get(Color c, Piece prevPiece, Square prevTo) const {
        if (prevPiece >= 6 || prevTo >= 64) return 0;
        return table[c][prevPiece][prevTo] / CORRECTION_GRAIN;
    }
};

/* Helpers for computing the position-feature hashes that correction
   history indexes. Both are O(piece_count) -- fast, no incremental
   maintenance needed because they're only called at evaluate() sites
   on the search hot path (not at every makeMove). */

#include "zobrist.h"

inline uint64_t computePawnHash(const Position& pos) {
    uint64_t h = 0;
    for (int c = 0; c < 2; c++) {
        U64 bb = pos.pieces[c][PAWN];
        while (bb) {
            int sq = __builtin_ctzll(bb);
            bb &= bb - 1;
            h ^= Zobrist::piece[c][PAWN][sq];
        }
    }
    return h;
}

// Non-pawn-piece hash for one side. Used by SF-style two-table
// non-pawn correction history (one table per side's pieces).
inline uint64_t computeNonPawnHash(const Position& pos, Color side) {
    uint64_t h = 0;
    for (int p = ROOK; p <= KING; p++) {  // skip PAWN
        U64 bb = pos.pieces[side][p];
        while (bb) {
            int sq = __builtin_ctzll(bb);
            bb &= bb - 1;
            h ^= Zobrist::piece[side][p][sq];
        }
    }
    return h;
}

struct HistoryManager {
    ButterflyHistory butterfly;
    CountermoveHistory countermove;
    ContinuationHistory continuation;
    CaptureHistory capture;
    PieceSquareHistory pieceSquare;

    // Correction histories. Three variants, blended on read.
    CorrectionHistory pawnCorr;       // indexed by pawn hash
    CorrectionHistory nonPawnCorrW;   // indexed by white non-pawn hash
    CorrectionHistory nonPawnCorrB;   // indexed by black non-pawn hash
    ContinuationCorrectionHistory contCorr;

    void clear() {
        butterfly.clear();
        countermove.clear();
        continuation.clear();
        capture.clear();
        pieceSquare.clear();
        pawnCorr.clear();
        nonPawnCorrW.clear();
        nonPawnCorrB.clear();
        contCorr.clear();
    }

    void age() {
        butterfly.age();
        continuation.age();
        // Correction histories age naturally via the EMA update rule
        // -- old entries get washed out over many updates. No explicit
        // halving needed (and halving them would slow learning).
    }

    /* ===== CORRECTION HISTORY API =====

       Apply the combined correction from all three sources to a raw
       static eval. Result is clamped to TB-win range so we never
       turn a real eval into a fake mate.

       prevPiece/prevTo: piece type and destination of the move played
       at ply-1 (or NO_PIECE/NO_SQUARE if at root or after a null move).
    */
    int applyCorrection(int rawEval, const Position& pos,
                        Piece prevPiece, Square prevTo) const {
        Color stm = pos.sideToMove;
        uint64_t pawnHash = computePawnHash(pos);
        uint64_t npwHash  = computeNonPawnHash(pos, WHITE);
        uint64_t npbHash  = computeNonPawnHash(pos, BLACK);

        // Each source contributes; pawn structure carries the most
        // weight as it's the most stable indicator of bias.
        int correction = 0;
        correction += pawnCorr.get(stm, pawnHash)        * 3;
        correction += nonPawnCorrW.get(stm, npwHash)     * 1;
        correction += nonPawnCorrB.get(stm, npbHash)     * 1;
        correction += contCorr.get(stm, prevPiece, prevTo) * 2;
        correction /= 7;  // weighted average

        int corrected = rawEval + correction;
        // Never let correction push eval into TB-win territory
        if (corrected >  20000) corrected =  20000;
        if (corrected < -20000) corrected = -20000;
        return corrected;
    }

    /* Update all correction histories given the divergence between
       static eval and the actual returned search score.

         diff   = bestScore - staticEval  (in centipawns)
         depth  = search depth that produced bestScore
         bound  = TT bound flag (EXACT/ALPHA/BETA) that produced score
                  -- callers should skip the update when the score is
                  a fail-low (alpha bound) and diff < 0, or fail-high
                  (beta bound) and diff > 0 -- because those bounds
                  don't tell us the true eval bias direction.

       Update weight scales with depth: deeper searches produce more
       trustworthy diffs, so they shift the running estimate more.
    */
    void updateCorrection(const Position& pos, Piece prevPiece, Square prevTo,
                          int diff, int depth) {
        Color stm = pos.sideToMove;

        // Scale diff to the internal grain
        int scaledDiff = diff * CorrectionHistory::CORRECTION_GRAIN;

        // Clamp absurd diffs (mate scores, eval explosions, etc.)
        const int maxScaled = CorrectionHistory::MAX_CORRECTION;
        if (scaledDiff >  maxScaled) scaledDiff =  maxScaled;
        if (scaledDiff < -maxScaled) scaledDiff = -maxScaled;

        // Weight: grows with depth, capped at 256 (the EMA denominator).
        // depth 1 -> w=8, depth 5 -> w=32, depth 16 -> w=128, depth 32 -> w=256.
        int weight = std::min(256, depth * depth + 8);

        uint64_t pawnHash = computePawnHash(pos);
        uint64_t npwHash  = computeNonPawnHash(pos, WHITE);
        uint64_t npbHash  = computeNonPawnHash(pos, BLACK);

        pawnCorr.update(stm, pawnHash, scaledDiff, weight);
        nonPawnCorrW.update(stm, npwHash, scaledDiff, weight);
        nonPawnCorrB.update(stm, npbHash, scaledDiff, weight);
        if (prevPiece < 6 && prevTo < 64) {
            contCorr.update(stm, prevPiece, prevTo, scaledDiff, weight);
        }
    }
    /* ===== END CORRECTION HISTORY API ===== */

    void updateQuiet(const Position& pos, Move move, int bonus, const Move* prevMove = nullptr) {
        Color c = pos.sideToMove;
        Piece piece = pos.pieceAt(move.from);
        if (piece == NO_PIECE) return;

        butterfly.update(c, move.from, move.to, bonus);
        pieceSquare.update(c, piece, move.to, bonus);

        if (prevMove && prevMove->from != NO_SQUARE) {
            Piece prevPiece = pos.pieceAt(prevMove->to);
            if (prevPiece != NO_PIECE)
                continuation.update(prevPiece, prevMove->to, piece, move.to, bonus);
        }
    }

    void updateCapture(const Position& pos, Move move, int depth) {
        Piece attacker = pos.pieceAt(move.from);
        Piece victim = pos.pieceAt(move.to);
        if (attacker == NO_PIECE || victim == NO_PIECE) return;
        int bonus = std::min(depth * depth, 400);
        capture.update(attacker, move.to, victim, bonus);
    }

    int getQuietScore(const Position& pos, Move move, const Move* prevMove = nullptr) const {
        Color c = pos.sideToMove;
        Piece piece = pos.pieceAt(move.from);
        if (piece == NO_PIECE) return 0;

        int score = butterfly.get(c, move.from, move.to);
        score += pieceSquare.get(c, piece, move.to);

        if (prevMove && prevMove->from != NO_SQUARE) {
            Piece prevPiece = pos.pieceAt(prevMove->to);
            if (prevPiece != NO_PIECE)
                score += continuation.get(prevPiece, prevMove->to, piece, move.to);
        }
        return score;
    }

    int getCaptureScore(const Position& pos, Move move) const {
        Piece attacker = pos.pieceAt(move.from);
        Piece victim = pos.pieceAt(move.to);
        if (attacker == NO_PIECE || victim == NO_PIECE) return 0;
        return capture.get(attacker, move.to, victim);
    }
};

inline int historyBonus(int depth) {
    return std::min(16 * depth * depth + 128 * std::max(depth - 1, 0), 1400);
}

#endif // HISTORY_H
