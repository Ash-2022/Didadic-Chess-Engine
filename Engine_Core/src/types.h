#ifndef TYPES_H
#define TYPES_H

#include <cstdint>
#include <string>

#define FILE_FROM_SQ(sq) (char)('a' + (sq % 8))
#define RANK_FROM_SQ(sq) (char)('1' + (sq / 8))

using U64 = std::uint64_t;

enum Color : int {
    WHITE = 0,
    BLACK,
    NO_COLOR
};

enum Piece : int {
    PAWN = 0,
    ROOK,
    KNIGHT,
    BISHOP,
    QUEEN,
    KING,
    NO_PIECE
};

enum Square : int {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    NO_SQUARE
};

enum CastlingRights : int {
    WK = 1 << 0,
    WQ = 1 << 1,
    BK = 1 << 2,
    BQ = 1 << 3
};

enum MoveFlags : uint8_t {
    QUIET        = 0,
    CAPTURE      = 1 << 0,
    EN_PASSANT   = 1 << 1,
    CASTLING     = 1 << 2,
    PROMOTION    = 1 << 3,
    PAWN_DOUBLE  = 1 << 4
};

inline MoveFlags operator|(MoveFlags a, MoveFlags b) {
    return static_cast<MoveFlags>(static_cast<int>(a) | static_cast<int>(b));
}

inline MoveFlags operator&(MoveFlags a, MoveFlags b) {
    return static_cast<MoveFlags>(static_cast<int>(a) & static_cast<int>(b));
}

inline std::string squareToNotation(Square sq) {
    char file = 'a' + (sq % 8);
    char rank = '1' + (sq / 8);
    return {file, rank};
}

inline Square mirrorSquare(Square sq) {
    return Square(sq ^ 56);
}

struct Move {
    Square from      = NO_SQUARE;
    Square to        = NO_SQUARE;
    MoveFlags flags  = QUIET;
    Piece promotion  = NO_PIECE;

    bool isNull() const { return from == NO_SQUARE; }
};

inline Move nullMove() {
    return Move{NO_SQUARE, NO_SQUARE, QUIET, NO_PIECE};
}

inline bool operator==(const Move& a, const Move& b) {
    return a.from == b.from && a.to == b.to &&
           a.flags == b.flags && a.promotion == b.promotion;
}

inline bool operator!=(const Move& a, const Move& b) {
    return !(a == b);
}

struct Undo {
    Piece moved     = NO_PIECE;
    Piece captured  = NO_PIECE;
    Square ep       = NO_SQUARE;
    int castling    = 0;
    int halfmove    = 0;
    U64 zobristKey  = 0;
};

/* =========================
   STACK-ALLOCATED MOVE LIST
   Eliminates millions of heap allocations per search
   ========================= */
struct MoveList {
    Move moves[256];
    int count = 0;

    void add(Square from, Square to, MoveFlags flags, Piece promo = NO_PIECE) {
        moves[count++] = {from, to, flags, promo};
    }
    int size() const { return count; }
    Move& operator[](int i) { return moves[i]; }
    const Move& operator[](int i) const { return moves[i]; }
    void clear() { count = 0; }
    void swapMoves(int i, int j) {
        Move tmp = moves[i]; moves[i] = moves[j]; moves[j] = tmp;
    }
};

enum TTFlag : uint8_t {
    TT_EMPTY = 0,
    TT_EXACT = 1,
    TT_ALPHA = 2,
    TT_BETA  = 3
};

#endif // TYPES_H
