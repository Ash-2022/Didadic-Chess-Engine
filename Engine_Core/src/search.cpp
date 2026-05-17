#include "search.h"
#include "evaluation.h"
#include "position.h"
#include "uci.h"
#include "tt_atomic.h"
#include "thread_pool.h"
#include <algorithm>
#include <iostream>
#include <chrono>
#include <cmath>

/* Time management globals */
std::atomic<bool> g_timeUp{false};
std::atomic<Move> g_bestMove{nullMove()};
std::chrono::steady_clock::time_point g_searchStartTime;
int g_allocatedTime = 0;

/* Precomputed LMR table - OPTIMIZED */
int LMR_TABLE[64][64];

void initLMR() {
    for (int d = 0; d < 64; d++) {
        for (int m = 0; m < 64; m++) {
            if (d == 0 || m == 0) {
                LMR_TABLE[d][m] = 0;
            } else {
                // More aggressive: log(d) * log(m) / 2.0
                LMR_TABLE[d][m] = int(0.5 + log(double(d)) * log(double(m)) / 2.0);
            }
        }
    }
}

/* ThreadLocalTTCache implementation */
void ThreadLocalTTCache::clear() {
    std::memset(cache, 0, sizeof(cache));
}

bool ThreadLocalTTCache::probe(uint64_t key, TTCacheEntry& entry) {
    int idx = key & (SIZE - 1);
    if (cache[idx].key == key) {
        entry = cache[idx];
        return true;
    }
    return false;
}

void ThreadLocalTTCache::store(const TTCacheEntry& entry) {
    int idx = entry.key & (SIZE - 1);
    cache[idx] = entry;
}

/* SearchState methods */
SearchState::SearchState() {
    history = new HistoryManager();
    clear();
}

SearchState::~SearchState() {
    delete history;
}

void SearchState::clear() {
    for (int i = 0; i < MAX_PLY; i++) {
        killerMoves[i][0] = nullMove();
        killerMoves[i][1] = nullMove();
        previousMoves[i] = nullMove();
        evalStack[i] = 0;
        pvLength[i] = 0;
    }
    for (int p = 0; p < 6; p++) {
        for (int sq = 0; sq < 64; sq++) {
            counterMoves[p][sq] = nullMove();
        }
    }
    history->clear();
    nodesSearched = 0;
    ttCache.clear();
}

void SearchState::updateKillers(Move move, int ply) {
    if (move == killerMoves[ply][0]) return;
    killerMoves[ply][1] = killerMoves[ply][0];
    killerMoves[ply][0] = move;
}

void SearchState::updateHistory(const Position& pos, Move move, int depth, int ply) {
    Move* prevMove = (ply > 0) ? &previousMoves[ply - 1] : nullptr;
    int bonus = historyBonus(depth);
    history->updateQuiet(pos, move, bonus, prevMove);
}

void SearchState::updateCountermove(const Position& pos, Move prevMove, Move response) {
    if (prevMove.isNull()) return;
    Piece piece = pos.pieceAt(prevMove.to);
    if (piece != NO_PIECE && piece < 6) {
        counterMoves[piece][prevMove.to] = response;
    }
}

bool SearchState::isKiller(Move move, int ply) const {
    return (move == killerMoves[ply][0]) || (move == killerMoves[ply][1]);
}

bool SearchState::isCountermove(const Position& pos, Move move, int ply) const {
    if (ply == 0) return false;
    Move prevMove = previousMoves[ply - 1];
    if (prevMove.isNull()) return false;
    Piece piece = pos.pieceAt(prevMove.to);
    if (piece == NO_PIECE) return false;
    return counterMoves[piece][prevMove.to] == move;
}

void SearchState::updatePV(Move move, int ply) {
    pvTable[ply][0] = move;
    pvLength[ply] = 1;
    
    // Copy child PV
    if (ply + 1 < MAX_PLY) {
        for (int i = 0; i < pvLength[ply + 1] && pvLength[ply] < MAX_PLY; i++) {
            pvTable[ply][pvLength[ply]] = pvTable[ply + 1][i];
            pvLength[ply]++;
        }
    }
}

/* Time check - OPTIMIZED: check every 16K nodes instead of 4K */
inline bool checkTime() {
    if (g_allocatedTime <= 0) return false;
    
    // Check every 16384 nodes (was 4096)
    static thread_local uint64_t nodeCounter = 0;
    if ((++nodeCounter & 16383) != 0) return false;
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_searchStartTime).count();
    
    if (elapsed >= g_allocatedTime) {
        g_timeUp.store(true, std::memory_order_relaxed);
        g_stopSearch.store(true, std::memory_order_relaxed);
        return true;
    }
    return false;
}

/* SEE - OPTIMIZED */
int see(const Position& pos, const Move& move) {
    if (!(move.flags & CAPTURE)) return 0;
    
    Piece attacker = pos.board[move.from];
    Piece victim = pos.board[move.to];
    
    if (attacker == NO_PIECE || victim == NO_PIECE) return 0;
    
    // Quick heuristic: good captures
    int gain = pieceValue[victim] - pieceValue[attacker];
    if (gain >= 0) return gain;
    
    // Losing capture - check if square is defended
    Color us = pos.sideToMove;
    Color them = (us == WHITE ? BLACK : WHITE);
    
    if (pos.isSquareAttacked(move.to, them)) {
        return pieceValue[victim] - pieceValue[attacker];
    }
    
    return pieceValue[victim];
}

/* OPTIMIZED Move ordering */
int scoreMove(const Position& pos, SearchState& state, const Move& m, int ply) {
    // Good captures
    if (m.flags & CAPTURE) {
        Piece victim = pos.board[m.to];
        Piece attacker = pos.board[m.from];
        
        if (victim == NO_PIECE || attacker == NO_PIECE) return 10000;
        
        int mvvLva = pieceValue[victim] * 10 - pieceValue[attacker];
        
        if (mvvLva >= 0) {
            return 15000 + mvvLva;
        } else {
            int seeScore = see(pos, m);
            if (seeScore >= 0) return 15000 + seeScore;
            return -5000 + seeScore;
        }
    }
    
    // Promotions
    if (m.flags & PROMOTION) {
        return 14000 + pieceValue[m.promotion];
    }
    
    // Killer moves
    if (state.isKiller(m, ply)) return 13000;
    
    // Counter moves
    if (state.isCountermove(pos, m, ply)) return 12000;
    
    // History
    Move* prevMove = (ply > 0) ? &state.previousMoves[ply - 1] : nullptr;
    return state.history->getQuietScore(pos, m, prevMove);
}

void pickNextMove(MoveList& moves, int current,
                  const Position& pos, SearchState& state, int ply) {
    if (current >= moves.size()) return;
    
    int best = current;
    int bestScore = scoreMove(pos, state, moves[current], ply);
    
    for (int i = current + 1; i < moves.size(); i++) {
        int score = scoreMove(pos, state, moves[i], ply);
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    
    if (best != current) {
        moves.swapMoves(current, best);
    }
}

/* Quiescence search - OPTIMIZED */
int quiescence(Position& pos, SearchState& state, int ply, int alpha, int beta) {
    state.nodesSearched++;
    
    if (ply >= MAX_PLY - 1) return evaluate(pos);
    
    if (g_allocatedTime > 0 && (g_stopSearch.load(std::memory_order_relaxed) || checkTime())) {
        return 0;
    }

    int standPat = evaluate(pos);
    
    if (standPat >= beta) return beta;
    
    // Delta pruning - MORE AGGRESSIVE
    if (standPat < alpha - 1200) return alpha;
    
    if (standPat > alpha) alpha = standPat;

    MoveList moves;
    MoveGenerator::generateCaptures(pos, moves);

    for (int idx = 0; idx < moves.size(); idx++) {
        pickNextMove(moves, idx, pos, state, ply);
        const Move& m = moves[idx];

        // SEE pruning - MORE AGGRESSIVE
        if ((m.flags & CAPTURE) && see(pos, m) < -100) continue;
        
        // Futility in qsearch
        if (m.flags & CAPTURE) {
            Piece victim = pos.board[m.to];
            if (victim != NO_PIECE && standPat + pieceValue[victim] + 300 < alpha) {
                continue;
            }
        }

        Undo undo;
        Color us = pos.sideToMove;
        pos.makeMove(m, undo);
        
        if (pos.isKingInCheck(us)) {
            pos.undoMove(m, undo);
            continue;
        }

        int score = -quiescence(pos, state, ply + 1, -beta, -alpha);
        pos.undoMove(m, undo);

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    
    return alpha;
}

/* Main negamax search - FULLY OPTIMIZED */
int negamax(Position& pos, SearchState& state, int depth, int ply,
            int alpha, int beta, bool allowNull) {
    
    const int alphaOrig = alpha;
    const bool pvNode = (beta - alpha > 1);
    const bool rootNode = (ply == 0);
    
    state.nodesSearched++;

    // Reset our PV length at entry. This is critical for PV correctness:
    // when the parent later calls updatePV(m, parent_ply), it copies
    // pvTable[ply][0..pvLength[ply]-1] into the parent's PV. If we
    // returned from this node via TT cutoff, qsearch, or repetition
    // without ever writing here, pvLength[ply] must be 0 -- otherwise
    // we'd hand the parent stale moves from a previous sibling that
    // searched the same `ply` slot. The previous code did not reset
    // here, which produced PV outputs containing pseudo-random moves
    // from unrelated subtrees (knight-from-empty-square, etc.).
    state.pvLength[ply] = 0;

    if (g_stopSearch.load(std::memory_order_relaxed)) return 0;
    if (ply >= MAX_PLY - 1) return evaluate(pos);

    if (!rootNode) {
        if (pos.isRepetition() || pos.halfmoveClock >= 100) return 0;
        
        if (state.threadID > 0) {
            if (g_stopSearch.load(std::memory_order_relaxed)) return 0;
        } else {
            if (g_allocatedTime > 0 && checkTime()) return 0;
        }
    }

    // Mate distance pruning
    alpha = std::max(alpha, -MATE + ply);
    beta = std::min(beta, MATE - ply - 1);
    if (alpha >= beta) return alpha;

    // TT probe with cache layer
    int16_t ttScore;
    Move ttMove = nullMove();
    int8_t ttDepth;
    uint8_t ttFlag;
    bool ttHit = false;
    
    TTCacheEntry cacheEntry;
    if (state.ttCache.probe(pos.zobristKey, cacheEntry)) {
        ttHit = true;
        ttScore = cacheEntry.score;
        ttMove = cacheEntry.move;
        ttDepth = cacheEntry.depth;
        ttFlag = cacheEntry.flag;
    } else {
        ttHit = globalTT->probe(pos.zobristKey, ttScore, ttMove, ttDepth, ttFlag, ply);
        if (ttHit) {
            cacheEntry = TTCacheEntry(pos.zobristKey, ttScore, ttMove, ttDepth, ttFlag);
            state.ttCache.store(cacheEntry);
        }
    }

    // TT cutoff - AGGRESSIVE
    if (ttHit && !rootNode) {
        if (ttDepth >= depth || (!pvNode && ttDepth >= depth - 2)) {
            if (ttFlag == TT_EXACT) return ttScore;
            if (ttFlag == TT_ALPHA && ttScore <= alpha) return alpha;
            if (ttFlag == TT_BETA && ttScore >= beta) return beta;
        }
    }

    if (depth <= 0) return quiescence(pos, state, ply, alpha, beta);

    bool inCheck = pos.isKingInCheck(pos.sideToMove);
    int staticEval = inCheck ? -MATE + ply : evaluate(pos);

    bool improving = false;
    if (!inCheck && ply >= 2) {
        improving = staticEval > state.evalStack[ply - 2];
    }
    state.evalStack[ply] = staticEval;

    // Razoring - MORE AGGRESSIVE
    if (!pvNode && !inCheck && depth <= 3 && allowNull) {
        int razorMargin = 500 + 400 * depth;
        if (staticEval + razorMargin < alpha) {
            int razorScore = quiescence(pos, state, ply, alpha - razorMargin, alpha - razorMargin + 1);
            if (razorScore < alpha - razorMargin) return alpha;
        }
    }

    // Reverse futility pruning - OPTIMIZED
    if (!pvNode && !inCheck && depth <= 8 && allowNull) {
        int margin = improving ? 80 : 100;
        if (depth >= 7) margin += 20;
        margin *= depth;
        if (staticEval - margin >= beta) {
            return staticEval - margin;
        }
    }

    // Null move pruning - OPTIMIZED
    if (allowNull && !pvNode && !inCheck && depth >= 2 &&
        hasNonPawnMaterial(pos, pos.sideToMove) && staticEval >= beta) {
        
        // OPTIMIZED: R = 3 + depth/5
        int R = 3 + depth / 5;
        if (staticEval - beta > 300) R++;
        if (!improving) R--;
        R = std::max(2, R);

        Undo undo;
        pos.makeNullMove(undo);
        int nullScore = -negamax(pos, state, depth - 1 - R, ply + 1, -beta, -beta + 1, false);
        pos.undoNullMove(undo);

        if (nullScore >= beta) {
            if (nullScore >= MATE - 1000) return beta;
            return nullScore;
        }
    }

    // Probcut
    if (!pvNode && depth >= 5 && beta < MATE - 100 && allowNull) {
        int rbeta = beta + 200;
        MoveList probMoves;
        MoveGenerator::generateCaptures(pos, probMoves);

        int probcutTries = 0;
        for (int i = 0; i < probMoves.size() && probcutTries < 3; i++) {
            const Move& m = probMoves[i];
            if (see(pos, m) < 100) continue;

            Undo undo;
            Color us = pos.sideToMove;
            pos.makeMove(m, undo);
            
            if (pos.isKingInCheck(us)) {
                pos.undoMove(m, undo);
                continue;
            }

            probcutTries++;
            int score = -negamax(pos, state, depth - 4, ply + 1, -rbeta, -rbeta + 1, false);
            pos.undoMove(m, undo);
            
            if (score >= rbeta) return score;
        }
    }

    // IID
    if (depth >= 6 && !ttHit && pvNode) {
        negamax(pos, state, depth - 2, ply, alpha, beta, true);
        
        if (state.ttCache.probe(pos.zobristKey, cacheEntry)) {
            ttMove = cacheEntry.move;
        } else {
            globalTT->probe(pos.zobristKey, ttScore, ttMove, ttDepth, ttFlag, ply);
        }
    }

    MoveList moves;
    MoveGenerator::generateAllMoves(pos, moves);

    // ============================================================
    // TT-MOVE VALIDATION (two-layer defense against TT corruption)
    // ------------------------------------------------------------
    // Layer 1 (in tt_atomic.h): lockless XOR-checksum read rejects
    //   torn reads on the 16-byte TTEntry.
    // Layer 2 (here): even after a verified TT read, hash collisions
    //   (two different positions mapping to the same 48-bit truncated
    //   zobrist) can produce a `Move` struct that decodes cleanly but
    //   refers to a piece/square layout that doesn't exist in THIS
    //   position. We reject those before they reach makeMove.
    //
    // isPseudoLegal() also implicitly subsumes the old membership-in-
    // generated-list check (any move accepted by isPseudoLegal that
    // doesn't appear in `moves` would indicate a movegen bug, which
    // we'd want to find and fix anyway).
    // ============================================================
    if (!ttMove.isNull()) {
        if (!pos.isPseudoLegal(ttMove)) {
            // Corrupt TT move -- discard it. Search proceeds normally
            // with full movegen-generated ordering.
            ttMove = nullMove();
        } else {
            // The TT move is pseudo-legal. Find it in the generated move
            // list and swap to the front. If it's not in the list, it
            // means our movegen disagrees with isPseudoLegal -- treat as
            // a bug-hunting opportunity in debug, otherwise just drop it.
            int ttMoveIndex = -1;
            for (int i = 0; i < moves.size(); i++) {
                if (moves[i] == ttMove) {
                    ttMoveIndex = i;
                    break;
                }
            }
            if (ttMoveIndex >= 0) {
                moves.swapMoves(0, ttMoveIndex);
            } else {
                // isPseudoLegal accepted it but movegen didn't produce it.
                // In a correct engine these should be equivalent for
                // chess960-disabled, orthodox-only play. Drop the move
                // and let movegen drive the ordering.
                #ifndef NDEBUG
                std::cerr << "WARNING: TT move passed isPseudoLegal but is not in generated moves: "
                          << squareToNotation(ttMove.from) << "->" << squareToNotation(ttMove.to)
                          << " flags=" << (int)ttMove.flags << std::endl;
                #endif
                ttMove = nullMove();
            }
        }
    }

    bool foundLegal = false;
    bool firstMove = true;
    int bestScore = -INF;
    Move bestMove = nullMove();
    int moveCount = 0;

    for (int idx = 0; idx < moves.size(); idx++) {
        if (g_stopSearch.load(std::memory_order_relaxed)) break;
        
        if (idx > 0 || ttMove.isNull()) {
            pickNextMove(moves, idx, pos, state, ply);
        }

        const Move& m = moves[idx];
        Undo undo;
        Color us = pos.sideToMove;
        
        pos.makeMove(m, undo);

        if (pos.isKingInCheck(us)) {
            pos.undoMove(m, undo);
            continue;
        }

        foundLegal = true;
        moveCount++;

        bool isTactical = (m.flags & CAPTURE) || (m.flags & PROMOTION);
        bool givesCheck = pos.isKingInCheck(pos.sideToMove);

        // Futility pruning - OPTIMIZED
        if (!pvNode && !inCheck && !givesCheck && !isTactical &&
            depth <= 5 && moveCount >= 8) {
            
            int margin = 100 + 50 * depth * depth;
            if (staticEval + margin < alpha) {
                pos.undoMove(m, undo);
                continue;
            }
        }

        // Late Move Pruning - OPTIMIZED
        if (!pvNode && !inCheck && !givesCheck && !isTactical) {
            int lmpLimit = improving ? (3 + depth * depth / 2) : (2 + depth * depth / 3);
            
            if (moveCount > lmpLimit) {
                pos.undoMove(m, undo);
                continue;
            }
        }

        // History pruning - OPTIMIZED
        if (!pvNode && !inCheck && !givesCheck && !isTactical && depth <= 4) {
            int histThreshold = -1500 - 200 * depth;
            
            if (moveCount > 8) {
                Move* prevMove = (ply > 0) ? &state.previousMoves[ply - 1] : nullptr;
                int histScore = state.history->getQuietScore(pos, m, prevMove);
                
                if (histScore < histThreshold) {
                    pos.undoMove(m, undo);
                    continue;
                }
            }
        }

        int extension = 0;
        if (givesCheck && depth < 20) extension = 1;

        state.previousMoves[ply] = m;

        int score;
        bool canReduce = !firstMove && moveCount > 2 && depth >= 3 &&
                        !isTactical && !givesCheck && !inCheck;

        if (firstMove) {
            score = -negamax(pos, state, depth - 1 + extension, ply + 1, -beta, -alpha, true);
            firstMove = false;
        } else if (canReduce) {
            int reduction = LMR_TABLE[std::min(depth, 63)][std::min(moveCount, 63)];
            
            if (!pvNode) reduction++;
            if (!improving) reduction++;
            if (state.isKiller(m, ply)) reduction--;
            if (givesCheck) reduction--;
            if (depth <= 10) reduction--;
            
            reduction = std::max(0, std::min(reduction, depth - 2));

            score = -negamax(pos, state, depth - 1 - reduction + extension, ply + 1,
                            -alpha - 1, -alpha, true);
            
            if (score > alpha) {
                score = -negamax(pos, state, depth - 1 + extension, ply + 1,
                                -alpha - 1, -alpha, true);
                
                if (score > alpha && score < beta) {
                    score = -negamax(pos, state, depth - 1 + extension, ply + 1,
                                    -beta, -alpha, true);
                }
            }
        } else {
            score = -negamax(pos, state, depth - 1 + extension, ply + 1,
                            -alpha - 1, -alpha, true);
            
            if (score > alpha && score < beta) {
                score = -negamax(pos, state, depth - 1 + extension, ply + 1,
                                -beta, -alpha, true);
            }
        }

        pos.undoMove(m, undo);

        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
        }
        
        if (score > alpha) {
            alpha = score;

            // Only record PV at PV nodes. Scout (null-window) nodes
            // explore with beta = alpha + 1, so an "alpha improvement"
            // there only means "this move passes the scout" -- not
            // "this is a true principal variation move." Writing such
            // moves into pvTable would corrupt the displayed PV with
            // moves whose scores were never properly verified.
            if (pvNode) {
                state.updatePV(m, ply);
            }

            if (ply == 0) {
                g_bestMove.store(m, std::memory_order_relaxed);
            }
        }

        if (alpha >= beta) {
            if (!isTactical) {
                state.updateKillers(m, ply);
                state.updateHistory(pos, m, depth, ply);
                
                if (ply > 0) {
                    state.updateCountermove(pos, state.previousMoves[ply - 1], m);
                }
            }
            break;
        }
    }

    if (!foundLegal) {
        if (inCheck) return -MATE + ply;
        else return 0;
    }

    uint8_t flag = TT_ALPHA;
    if (bestScore >= beta) flag = TT_BETA;
    else if (bestScore > alphaOrig) flag = TT_EXACT;
    
    globalTT->store(pos.zobristKey, bestScore, bestMove, depth, flag, ply);

    return bestScore;
}

/* Iterative deepening with ASPIRATION WINDOWS */
Move iterativeDeepeningST(Position& pos, int maxDepth, SearchState& state) {
    Position root = pos;
    Move lastCompletedBest = nullMove();
    int prevScore = 0;

    for (int depth = 1; depth <= maxDepth; depth++) {
        if (g_stopSearch.load(std::memory_order_relaxed) ||
            g_timeUp.load(std::memory_order_relaxed)) {
            break;
        }

        pos = root;
        int score;
        
        // Aspiration windows for depth >= 5
        if (depth >= 5) {
            int delta = 25;
            int alpha = std::max(-INF, prevScore - delta);
            int beta = std::min(INF, prevScore + delta);
            
            int failCount = 0;
            while (true) {
                score = negamax(pos, state, depth, 0, alpha, beta, true);
                
                if (g_stopSearch.load(std::memory_order_relaxed)) break;
                
                if (score <= alpha) {
                    alpha = std::max(-INF, alpha - delta);
                    delta *= 2;
                    failCount++;
                } else if (score >= beta) {
                    beta = std::min(INF, beta + delta);
                    delta *= 2;
                    failCount++;
                } else {
                    break;
                }
                
                if (failCount > 5) {
                    alpha = -INF;
                    beta = INF;
                }
            }
        } else {
            score = negamax(pos, state, depth, 0, -INF, INF, true);
        }
        
        pos = root;

        if (g_stopSearch.load(std::memory_order_relaxed) ||
            g_timeUp.load(std::memory_order_relaxed)) {
            break;
        }

        // Extract best move from TT for `bestmove` UCI output.
        // CRITICAL: validate it's pseudo-legal in the root position before
        // accepting it. A torn TT read or hash collision could otherwise
        // make us emit `bestmove a1a1 (flags=12)` to the GUI.
        int16_t ttScore;
        Move ttMove = nullMove();
        int8_t ttDepth;
        uint8_t ttFlag;

        if (globalTT->probe(root.zobristKey, ttScore, ttMove, ttDepth, ttFlag, 0) &&
            !ttMove.isNull() && root.isPseudoLegal(ttMove)) {

            lastCompletedBest = ttMove;
            prevScore = score;
            g_bestMove.store(ttMove, std::memory_order_relaxed);
        }
        
        int whiteScore = (root.sideToMove == WHITE) ? prevScore : -prevScore;
        
        std::cout << "info depth " << depth
                  << " score cp " << whiteScore
                  << " nodes " << state.nodesSearched
                  << " pv ";
        
        // Print full PV with per-move validation. We replay each move
        // on a scratch copy of root and verify it's pseudo-legal before
        // emitting it. This protects against any residual bug in pv-
        // Table maintenance (e.g. an LMR scout writing junk) -- a bad
        // PV move stops emission rather than corrupting the GUI display
        // or crashing the engine via the new makeMove contract checks.
        {
            Position pvPos = root;
            for (int i = 0; i < state.pvLength[0]; i++) {
                Move pvMove = state.pvTable[0][i];
                if (pvMove.isNull() || !pvPos.isPseudoLegal(pvMove)) {
                    break;
                }
                std::cout << UCI::moveToUCI(pvMove) << " ";

                Undo undo;
                Color us = pvPos.sideToMove;
                pvPos.makeMove(pvMove, undo);
                // Reject if the move left our own king in check (truly
                // illegal, not just pseudo-illegal). If it did, the PV
                // was corrupted -- stop here. We don't undo because we
                // discarded pvPos anyway.
                if (pvPos.isKingInCheck(us)) {
                    break;
                }
            }
        }
        std::cout << std::endl;
    }

    return g_bestMove.load(std::memory_order_relaxed);
}

Move iterativeDeepening(Position& pos, int maxDepth) {
    g_timeUp.store(false, std::memory_order_relaxed);
    g_stopSearch.store(false, std::memory_order_relaxed);
    g_bestMove.store(nullMove(), std::memory_order_relaxed);

    if (globalThreadPool && globalThreadPool->getNumThreads() > 1) {
        return globalThreadPool->search(pos, maxDepth);
    }

    SearchState state;
    return iterativeDeepeningST(pos, maxDepth, state);
}

bool hasNonPawnMaterial(const Position& pos, Color side) {
    return (pos.pieces[side][KNIGHT] | pos.pieces[side][BISHOP] |
            pos.pieces[side][ROOK] | pos.pieces[side][QUEEN]) != 0;
}
