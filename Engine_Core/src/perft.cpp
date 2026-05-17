#include "perft.h"
#include "movegen.h"
#include <chrono>

static MoveList moveStack[MAX_PLY];

uint64_t perft(Position& pos, int depth)
{
    if (depth == 0)
        return 1;

    auto& moves = moveStack[depth];
    moves.clear();

    MoveGenerator::generateAllMoves(pos, moves);

    uint64_t nodes = 0;

    for (int i = 0; i < moves.size(); i++) {
        const auto& m = moves[i];
        Undo undo;
        pos.makeMove(m, undo);

        Color justMoved = (pos.sideToMove == WHITE ? BLACK : WHITE);
        if (!pos.isKingInCheck(justMoved)) {
            nodes += perft(pos, depth - 1);
        }

        pos.undoMove(m, undo);
    }

    return nodes;
}

void perftDivide(Position& pos, int depth)
{
    std::vector<Move> rootMoves;
    rootMoves.reserve(256);

    MoveGenerator::generateAllMoves(pos, rootMoves);

    uint64_t total = 0;

    for (const auto& m : rootMoves) {
        Undo undo;
        pos.makeMove(m, undo);

        Color justMoved = (pos.sideToMove == WHITE ? BLACK : WHITE);
        uint64_t nodes = 0;

        if (!pos.isKingInCheck(justMoved)) {
            nodes = perft(pos, depth - 1);
        }

        pos.undoMove(m, undo);

        MoveGenerator::printMove(m, pos);
        std::cout << ": " << nodes << "\n";
        total += nodes;
    }

    std::cout << "Total: " << total << "\n";
}

void benchmarkPerft(Position& pos, int depth, int runs)
{
    using clock = std::chrono::high_resolution_clock;

    uint64_t nodes = 0;
    double totalTime = 0.0;

    for (int i = 0; i < runs; i++) {
        auto start = clock::now();
        nodes = perft(pos, depth);
        auto end = clock::now();

        totalTime += std::chrono::duration<double>(end - start).count();
    }

    double avgTime = totalTime / runs;
    double nps = nodes / avgTime;

    std::cout << "perft(" << depth << ") avg over " << runs << " runs\n";
    std::cout << "Nodes: " << nodes << "\n";
    std::cout << "Avg time: " << avgTime << " s\n";
    std::cout << "NPS: " << static_cast<uint64_t>(nps) << "\n";
}
