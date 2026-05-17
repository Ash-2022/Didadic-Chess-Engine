#include "pst.h"

/* =========================
   COMPILE-TIME MIRROR
   ========================= */

// Flip rank: 0↔7, 1↔6, etc.
static constexpr int mirror(int sq) {
    return sq ^ 56;
}

/* =========================
   PAWN PST
   ========================= */

const int PawnPSTWhite[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
     0,  0,  0,  0,  0,  0,  0,  0
};

const int PawnPSTBlack[64] = {
    PawnPSTWhite[mirror(0)],  PawnPSTWhite[mirror(1)],  PawnPSTWhite[mirror(2)],  PawnPSTWhite[mirror(3)],
    PawnPSTWhite[mirror(4)],  PawnPSTWhite[mirror(5)],  PawnPSTWhite[mirror(6)],  PawnPSTWhite[mirror(7)],
    PawnPSTWhite[mirror(8)],  PawnPSTWhite[mirror(9)],  PawnPSTWhite[mirror(10)], PawnPSTWhite[mirror(11)],
    PawnPSTWhite[mirror(12)], PawnPSTWhite[mirror(13)], PawnPSTWhite[mirror(14)], PawnPSTWhite[mirror(15)],
    PawnPSTWhite[mirror(16)], PawnPSTWhite[mirror(17)], PawnPSTWhite[mirror(18)], PawnPSTWhite[mirror(19)],
    PawnPSTWhite[mirror(20)], PawnPSTWhite[mirror(21)], PawnPSTWhite[mirror(22)], PawnPSTWhite[mirror(23)],
    PawnPSTWhite[mirror(24)], PawnPSTWhite[mirror(25)], PawnPSTWhite[mirror(26)], PawnPSTWhite[mirror(27)],
    PawnPSTWhite[mirror(28)], PawnPSTWhite[mirror(29)], PawnPSTWhite[mirror(30)], PawnPSTWhite[mirror(31)],
    PawnPSTWhite[mirror(32)], PawnPSTWhite[mirror(33)], PawnPSTWhite[mirror(34)], PawnPSTWhite[mirror(35)],
    PawnPSTWhite[mirror(36)], PawnPSTWhite[mirror(37)], PawnPSTWhite[mirror(38)], PawnPSTWhite[mirror(39)],
    PawnPSTWhite[mirror(40)], PawnPSTWhite[mirror(41)], PawnPSTWhite[mirror(42)], PawnPSTWhite[mirror(43)],
    PawnPSTWhite[mirror(44)], PawnPSTWhite[mirror(45)], PawnPSTWhite[mirror(46)], PawnPSTWhite[mirror(47)],
    PawnPSTWhite[mirror(48)], PawnPSTWhite[mirror(49)], PawnPSTWhite[mirror(50)], PawnPSTWhite[mirror(51)],
    PawnPSTWhite[mirror(52)], PawnPSTWhite[mirror(53)], PawnPSTWhite[mirror(54)], PawnPSTWhite[mirror(55)],
    PawnPSTWhite[mirror(56)], PawnPSTWhite[mirror(57)], PawnPSTWhite[mirror(58)], PawnPSTWhite[mirror(59)],
    PawnPSTWhite[mirror(60)], PawnPSTWhite[mirror(61)], PawnPSTWhite[mirror(62)], PawnPSTWhite[mirror(63)]
};

/* =========================
   KNIGHT PST
   ========================= */

const int KnightPSTWhite[64] = {
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50
};

const int KnightPSTBlack[64] = {
    KnightPSTWhite[mirror(0)],  KnightPSTWhite[mirror(1)],  KnightPSTWhite[mirror(2)],  KnightPSTWhite[mirror(3)],
    KnightPSTWhite[mirror(4)],  KnightPSTWhite[mirror(5)],  KnightPSTWhite[mirror(6)],  KnightPSTWhite[mirror(7)],
    KnightPSTWhite[mirror(8)],  KnightPSTWhite[mirror(9)],  KnightPSTWhite[mirror(10)], KnightPSTWhite[mirror(11)],
    KnightPSTWhite[mirror(12)], KnightPSTWhite[mirror(13)], KnightPSTWhite[mirror(14)], KnightPSTWhite[mirror(15)],
    KnightPSTWhite[mirror(16)], KnightPSTWhite[mirror(17)], KnightPSTWhite[mirror(18)], KnightPSTWhite[mirror(19)],
    KnightPSTWhite[mirror(20)], KnightPSTWhite[mirror(21)], KnightPSTWhite[mirror(22)], KnightPSTWhite[mirror(23)],
    KnightPSTWhite[mirror(24)], KnightPSTWhite[mirror(25)], KnightPSTWhite[mirror(26)], KnightPSTWhite[mirror(27)],
    KnightPSTWhite[mirror(28)], KnightPSTWhite[mirror(29)], KnightPSTWhite[mirror(30)], KnightPSTWhite[mirror(31)],
    KnightPSTWhite[mirror(32)], KnightPSTWhite[mirror(33)], KnightPSTWhite[mirror(34)], KnightPSTWhite[mirror(35)],
    KnightPSTWhite[mirror(36)], KnightPSTWhite[mirror(37)], KnightPSTWhite[mirror(38)], KnightPSTWhite[mirror(39)],
    KnightPSTWhite[mirror(40)], KnightPSTWhite[mirror(41)], KnightPSTWhite[mirror(42)], KnightPSTWhite[mirror(43)],
    KnightPSTWhite[mirror(44)], KnightPSTWhite[mirror(45)], KnightPSTWhite[mirror(46)], KnightPSTWhite[mirror(47)],
    KnightPSTWhite[mirror(48)], KnightPSTWhite[mirror(49)], KnightPSTWhite[mirror(50)], KnightPSTWhite[mirror(51)],
    KnightPSTWhite[mirror(52)], KnightPSTWhite[mirror(53)], KnightPSTWhite[mirror(54)], KnightPSTWhite[mirror(55)],
    KnightPSTWhite[mirror(56)], KnightPSTWhite[mirror(57)], KnightPSTWhite[mirror(58)], KnightPSTWhite[mirror(59)],
    KnightPSTWhite[mirror(60)], KnightPSTWhite[mirror(61)], KnightPSTWhite[mirror(62)], KnightPSTWhite[mirror(63)]
};
