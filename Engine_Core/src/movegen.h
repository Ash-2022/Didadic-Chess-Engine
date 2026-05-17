#pragma once
#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "utilities.h"
#include "position.h"
#include <vector>

constexpr int MAX_PLY = 128;

class MoveGenerator {
public:
    static void printMove(const Move& move, const Position& pos);

    // Stack-allocated MoveList versions (used in search hot path)
    static void generateAllMoves(const Position& pos, MoveList& moves);
    static void generateCaptures(const Position& pos, MoveList& moves);

    // Legacy vector versions (used in perft, UCI)
    static void generateAllMoves(const Position& pos, std::vector<Move>& moves);

private:
    static void generateKnightMoves(const Position& pos, MoveList& moves);
    static void generateKingMoves(const Position& pos, MoveList& moves);
    static void generateBishopMoves(const Position& pos, MoveList& moves);
    static void generateRookMoves(const Position& pos, MoveList& moves);
    static void generateQueenMoves(const Position& pos, MoveList& moves);
    static void generatePawnMoves(const Position& pos, MoveList& moves);
    static void generatePawnCaptures(const Position& pos, MoveList& moves);
};

#endif // MOVEGEN_H
