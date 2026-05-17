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

struct HistoryManager {
    ButterflyHistory butterfly;
    CountermoveHistory countermove;
    ContinuationHistory continuation;
    CaptureHistory capture;
    PieceSquareHistory pieceSquare;

    void clear() {
        butterfly.clear();
        countermove.clear();
        continuation.clear();
        capture.clear();
        pieceSquare.clear();
    }

    void age() {
        butterfly.age();
        continuation.age();
    }

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
