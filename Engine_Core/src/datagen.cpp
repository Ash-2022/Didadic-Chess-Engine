#include "datagen.h"
#include "position.h"
#include "movegen.h"
#include "search.h"
#include "tt_atomic.h"
#include <fstream>
#include <iostream>
#include <random>
#include <vector>

namespace {

struct Record {
    std::string fen;
    int whiteScore;
};

int legalMoves(Position& pos, std::vector<Move>& out) {
    std::vector<Move> pseudo;
    MoveGenerator::generateAllMoves(pos, pseudo);
    out.clear();
    for (const Move& m : pseudo) {
        Undo u;
        Color us = pos.sideToMove;
        pos.makeMove(m, u);
        if (!pos.isKingInCheck(us)) out.push_back(m);
        pos.undoMove(m, u);
    }
    return (int)out.size();
}

} // namespace

void runDatagen(const std::string& outPath, int games, int depth, unsigned seed) {
    std::mt19937 rng(seed);
    std::ofstream out(outPath, std::ios::app);
    if (!out) {
        std::cerr << "datagen: cannot open " << outPath << std::endl;
        return;
    }
    g_silentSearch = true;

    constexpr int MAX_GAME_PLIES = 300;
    constexpr int ADJ_SCORE      = 1500; // white-POV adjudication threshold
    constexpr int ADJ_STREAK     = 6;

    long long totalPositions = 0;
    std::vector<Move> moves;

    for (int game = 0; game < games; game++) {
        Position pos;
        pos.setStartPosition();
        if (globalTT) globalTT->nextGeneration();

        // --- Random opening: 8 or 9 plies ---
        int openPlies = 8 + (int)(rng() % 2);
        bool openingOk = true;
        for (int i = 0; i < openPlies; i++) {
            if (legalMoves(pos, moves) == 0) { openingOk = false; break; }
            Undo u;
            pos.makeMove(moves[rng() % moves.size()], u);
        }
        if (!openingOk) { game--; continue; }

        std::vector<Record> buf;
        buf.reserve(256);
        double result = 0.5;
        int winStreak = 0;

        for (int ply = openPlies; ply < MAX_GAME_PLIES; ply++) {
            int numLegal = legalMoves(pos, moves);
            if (numLegal == 0) {
                if (pos.isKingInCheck(pos.sideToMove))
                    result = (pos.sideToMove == WHITE) ? 0.0 : 1.0; // mated
                else
                    result = 0.5; // stalemate
                break;
            }
            if (pos.halfmoveClock >= 100 || pos.isRepetition()) {
                result = 0.5;
                break;
            }

            g_timeUp.store(false, std::memory_order_release);
            g_stopSearch.store(false, std::memory_order_release);
            g_searchStartTime = std::chrono::steady_clock::now();
            g_allocatedTime = 0;

            Move best = iterativeDeepening(pos, depth);
            int score = g_lastRootScore.load(std::memory_order_relaxed);
            if (best.isNull()) { result = 0.5; break; }

            int whiteScore = (pos.sideToMove == WHITE) ? score : -score;

            // Forced mate found: adjudicate immediately
            if (whiteScore >= MATE - 1000)  { result = 1.0; break; }
            if (whiteScore <= -MATE + 1000) { result = 0.0; break; }

            // Big-advantage adjudication
            if (whiteScore >= ADJ_SCORE || whiteScore <= -ADJ_SCORE) winStreak++;
            else winStreak = 0;
            if (winStreak >= ADJ_STREAK) {
                result = (whiteScore > 0) ? 1.0 : 0.0;
                break;
            }

            // Record quiet, balanced positions only
            bool quiet = !(best.flags & (CAPTURE | PROMOTION)) &&
                         !pos.isKingInCheck(pos.sideToMove) &&
                         whiteScore < ADJ_SCORE && whiteScore > -ADJ_SCORE;
            if (quiet) {
                buf.push_back({pos.toFEN(), whiteScore});
            }

            Undo u;
            pos.makeMove(best, u);
        }

        for (const Record& r : buf)
            out << r.fen << " | " << r.whiteScore << " | " << result << "\n";
        totalPositions += (long long)buf.size();

        if ((game + 1) % 25 == 0) {
            out.flush();
            std::cout << "datagen: " << (game + 1) << "/" << games
                      << " games, " << totalPositions << " positions" << std::endl;
        }
    }

    out.flush();
    std::cout << "datagen: done, " << totalPositions << " positions -> "
              << outPath << std::endl;
}
