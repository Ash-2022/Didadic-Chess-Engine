#include "evaluation.h"
#include "attacks.h"
#include <algorithm>

/* =========================
   CONSTANT TABLES
   ========================= */

constexpr U64 FILE_MASKS[8] = {
    0x0101010101010101ULL, 0x0202020202020202ULL,
    0x0404040404040404ULL, 0x0808080808080808ULL,
    0x1010101010101010ULL, 0x2020202020202020ULL,
    0x4040404040404040ULL, 0x8080808080808080ULL
};

constexpr U64 ADJ_FILE_MASKS[8] = {
    FILE_MASKS[1],
    FILE_MASKS[0] | FILE_MASKS[2],
    FILE_MASKS[1] | FILE_MASKS[3],
    FILE_MASKS[2] | FILE_MASKS[4],
    FILE_MASKS[3] | FILE_MASKS[5],
    FILE_MASKS[4] | FILE_MASKS[6],
    FILE_MASKS[5] | FILE_MASKS[7],
    FILE_MASKS[6]
};

/* =========================
   EVALUATION PARAMETERS
   ========================= */

// Material values (middlegame / endgame)
const int PAWN_MG = 100;
const int PAWN_EG = 120;
const int KNIGHT_MG = 320;
const int KNIGHT_EG = 330;
const int BISHOP_MG = 330;
const int BISHOP_EG = 340;
const int ROOK_MG = 500;
const int ROOK_EG = 550;
const int QUEEN_MG = 900;
const int QUEEN_EG = 1000;

// Positional bonuses
const int BISHOP_PAIR_BONUS = 50;
const int ROOK_OPEN_FILE_BONUS = 20;
const int ROOK_SEMI_OPEN_FILE_BONUS = 10;

// Pawn structure penalties
const int DOUBLED_PAWN_PENALTY = 10;
const int ISOLATED_PAWN_PENALTY = 15;

// Passed pawn bonuses (by rank for white)
const int PASSED_PAWN_BONUS[8] = {0, 10, 15, 25, 40, 60, 90, 0};

// King safety
const int PAWN_SHIELD_BONUS = 10;
const int OPEN_FILE_NEAR_KING_PENALTY = 25;

// Mobility (per square)
const int KNIGHT_MOBILITY = 4;
const int BISHOP_MOBILITY = 3;
const int ROOK_MOBILITY = 2;
const int QUEEN_MOBILITY = 1;

/* =========================
   KING SAFETY PARAMETERS
   ========================= */

// King zones (initialized in initEvaluation)
U64 kingZone[64];

// Attack weights for different pieces
const int ATTACK_WEIGHT[6] = {
    0,   // Pawn
    5,   // Rook
    2,   // Knight
    2,   // Bishop
    4,   // Queen
    0    // King
};

// Attack table (reduced from Stockfish values)
const int SAFETY_TABLE[100] = {
    0, 0, 1, 2, 3, 5, 7, 9, 12, 15,
    18, 22, 26, 30, 35, 39, 44, 50, 56, 62,
    68, 75, 82, 85, 89, 97, 105, 113, 122, 131,
    140, 150, 160, 170, 180, 190, 200, 210, 220, 230,
    240, 250, 260, 270, 280, 290, 300, 300, 300, 300,
    300, 300, 300, 300, 300, 300, 300, 300, 300, 300,
    300, 300, 300, 300, 300, 300, 300, 300, 300, 300,
    300, 300, 300, 300, 300, 300, 300, 300, 300, 300,
    300, 300, 300, 300, 300, 300, 300, 300, 300, 300,
    300, 300, 300, 300, 300, 300, 300, 300, 300, 300
};

/* =========================
   INITIALIZATION
   ========================= */

void initEvaluation() {
    // Initialize king zones
    for (int sq = 0; sq < 64; sq++) {
        int rank = sq / 8;
        int file = sq % 8;
        
        kingZone[sq] = 0;
        
        // 3x3 square around king
        for (int dr = -1; dr <= 1; dr++) {
            for (int df = -1; df <= 1; df++) {
                int r = rank + dr;
                int f = file + df;
                if (r >= 0 && r < 8 && f >= 0 && f < 8) {
                    kingZone[sq] |= 1ULL << (r * 8 + f);
                }
            }
        }
        
        // Add squares in front of king (for white perspective)
        for (int r = rank + 1; r < 8 && r < rank + 3; r++) {
            for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); f++) {
                kingZone[sq] |= 1ULL << (r * 8 + f);
            }
        }
    }
}

/* =========================
   HELPER FUNCTIONS
   ========================= */

inline int calculatePhase(const Position& pos) {
    int phase = 0;
    phase += __builtin_popcountll(pos.pieces[WHITE][KNIGHT] | pos.pieces[BLACK][KNIGHT]);
    phase += __builtin_popcountll(pos.pieces[WHITE][BISHOP] | pos.pieces[BLACK][BISHOP]);
    phase += __builtin_popcountll(pos.pieces[WHITE][ROOK]   | pos.pieces[BLACK][ROOK]) * 2;
    phase += __builtin_popcountll(pos.pieces[WHITE][QUEEN]  | pos.pieces[BLACK][QUEEN]) * 4;
    return std::min(phase, 24);
}

inline bool isOpenFile(const Position& pos, int file) {
    return pos.pawnFileCount[WHITE][file] == 0 &&
           pos.pawnFileCount[BLACK][file] == 0;
}

inline bool isSemiOpenFile(const Position& pos, Color c, int file) {
    return pos.pawnFileCount[c][file] == 0;
}

/* =========================
   PAWN STRUCTURE EVALUATION
   ========================= */

int evaluatePawnStructure(const Position& pos, Color side)
{
    int score = 0;
    Color enemy = (side == WHITE ? BLACK : WHITE);

    U64 pawns      = pos.pieces[side][PAWN];
    U64 enemyPawns = pos.pieces[enemy][PAWN];

    const uint8_t* pawnCount = pos.pawnFileCount[side];

    while (pawns) {
        int sq = __builtin_ctzll(pawns);
        pawns &= pawns - 1;

        int file = sq & 7;
        int rank = sq >> 3;

        // Doubled pawn
        if (pawnCount[file] > 1)
            score -= DOUBLED_PAWN_PENALTY;

        // Isolated pawn
        if (!(pos.pieces[side][PAWN] & ADJ_FILE_MASKS[file]))
            score -= ISOLATED_PAWN_PENALTY;

        // Passed pawn
        U64 passedMask = FILE_MASKS[file] | ADJ_FILE_MASKS[file];
        if (side == WHITE)
            passedMask &= 0xFFFFFFFFFFFFFF00ULL << (rank * 8);
        else
            passedMask &= 0x00FFFFFFFFFFFFFFULL >> ((7 - rank) * 8);

        if (!(enemyPawns & passedMask)) {
            int passedRank = (side == WHITE) ? rank : (7 - rank);
            score += PASSED_PAWN_BONUS[passedRank];
        }
    }

    return score;
}

/* =========================
   KING SAFETY EVALUATION - OPTIMIZED WITH CAPS
   ========================= */

int evaluateKingSafety(const Position& pos, Color side) {
    Color enemy = (side == WHITE ? BLACK : WHITE);
    
    U64 kingBB = pos.pieces[side][KING];
    if (!kingBB) return 0;
    
    Square kingSq = Square(__builtin_ctzll(kingBB));
    int kingFile = kingSq % 8;
    int kingRank = kingSq / 8;
    
    U64 zone = kingZone[kingSq];
    
    int safety = 0;
    U64 occupied = pos.occupancy[2];
    
    // Count attacks on king zone
    int attackWeight = 0;
    
    // Enemy knight attacks
    U64 knights = pos.pieces[enemy][KNIGHT];
    while (knights) {
        Square sq = Square(__builtin_ctzll(knights));
        knights &= knights - 1;
        
        U64 attacks = Attacks::knightAttacks[sq];
        if (attacks & zone) {
            attackWeight += ATTACK_WEIGHT[KNIGHT];
        }
    }
    
    // Enemy bishop attacks
    U64 bishops = pos.pieces[enemy][BISHOP];
    while (bishops) {
        Square sq = Square(__builtin_ctzll(bishops));
        bishops &= bishops - 1;
        
        U64 attacks = Attacks::bishopAttacks(sq, occupied);
        if (attacks & zone) {
            attackWeight += ATTACK_WEIGHT[BISHOP];
        }
    }
    
    // Enemy rook attacks
    U64 rooks = pos.pieces[enemy][ROOK];
    while (rooks) {
        Square sq = Square(__builtin_ctzll(rooks));
        rooks &= rooks - 1;
        
        U64 attacks = Attacks::rookAttacks(sq, occupied);
        if (attacks & zone) {
            attackWeight += ATTACK_WEIGHT[ROOK];
        }
    }
    
    // Enemy queen attacks
    U64 queens = pos.pieces[enemy][QUEEN];
    while (queens) {
        Square sq = Square(__builtin_ctzll(queens));
        queens &= queens - 1;
        
        U64 attacks = Attacks::queenAttacks(sq, occupied);
        if (attacks & zone) {
            attackWeight += ATTACK_WEIGHT[QUEEN];
        }
    }
    
    // ✅ CAPPED safety penalty - maximum -300cp
    int safetyIndex = std::min(attackWeight, 99);
    int scaledSafety = SAFETY_TABLE[safetyIndex];
    
    int attackers = 0;
    attackers += __builtin_popcountll(pos.pieces[enemy][QUEEN]);
    attackers += __builtin_popcountll(pos.pieces[enemy][ROOK]);
    attackers += __builtin_popcountll(pos.pieces[enemy][BISHOP]);
    attackers += __builtin_popcountll(pos.pieces[enemy][KNIGHT]);

    // Gradual scaling
    if (attackers >= 2) scaledSafety += 15;
    if (attackers >= 3) scaledSafety += 20;
    if (attackers >= 4) scaledSafety += 25;

    // ✅ CAP at -300cp (was unbounded before)
    scaledSafety = std::min(scaledSafety, 300);
    safety -= scaledSafety;

    // Pawn shelter evaluation
    int shelter = 0;
    
    if (side == WHITE && kingRank <= 1) {
        for (int f = std::max(0, kingFile - 1); f <= std::min(7, kingFile + 1); f++) {
            U64 fileMask = FILE_MASKS[f];
            U64 ourPawns = pos.pieces[WHITE][PAWN] & fileMask;
            
            if (ourPawns) {
                int pawnRank = __builtin_ctzll(ourPawns) / 8;
                int distance = pawnRank - kingRank;
                
                if (distance == 1) shelter += 10;
                else if (distance == 2) shelter += 5;
            } else {
                shelter -= 15;
            }
        }
    } else if (side == BLACK && kingRank >= 6) {
        for (int f = std::max(0, kingFile - 1); f <= std::min(7, kingFile + 1); f++) {
            U64 fileMask = FILE_MASKS[f];
            U64 ourPawns = pos.pieces[BLACK][PAWN] & fileMask;
            
            if (ourPawns) {
                int pawnSq = 63 - __builtin_clzll(ourPawns);
                int pawnRank = pawnSq / 8;
                int distance = kingRank - pawnRank;
                
                if (distance == 1) shelter += 10;
                else if (distance == 2) shelter += 5;
            } else {
                shelter -= 15;
            }
        }
    }
    
    // Open files near king penalty
    int openFilePenalty = 0;
    for (int f = std::max(0, kingFile - 1); f <= std::min(7, kingFile + 1); f++) {
        if (isOpenFile(pos, f)) {
            openFilePenalty -= OPEN_FILE_NEAR_KING_PENALTY;
        }
    }

    // King confinement (no flight squares)
    U64 kingMoves = Attacks::kingAttacks[kingSq];
    U64 safeSquares = kingMoves & ~pos.occupancy[side];

    int escapeSquares = __builtin_popcountll(safeSquares);

    if (escapeSquares == 0) safety -= 60;
    else if (escapeSquares == 1) safety -= 40;
    else if (escapeSquares == 2) safety -= 20;
    
    return safety + shelter + openFilePenalty;
}

int evaluateSpace(const Position& pos, Color side) {
    Color enemy = (side == WHITE ? BLACK : WHITE);

    // Central + extended central squares
    U64 spaceMask = 0x00003C3C3C3C0000ULL;

    U64 enemyOccupied = pos.occupancy[enemy];
    U64 denied = 0;

    // Pawns deny space (most important)
    U64 pawns = pos.pieces[side][PAWN];
    while (pawns) {
        Square sq = Square(__builtin_ctzll(pawns));
        pawns &= pawns - 1;
        denied |= Attacks::pawnAttacks[side][sq];
    }

    // Minor pieces deny space
    U64 minors = pos.pieces[side][KNIGHT] | pos.pieces[side][BISHOP];
    while (minors) {
        Square sq = Square(__builtin_ctzll(minors));
        minors &= minors - 1;
        denied |= Attacks::knightAttacks[sq];
    }

    // Count squares enemy *could* want to use but cannot
    int space = __builtin_popcountll((denied & spaceMask) & ~enemyOccupied);
    return space * 8;
}

/* =========================
   PASSED PAWN EVALUATION
   ========================= */

int evaluatePassedPawns(const Position& pos, Color side) {
    Color enemy = (side == WHITE ? BLACK : WHITE);
    int score = 0;
    
    U64 pawns = pos.pieces[side][PAWN];
    U64 enemyPawns = pos.pieces[enemy][PAWN];
    
    while (pawns) {
        Square sq = Square(__builtin_ctzll(pawns));
        pawns &= pawns - 1;
        
        int file = sq % 8;
        int rank = sq / 8;
        
        // Check if passed
        U64 frontMask = 0xFFFFFFFFFFFFFF00ULL << (rank * 8);
        U64 adjacentFiles = 0;
        
        if (file > 0) adjacentFiles |= FILE_MASKS[file - 1];
        if (file < 7) adjacentFiles |= FILE_MASKS[file + 1];
        
        U64 fileMask = FILE_MASKS[file];
        U64 passedMask = (fileMask | adjacentFiles) & frontMask;
        
        if (side == BLACK) {
            passedMask = 0x00FFFFFFFFFFFFFFULL >> ((7 - rank) * 8);
            passedMask &= (fileMask | adjacentFiles);
        }
        
        bool isPassed = !(enemyPawns & passedMask);
        
        if (isPassed) {
            int passedRank = (side == WHITE) ? rank : (7 - rank);
            int bonus = PASSED_PAWN_BONUS[passedRank];

            if (passedRank >= 5)
                bonus += 40;

            if (passedRank >= 6)
                bonus += 60;
            
            // Connected passed pawns bonus
            U64 supportMask = 0;
            if (file > 0) supportMask |= 1ULL << (sq - 1);
            if (file < 7) supportMask |= 1ULL << (sq + 1);
            
            if (pos.pieces[side][PAWN] & supportMask) {
                bonus += 35;
            }
            
            score += bonus;
        }
    }
    
    return score;
}

/* =========================
   PIECE MOBILITY EVALUATION - OPTIMIZED
   ========================= */

int evaluateMobility(const Position& pos, Color side) {
    int score = 0;
    U64 occupied = pos.occupancy[2];
    U64 enemyOrEmpty = ~pos.occupancy[side];
    
    // Knight mobility
    U64 knights = pos.pieces[side][KNIGHT];
    while (knights) {
        Square sq = Square(__builtin_ctzll(knights));
        knights &= knights - 1;
        
        U64 attacks = Attacks::knightAttacks[sq] & enemyOrEmpty;
        int mobility = __builtin_popcountll(attacks);
        score += mobility * KNIGHT_MOBILITY;
    }
    
    // Bishop mobility
    U64 bishops = pos.pieces[side][BISHOP];
    while (bishops) {
        Square sq = Square(__builtin_ctzll(bishops));
        bishops &= bishops - 1;
        
        U64 attacks = Attacks::bishopAttacks(sq, occupied) & enemyOrEmpty;
        int mobility = __builtin_popcountll(attacks);
        score += mobility * BISHOP_MOBILITY;
    }
    
    // Rook mobility
    U64 rooks = pos.pieces[side][ROOK];
    while (rooks) {
        Square sq = Square(__builtin_ctzll(rooks));
        rooks &= rooks - 1;
        
        U64 attacks = Attacks::rookAttacks(sq, occupied) & enemyOrEmpty;
        int mobility = __builtin_popcountll(attacks);
        score += mobility * ROOK_MOBILITY;
    }
    
    // Queen mobility
    U64 queens = pos.pieces[side][QUEEN];
    while (queens) {
        Square sq = Square(__builtin_ctzll(queens));
        queens &= queens - 1;
        
        U64 attacks = Attacks::queenAttacks(sq, occupied) & enemyOrEmpty;
        int mobility = __builtin_popcountll(attacks);
        score += mobility * QUEEN_MOBILITY;
    }

    // ✅ REDUCED suffocation penalty
    if (score < 15) score -= 20;
    if (score < 8) score -= 40;
    if (score < 3) score -= 60;
        
    return score;
}

/* =========================
   PIECE-SPECIFIC BONUSES
   ========================= */

int evaluatePieces(const Position& pos, Color side) {
    int score = 0;
    
    // Bishop pair
    if (__builtin_popcountll(pos.pieces[side][BISHOP]) >= 2) {
        score += BISHOP_PAIR_BONUS;
    }
    
    // Rooks on open/semi-open files
    U64 rooks = pos.pieces[side][ROOK];
    while (rooks) {
        Square sq = Square(__builtin_ctzll(rooks));
        rooks &= rooks - 1;
        int file = sq % 8;
        
        if (isOpenFile(pos, file)) {
            score += ROOK_OPEN_FILE_BONUS;
        } else if (isSemiOpenFile(pos, side, file)) {
            score += ROOK_SEMI_OPEN_FILE_BONUS;
        }
    }

    // Bad bishop penalty (blocked by own pawns)
    U64 bishops = pos.pieces[side][BISHOP];
    while (bishops) {
        Square sq = Square(__builtin_ctzll(bishops));
        bishops &= bishops - 1;

        bool darkSquare = ((sq + (sq >> 3)) & 1);
        U64 pawnMask = pos.pieces[side][PAWN];

        U64 sameColorPawns = darkSquare
            ? pawnMask & 0xAA55AA55AA55AA55ULL
            : pawnMask & 0x55AA55AA55AA55AAULL;

        int blocked = __builtin_popcountll(sameColorPawns);
        score -= blocked * 4;
    }

    // Passive rook / queen penalty
    U64 majors = pos.pieces[side][ROOK] | pos.pieces[side][QUEEN];
    while (majors) {
        Square sq = Square(__builtin_ctzll(majors));
        majors &= majors - 1;

        int rank = sq / 8;
        if ((side == WHITE && rank <= 1) ||
            (side == BLACK && rank >= 6)) {
            score -= 20;
        }
    }

    return score;
}

/* =========================
   THREAT EVALUATION
   ========================= */

int evaluateThreats(const Position& pos, Color side) {
    Color enemy = (side == WHITE ? BLACK : WHITE);
    int score = 0;
    U64 occupied = pos.occupancy[2];
    
    // Find enemy pieces attacked by our pawns
    U64 pawnAttacks = 0;
    U64 ourPawns = pos.pieces[side][PAWN];
    
    while (ourPawns) {
        Square sq = Square(__builtin_ctzll(ourPawns));
        ourPawns &= ourPawns - 1;
        pawnAttacks |= Attacks::pawnAttacks[side][sq];
    }
    
    // Pieces attacked by pawns
    if (pawnAttacks & pos.pieces[enemy][KNIGHT]) score += 50;
    if (pawnAttacks & pos.pieces[enemy][BISHOP]) score += 50;
    if (pawnAttacks & pos.pieces[enemy][ROOK]) score += 75;
    if (pawnAttacks & pos.pieces[enemy][QUEEN]) score += 100;
    
    return score;
}

/* =========================
   MAIN EVALUATION FUNCTION - OPTIMIZED
   ========================= */

int evaluate(const Position& pos) {
    // NNUE path: accumulator is maintained incrementally by make/undo and
    // refreshed at every search root. Falls through to the hand-crafted
    // eval when no network is loaded (or NNUE is disabled via UCI).
    if (NNUE::active()) {
        return NNUE::evaluate(pos);
    }

    // Material + PST (already tracked incrementally)
    int mgScore = pos.mgMaterial[WHITE] - pos.mgMaterial[BLACK];
    int egScore = pos.egMaterial[WHITE] - pos.egMaterial[BLACK];
    
    mgScore += pos.mgPST[WHITE] - pos.mgPST[BLACK];
    egScore += pos.egPST[WHITE] - pos.egPST[BLACK];
    
    // Positional terms
    int mgPos = 0, egPos = 0;

    // Space control
    int space = evaluateSpace(pos, WHITE) - evaluateSpace(pos, BLACK);
    mgPos += space;

    // Pawn structure
    int pawnStruct = evaluatePawnStructure(pos, WHITE) - 
                     evaluatePawnStructure(pos, BLACK);
    mgPos += pawnStruct;
    egPos += pawnStruct * 2;
    
    // Mobility - ✅ SCALED by game phase
    int mobility = evaluateMobility(pos, WHITE) - 
                   evaluateMobility(pos, BLACK);
    
    // Scale mobility by game phase (less important in endgame)
    int scaledMobility = mobility * pos.gamePhase / 24;
    mgPos += scaledMobility;
    egPos += scaledMobility / 3;
    
    // Piece bonuses
    int pieceBonus = evaluatePieces(pos, WHITE) - 
                     evaluatePieces(pos, BLACK);
    mgPos += pieceBonus;
    egPos += pieceBonus;
    
    // Passed pawns
    int passedPawns = evaluatePassedPawns(pos, WHITE) - 
                      evaluatePassedPawns(pos, BLACK);
    mgPos += passedPawns;
    egPos += passedPawns * 2;
    
    // ✅ King safety - only in middlegame
    int kingSafety = 0;
    if (pos.gamePhase > 12) {
        kingSafety = evaluateKingSafety(pos, WHITE) - 
                     evaluateKingSafety(pos, BLACK);
        mgPos += kingSafety;
    }
    
    // Threats
    int threats = evaluateThreats(pos, WHITE) - 
                  evaluateThreats(pos, BLACK);
    mgPos += threats;
    
    // Combine scores
    mgScore += mgPos;
    egScore += egPos;
    
    // Tapered evaluation
    int score = (mgScore * pos.gamePhase + egScore * (24 - pos.gamePhase)) / 24;
    
    // ✅ REDUCED passivity penalty
    int passivityPenalty = 0;
    if (mobility < 20) {
        passivityPenalty = (20 - mobility);
    }

    if (mgPos < -100) {
        passivityPenalty += std::max(0, (-mgPos - 100) / 4);
    }

    score -= passivityPenalty;

    // ✅ Tempo bonus - only in middlegame
    if (pos.gamePhase > 8) {
        score += 10;
    }
    
    return (pos.sideToMove == WHITE) ? score : -score;
}
