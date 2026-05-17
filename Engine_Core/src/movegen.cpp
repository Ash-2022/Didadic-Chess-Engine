#include "movegen.h"
#include "attacks.h"
#include "types.h"
#include <iostream>
#include <cassert>

void MoveGenerator::printMove(const Move& move, const Position& pos)
{
    auto sqStr = [](Square sq) {
        char f = 'a' + (sq % 8);
        char r = '1' + (sq / 8);
        return std::string() + f + r;
    };

    Piece p = pos.pieceAt(move.from);
    bool capture = move.flags & CAPTURE;

    std::string out;

    if (move.flags & CASTLING) {
        if (move.to == G1 || move.to == G8)
            out = "O-O";
        else
            out = "O-O-O";
        std::cout << out << "\n";
        return;
    }

    if (p != PAWN) {
        static const char pieceChar[] = {'P','R','N','B','Q','K'};
        out += pieceChar[p];
    }

    if (p == PAWN && capture) {
        out += char('a' + (move.from % 8));
    }

    if (capture) out += "x";
    out += sqStr(move.to);

    if (move.flags & PROMOTION) {
        static const char promoChar[] = {'P','R','N','B','Q','K'};
        out += "=";
        out += promoChar[move.promotion];
    }

    std::cout << out << "\n";
}

// ===== MoveList-based generation (hot path) =====

void MoveGenerator::generateKnightMoves(const Position& pos, MoveList& moves)
{
    Color us   = pos.sideToMove;
    Color them = (us == WHITE ? BLACK : WHITE);
    U64 knights = pos.pieces[us][KNIGHT];

    while (knights) {
        Square from = static_cast<Square>(__builtin_ctzll(knights));
        knights &= knights - 1;
        U64 attacks = Attacks::knightAttacks[from] & ~pos.occupancy[us];

        while (attacks) {
            Square to = static_cast<Square>(__builtin_ctzll(attacks));
            attacks &= attacks - 1;
            MoveFlags flags = (pos.occupancy[them] & (1ULL << to)) ? CAPTURE : QUIET;
            moves.add(from, to, flags);
        }
    }
}

void MoveGenerator::generateKingMoves(const Position& pos, MoveList& moves)
{
    Color us   = pos.sideToMove;
    Color them = (us == WHITE ? BLACK : WHITE);
    U64 kingBB = pos.pieces[us][KING];
    assert(kingBB);

    Square from = static_cast<Square>(__builtin_ctzll(kingBB));
    U64 attacks = Attacks::kingAttacks[from] & ~pos.occupancy[us];

    while (attacks) {
        Square to = static_cast<Square>(__builtin_ctzll(attacks));
        attacks &= attacks - 1;
        MoveFlags flags = (pos.occupancy[them] & (1ULL << to)) ? CAPTURE : QUIET;
        moves.add(from, to, flags);
    }

    U64 occupied = pos.occupied();

    if (us == WHITE) {
        if (pos.castlingRights & WK) {
            // ✅ FIX: Verify rook still exists at H1
            if (pos.pieces[WHITE][ROOK] & (1ULL << H1)) {
                U64 betweenMask = (1ULL << F1) | (1ULL << G1);
                if (!(occupied & betweenMask)) {
                    if (!pos.isSquareAttacked(E1, BLACK) &&
                        !pos.isSquareAttacked(F1, BLACK) &&
                        !pos.isSquareAttacked(G1, BLACK)) {
                        moves.add(E1, G1, CASTLING);
                    }
                }
            }
        }
        if (pos.castlingRights & WQ) {
            // ✅ FIX: Verify rook still exists at A1
            if (pos.pieces[WHITE][ROOK] & (1ULL << A1)) {
                U64 betweenMask = (1ULL << B1) | (1ULL << C1) | (1ULL << D1);
                if (!(occupied & betweenMask)) {
                    if (!pos.isSquareAttacked(E1, BLACK) &&
                        !pos.isSquareAttacked(D1, BLACK) &&
                        !pos.isSquareAttacked(C1, BLACK)) {
                        moves.add(E1, C1, CASTLING);
                    }
                }
            }
        }
    } else {
        if (pos.castlingRights & BK) {
            // ✅ FIX: Verify rook still exists at H8
            if (pos.pieces[BLACK][ROOK] & (1ULL << H8)) {
                U64 betweenMask = (1ULL << F8) | (1ULL << G8);
                if (!(occupied & betweenMask)) {
                    if (!pos.isSquareAttacked(E8, WHITE) &&
                        !pos.isSquareAttacked(F8, WHITE) &&
                        !pos.isSquareAttacked(G8, WHITE)) {
                        moves.add(E8, G8, CASTLING);
                    }
                }
            }
        }
        if (pos.castlingRights & BQ) {
            // ✅ FIX: Verify rook still exists at A8
            if (pos.pieces[BLACK][ROOK] & (1ULL << A8)) {
                U64 betweenMask = (1ULL << B8) | (1ULL << C8) | (1ULL << D8);
                if (!(occupied & betweenMask)) {
                    if (!pos.isSquareAttacked(E8, WHITE) &&
                        !pos.isSquareAttacked(D8, WHITE) &&
                        !pos.isSquareAttacked(C8, WHITE)) {
                        moves.add(E8, C8, CASTLING);
                    }
                }
            }
        }
    }
}

void MoveGenerator::generateBishopMoves(const Position& pos, MoveList& moves)
{
    Color us   = pos.sideToMove;
    Color them = (us == WHITE ? BLACK : WHITE);
    U64 bishops = pos.pieces[us][BISHOP];
    U64 occAll  = pos.occupied();

    while (bishops) {
        Square from = static_cast<Square>(__builtin_ctzll(bishops));
        bishops &= bishops - 1;
        U64 attacks = Attacks::bishopAttacks(from, occAll) & ~pos.occupancy[us];

        while (attacks) {
            Square to = static_cast<Square>(__builtin_ctzll(attacks));
            attacks &= attacks - 1;
            MoveFlags flags = (pos.occupancy[them] & (1ULL << to)) ? CAPTURE : QUIET;
            moves.add(from, to, flags);
        }
    }
}

void MoveGenerator::generateRookMoves(const Position& pos, MoveList& moves)
{
    Color us   = pos.sideToMove;
    Color them = (us == WHITE ? BLACK : WHITE);
    U64 rooks = pos.pieces[us][ROOK];
    U64 occAll  = pos.occupied();

    while (rooks) {
        Square from = static_cast<Square>(__builtin_ctzll(rooks));
        rooks &= rooks - 1;
        U64 attacks = Attacks::rookAttacks(from, occAll) & ~pos.occupancy[us];

        while (attacks) {
            Square to = static_cast<Square>(__builtin_ctzll(attacks));
            attacks &= attacks - 1;
            MoveFlags flags = (pos.occupancy[them] & (1ULL << to)) ? CAPTURE : QUIET;
            moves.add(from, to, flags);
        }
    }
}

void MoveGenerator::generateQueenMoves(const Position& pos, MoveList& moves)
{
    Color us   = pos.sideToMove;
    Color them = (us == WHITE ? BLACK : WHITE);
    U64 queens = pos.pieces[us][QUEEN];
    U64 occAll  = pos.occupied();

    while (queens) {
        Square from = static_cast<Square>(__builtin_ctzll(queens));
        queens &= queens - 1;
        U64 attacks = Attacks::queenAttacks(from, occAll) & ~pos.occupancy[us];

        while (attacks) {
            Square to = static_cast<Square>(__builtin_ctzll(attacks));
            attacks &= attacks - 1;
            MoveFlags flags = (pos.occupancy[them] & (1ULL << to)) ? CAPTURE : QUIET;
            moves.add(from, to, flags);
        }
    }
}

void MoveGenerator::generatePawnMoves(const Position& pos, MoveList& moves)
{
    Color us   = pos.sideToMove;
    Color them = (us == WHITE ? BLACK : WHITE);
    U64 pawns   = pos.pieces[us][PAWN];
    U64 occAll  = pos.occupied();

    const int push      = (us == WHITE ? 8 : -8);
    const int startRank = (us == WHITE ? 1 : 6);
    const int promoRank = (us == WHITE ? 6 : 1);

    while (pawns) {
        Square from = static_cast<Square>(__builtin_ctzll(pawns));
        pawns &= pawns - 1;
        int rank = from / 8;

        Square oneStep = static_cast<Square>(from + push);
        if (!(occAll & (1ULL << oneStep))) {
            if (rank == promoRank) {
                for (Piece p : {QUEEN, ROOK, BISHOP, KNIGHT})
                    moves.add(from, oneStep, PROMOTION, p);
            } else {
                moves.add(from, oneStep, QUIET);
                if (rank == startRank) {
                    Square twoStep = static_cast<Square>(from + 2 * push);
                    if (!(occAll & (1ULL << twoStep)))
                        moves.add(from, twoStep, PAWN_DOUBLE);
                }
            }
        }

        U64 captures = Attacks::pawnAttacks[us][from] & pos.occupancy[them];
        while (captures) {
            Square to = static_cast<Square>(__builtin_ctzll(captures));
            captures &= captures - 1;
            if (rank == promoRank) {
                for (Piece p : {QUEEN, ROOK, BISHOP, KNIGHT})
                    moves.add(from, to, CAPTURE | PROMOTION, p);
            } else {
                moves.add(from, to, CAPTURE);
            }
        }

        if (pos.enPassantSquare != NO_SQUARE) {
            if (Attacks::pawnAttacks[us][from] & (1ULL << pos.enPassantSquare))
                moves.add(from, pos.enPassantSquare, EN_PASSANT);
        }
    }
}

void MoveGenerator::generateAllMoves(const Position& pos, MoveList& moves)
{
    moves.clear();
    generatePawnMoves(pos, moves);
    generateKnightMoves(pos, moves);
    generateBishopMoves(pos, moves);
    generateRookMoves(pos, moves);
    generateQueenMoves(pos, moves);
    generateKingMoves(pos, moves);
}

// ===== Capture-only generation for quiescence =====

void MoveGenerator::generatePawnCaptures(const Position& pos, MoveList& moves)
{
    Color us   = pos.sideToMove;
    Color them = (us == WHITE ? BLACK : WHITE);
    U64 pawns  = pos.pieces[us][PAWN];
    U64 occAll = pos.occupied();

    const int push      = (us == WHITE ? 8 : -8);
    const int promoRank = (us == WHITE ? 6 : 1);

    while (pawns) {
        Square from = static_cast<Square>(__builtin_ctzll(pawns));
        pawns &= pawns - 1;
        int rank = from / 8;

        // Promotion pushes (tactical)
        if (rank == promoRank) {
            Square oneStep = static_cast<Square>(from + push);
            if (!(occAll & (1ULL << oneStep))) {
                for (Piece p : {QUEEN, ROOK, BISHOP, KNIGHT})
                    moves.add(from, oneStep, PROMOTION, p);
            }
        }

        U64 captures = Attacks::pawnAttacks[us][from] & pos.occupancy[them];
        while (captures) {
            Square to = static_cast<Square>(__builtin_ctzll(captures));
            captures &= captures - 1;
            if (rank == promoRank) {
                for (Piece p : {QUEEN, ROOK, BISHOP, KNIGHT})
                    moves.add(from, to, CAPTURE | PROMOTION, p);
            } else {
                moves.add(from, to, CAPTURE);
            }
        }

        if (pos.enPassantSquare != NO_SQUARE) {
            if (Attacks::pawnAttacks[us][from] & (1ULL << pos.enPassantSquare))
                moves.add(from, pos.enPassantSquare, EN_PASSANT);
        }
    }
}

void MoveGenerator::generateCaptures(const Position& pos, MoveList& moves)
{
    moves.clear();
    Color us   = pos.sideToMove;
    U64 enemyOcc = pos.occupancy[us == WHITE ? BLACK : WHITE];
    U64 occAll = pos.occupied();

    generatePawnCaptures(pos, moves);

    U64 knights = pos.pieces[us][KNIGHT];
    while (knights) {
        Square from = static_cast<Square>(__builtin_ctzll(knights));
        knights &= knights - 1;
        U64 attacks = Attacks::knightAttacks[from] & enemyOcc;
        while (attacks) {
            Square to = static_cast<Square>(__builtin_ctzll(attacks));
            attacks &= attacks - 1;
            moves.add(from, to, CAPTURE);
        }
    }

    U64 bishops = pos.pieces[us][BISHOP];
    while (bishops) {
        Square from = static_cast<Square>(__builtin_ctzll(bishops));
        bishops &= bishops - 1;
        U64 attacks = Attacks::bishopAttacks(from, occAll) & enemyOcc;
        while (attacks) {
            Square to = static_cast<Square>(__builtin_ctzll(attacks));
            attacks &= attacks - 1;
            moves.add(from, to, CAPTURE);
        }
    }

    U64 rooks = pos.pieces[us][ROOK];
    while (rooks) {
        Square from = static_cast<Square>(__builtin_ctzll(rooks));
        rooks &= rooks - 1;
        U64 attacks = Attacks::rookAttacks(from, occAll) & enemyOcc;
        while (attacks) {
            Square to = static_cast<Square>(__builtin_ctzll(attacks));
            attacks &= attacks - 1;
            moves.add(from, to, CAPTURE);
        }
    }

    U64 queens = pos.pieces[us][QUEEN];
    while (queens) {
        Square from = static_cast<Square>(__builtin_ctzll(queens));
        queens &= queens - 1;
        U64 attacks = Attacks::queenAttacks(from, occAll) & enemyOcc;
        while (attacks) {
            Square to = static_cast<Square>(__builtin_ctzll(attacks));
            attacks &= attacks - 1;
            moves.add(from, to, CAPTURE);
        }
    }

    U64 kingBB = pos.pieces[us][KING];
    if (kingBB) {
        Square from = static_cast<Square>(__builtin_ctzll(kingBB));
        U64 attacks = Attacks::kingAttacks[from] & enemyOcc;
        while (attacks) {
            Square to = static_cast<Square>(__builtin_ctzll(attacks));
            attacks &= attacks - 1;
            moves.add(from, to, CAPTURE);
        }
    }
}

// ===== Legacy vector version for perft/UCI =====
void MoveGenerator::generateAllMoves(const Position& pos, std::vector<Move>& allMoves)
{
    MoveList ml;
    generateAllMoves(pos, ml);
    allMoves.clear();
    allMoves.reserve(ml.count);
    for (int i = 0; i < ml.count; i++)
        allMoves.push_back(ml.moves[i]);
}
