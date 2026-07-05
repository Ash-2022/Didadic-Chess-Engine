#ifndef POSITION_H
#define POSITION_H

#include "types.h"
#include "attacks.h"
#include "zobrist.h"
#include "pst_tables.h"
#include "nnue.h"
#include <cassert>
#include <cstring>
#include <algorithm>

#ifndef NDEBUG
#include <iostream>
#endif

struct Position {
    U64 pieces[2][6] = {};
    U64 occupancy[3] = {};

    // MAILBOX: O(1) piece lookup instead of O(12) bitboard scan
    Piece board[64];      // piece type on each square
    Color colorBoard[64]; // color on each square

    Color sideToMove = WHITE;
    int castlingRights = 0;
    Square enPassantSquare = NO_SQUARE;
    int halfmoveClock = 0;
    U64 zobristKey = 0;

    // Repetition detection
    U64 posHistory[512];
    int posHistoryCount = 0;

    uint8_t pawnFileCount[2][8] = {};

    // Incremental evaluation
    int mgMaterial[2] = {};
    int egMaterial[2] = {};
    int mgPST[2] = {};
    int egPST[2] = {};
    int gamePhase = 0;

    // NNUE accumulator (both perspectives), maintained incrementally via
    // addPieceEval/removePieceEval and rebuilt at every search root.
    NNUE::Accumulator acc;

    // Core methods
    void clear();
    void updateOccupancy();
    void rebuildMailbox();
    void setStartPosition();
    void printBoard() const;
    void validateMailboxConsistency() const;

    // O(1) mailbox lookups
    Piece pieceAt(Square sq) const { return board[sq]; }
    Color colorAt(Square sq) const { return colorBoard[sq]; }

    // Repetition detection
    bool isRepetition() const {
        for (int i = posHistoryCount - 4; i >= 0; i -= 2) {
            if (posHistory[i] == zobristKey) return true;
        }
        return false;
    }

    void makeMove(const Move& move, Undo& undo);
    void undoMove(const Move& move, const Undo& undo);
    bool operator==(const Position& other) const;
    bool isSquareAttacked(Square sq, Color by) const;
    U64 computeZobrist(const Position& pos);
    bool setFromFEN(const std::string& fen);
    std::string toFEN() const;
    void validatePosition() const;
    void makeNullMove(Undo& undo);
    void undoNullMove(const Undo& undo);

    // Pseudo-legality check for a candidate move (typically a TT move).
    // Returns true iff the move's mechanics are valid in the CURRENT
    // position (correct piece on `from`, correct side, capture/quiet
    // consistency with mailbox, target reachable for the moving piece,
    // castling/promotion/en-passant invariants).
    //
    // This does NOT check whether the move leaves our king in check --
    // that's the caller's responsibility (search already does this via
    // isKingInCheck(us) right after makeMove).
    //
    // The point of this method is to *cheaply* reject moves coming from
    // TT entries that survived collision/torn-read paths into the search
    // but don't actually fit the current bitboard state. Without this
    // guard, makeMove would corrupt the position when handed such a move.
    bool isPseudoLegal(const Move& move) const;

    void initEval();

    inline void addPieceEval(Color c, Piece p, Square sq) {
        mgMaterial[c] += PIECE_VALUE_MG[p];
        egMaterial[c] += PIECE_VALUE_EG[p];
        mgPST[c] += getPSTValue(p, sq, c, false);
        egPST[c] += getPSTValue(p, sq, c, true);
        if (NNUE::active()) NNUE::addPiece(acc, c, p, sq);
    }

    inline void removePieceEval(Color c, Piece p, Square sq) {
        mgMaterial[c] -= PIECE_VALUE_MG[p];
        egMaterial[c] -= PIECE_VALUE_EG[p];
        mgPST[c] -= getPSTValue(p, sq, c, false);
        egPST[c] -= getPSTValue(p, sq, c, true);
        if (NNUE::active()) NNUE::removePiece(acc, c, p, sq);
    }

    inline bool isKingInCheck(Color side) const {
        U64 kingBB = pieces[side][KING];
        
        // Defensive: handle corrupted position gracefully
        if (!kingBB) {
            #ifndef NDEBUG
            std::cerr << "ERROR: No king for side " << side << std::endl;
            std::cerr << "Zobrist: " << std::hex << zobristKey << std::dec << std::endl;
            #endif
            return false; // Fail-safe in release
        }
        
        Square kingSq = static_cast<Square>(__builtin_ctzll(kingBB));
        Color opponent = (side == WHITE ? BLACK : WHITE);
        return isSquareAttacked(kingSq, opponent);
    }

    inline U64 occupied() const {
        return occupancy[WHITE] | occupancy[BLACK];
    }

    Position() {
        clear();
    }
    
    Position(const Position& other) {
        // Bitboards
        std::memcpy(pieces, other.pieces, sizeof(pieces));
        std::memcpy(occupancy, other.occupancy, sizeof(occupancy));
        std::memcpy(pawnFileCount, other.pawnFileCount, sizeof(pawnFileCount));
        
        // Mailbox
        std::memcpy(board, other.board, sizeof(board));
        std::memcpy(colorBoard, other.colorBoard, sizeof(colorBoard));
        
        // Evaluation
        std::memcpy(mgMaterial, other.mgMaterial, sizeof(mgMaterial));
        std::memcpy(egMaterial, other.egMaterial, sizeof(egMaterial));
        std::memcpy(mgPST, other.mgPST, sizeof(mgPST));
        std::memcpy(egPST, other.egPST, sizeof(egPST));
        gamePhase = other.gamePhase;
        std::memcpy(&acc, &other.acc, sizeof(acc));

        // State
        sideToMove = other.sideToMove;
        castlingRights = other.castlingRights;
        enPassantSquare = other.enPassantSquare;
        halfmoveClock = other.halfmoveClock;
        zobristKey = other.zobristKey;
        
        // History (don't copy - each thread has own)
        posHistoryCount = 0;
        
        #ifndef NDEBUG
        validatePosition();
        #endif
    }

    Position& operator=(const Position& other) {
        if (this != &other) {
            // Proper deep copy of all arrays
            std::memcpy(pieces, other.pieces, sizeof(pieces));
            std::memcpy(occupancy, other.occupancy, sizeof(occupancy));
            std::memcpy(pawnFileCount, other.pawnFileCount, sizeof(pawnFileCount));
            std::memcpy(board, other.board, sizeof(board));  // ✅ Mailbox
            std::memcpy(colorBoard, other.colorBoard, sizeof(colorBoard));  // ✅ Mailbox
            std::memcpy(mgMaterial, other.mgMaterial, sizeof(mgMaterial));
            std::memcpy(egMaterial, other.egMaterial, sizeof(egMaterial));
            std::memcpy(mgPST, other.mgPST, sizeof(mgPST));
            std::memcpy(egPST, other.egPST, sizeof(egPST));
            gamePhase = other.gamePhase;
            std::memcpy(&acc, &other.acc, sizeof(acc));
            sideToMove = other.sideToMove;
            castlingRights = other.castlingRights;
            enPassantSquare = other.enPassantSquare;
            halfmoveClock = other.halfmoveClock;
            zobristKey = other.zobristKey;
            posHistoryCount = 0;
        }
        return *this;
    }
};

inline bool Position::isSquareAttacked(Square sq, Color by) const {
    U64 occAll = occupancy[WHITE] | occupancy[BLACK];

    if (Attacks::pawnAttacks[by ^ 1][sq] & pieces[by][PAWN]) return true;
    if (Attacks::knightAttacks[sq] & pieces[by][KNIGHT]) return true;
    if (Attacks::bishopAttacks(sq, occAll) & (pieces[by][BISHOP] | pieces[by][QUEEN])) return true;
    if (Attacks::rookAttacks(sq, occAll) & (pieces[by][ROOK] | pieces[by][QUEEN])) return true;
    if (Attacks::kingAttacks[sq] & pieces[by][KING]) return true;

    return false;
}

// In position.h - add this after makeMove
#ifndef NDEBUG
inline void Position::validatePosition() const {
    // Check bitboard-mailbox consistency
    for (int sq = 0; sq < 64; sq++) {
        Piece mailboxPiece = board[sq];
        Color mailboxColor = colorBoard[sq];
        
        // Find what bitboards say
        Piece bitboardPiece = NO_PIECE;
        Color bitboardColor = NO_COLOR;
        int pieceCount = 0;
        
        for (int c = 0; c < 2; c++) {
            for (int p = 0; p < 6; p++) {
                if (pieces[c][p] & (1ULL << sq)) {
                    pieceCount++;
                    if (pieceCount > 1) {
                        std::cerr << "\n=== DUPLICATE PIECE IN BITBOARDS ===" << std::endl;
                        std::cerr << "Square " << sq << " has MULTIPLE pieces in bitboards!" << std::endl;
                        printBoard();
                        assert(false);
                    }
                    bitboardPiece = static_cast<Piece>(p);
                    bitboardColor = static_cast<Color>(c);
                }
            }
        }
        
        // Compare
        if (mailboxPiece != bitboardPiece || (mailboxPiece != NO_PIECE && mailboxColor != bitboardColor)) {
            std::cerr << "\n=== VALIDATION FAILED ===" << std::endl;
            std::cerr << "Square " << sq << " (" << char('a' + sq%8) << char('1' + sq/8) << "):" << std::endl;
            std::cerr << "  Mailbox: piece=" << (int)mailboxPiece << " color=" << (int)mailboxColor << std::endl;
            std::cerr << "  Bitboard: piece=" << (int)bitboardPiece << " color=" << (int)bitboardColor << std::endl;
            printBoard();
            assert(false);
        }
    }
}
#else
inline void Position::validatePosition() const {} // No-op in release
#endif

#endif // POSITION_H
