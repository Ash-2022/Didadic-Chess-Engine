#include "position.h"
#include "types.h"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <atomic>
#include <sstream>
#include <string>

void Position::clear() {
    std::memset(pieces, 0, sizeof(pieces));
    std::memset(occupancy, 0, sizeof(occupancy));
    std::memset(pawnFileCount, 0, sizeof(pawnFileCount));
    std::memset(mgMaterial, 0, sizeof(mgMaterial));
    std::memset(egMaterial, 0, sizeof(egMaterial));
    std::memset(mgPST, 0, sizeof(mgPST));
    std::memset(egPST, 0, sizeof(egPST));
    for (int i = 0; i < 64; i++) {
        board[i] = NO_PIECE;
        colorBoard[i] = NO_COLOR;
    }
    sideToMove = WHITE;
    castlingRights = 0;
    enPassantSquare = NO_SQUARE;
    halfmoveClock = 0;
    zobristKey = 0;
    gamePhase = 0;
    posHistoryCount = 0;
    NNUE::resetAccumulator(acc);
}

void Position::rebuildMailbox() {
    for (int sq = 0; sq < 64; sq++) {
        board[sq] = NO_PIECE;
        colorBoard[sq] = NO_COLOR;
    }
    for (int c = 0; c < 2; c++) {
        for (int p = 0; p < 6; p++) {
            U64 bb = pieces[c][p];
            while (bb) {
                int sq = __builtin_ctzll(bb);
                bb &= bb - 1;
                board[sq] = static_cast<Piece>(p);
                colorBoard[sq] = static_cast<Color>(c);
            }
        }
    }
}

void Position::initEval() {
    mgMaterial[WHITE] = mgMaterial[BLACK] = 0;
    egMaterial[WHITE] = egMaterial[BLACK] = 0;
    mgPST[WHITE] = mgPST[BLACK] = 0;
    egPST[WHITE] = egPST[BLACK] = 0;

    gamePhase = 0;
    gamePhase += __builtin_popcountll(pieces[WHITE][KNIGHT] | pieces[BLACK][KNIGHT]);
    gamePhase += __builtin_popcountll(pieces[WHITE][BISHOP] | pieces[BLACK][BISHOP]);
    gamePhase += __builtin_popcountll(pieces[WHITE][ROOK]   | pieces[BLACK][ROOK]) * 2;
    gamePhase += __builtin_popcountll(pieces[WHITE][QUEEN]  | pieces[BLACK][QUEEN]) * 4;
    gamePhase = std::min(gamePhase, 24);

    for (Color c : {WHITE, BLACK}) {
        for (Piece p : {PAWN, ROOK, KNIGHT, BISHOP, QUEEN, KING}) {
            U64 bb = pieces[c][p];
            while (bb) {
                Square sq = Square(__builtin_ctzll(bb));
                bb &= bb - 1;
                addPieceEval(c, p, sq);
            }
        }
    }
}

void Position::updateOccupancy() {
    occupancy[WHITE] = 0ULL;
    occupancy[BLACK] = 0ULL;
    for (int p = 0; p < 6; p++) {
        occupancy[WHITE] |= pieces[WHITE][p];
        occupancy[BLACK] |= pieces[BLACK][p];
    }
    occupancy[2] = occupancy[WHITE] | occupancy[BLACK];
}

void Position::setStartPosition() {
    clear();

    pieces[WHITE][PAWN] = 0x000000000000FF00ULL;
    for (int f = 0; f < 8; ++f) pawnFileCount[WHITE][f] = 1;

    pieces[WHITE][ROOK]   = (1ULL << A1) | (1ULL << H1);
    pieces[WHITE][KNIGHT] = (1ULL << B1) | (1ULL << G1);
    pieces[WHITE][BISHOP] = (1ULL << C1) | (1ULL << F1);
    pieces[WHITE][QUEEN]  = 1ULL << D1;
    pieces[WHITE][KING]   = 1ULL << E1;

    pieces[BLACK][PAWN] = 0x00FF000000000000ULL;
    for (int f = 0; f < 8; ++f) pawnFileCount[BLACK][f] = 1;

    pieces[BLACK][ROOK]   = (1ULL << A8) | (1ULL << H8);
    pieces[BLACK][KNIGHT] = (1ULL << B8) | (1ULL << G8);
    pieces[BLACK][BISHOP] = (1ULL << C8) | (1ULL << F8);
    pieces[BLACK][QUEEN]  = 1ULL << D8;
    pieces[BLACK][KING]   = 1ULL << E8;

    sideToMove = WHITE;
    castlingRights = WK | WQ | BK | BQ;
    enPassantSquare = NO_SQUARE;
    halfmoveClock = 0;

    updateOccupancy();
    rebuildMailbox();
    zobristKey = computeZobrist(*this);
    initEval();
}

bool Position::setFromFEN(const std::string& fen) {
    clear();

    std::istringstream ss(fen);
    std::string board, side, castling, ep, halfmove, fullmove;
    ss >> board >> side >> castling >> ep;
    if (ss >> halfmove >> fullmove)
        halfmoveClock = std::stoi(halfmove);

    int rank = 7, file = 0;
    for (char c : board) {
        if (c == '/') { rank--; file = 0; continue; }
        if (isdigit(c)) { file += c - '0'; continue; }

        Color color = isupper(c) ? WHITE : BLACK;
        char piece = tolower(c);
        Square sq = Square(rank * 8 + file);

        switch (piece) {
            case 'p': pieces[color][PAWN] |= (1ULL << sq); pawnFileCount[color][file]++; break;
            case 'r': pieces[color][ROOK]   |= (1ULL << sq); break;
            case 'n': pieces[color][KNIGHT] |= (1ULL << sq); break;
            case 'b': pieces[color][BISHOP] |= (1ULL << sq); break;
            case 'q': pieces[color][QUEEN]  |= (1ULL << sq); break;
            case 'k': pieces[color][KING]   |= (1ULL << sq); break;
            default: return false;
        }
        file++;
    }

    sideToMove = (side == "w") ? WHITE : BLACK;

    castlingRights = 0;
    for (char c : castling) {
        if (c == 'K') castlingRights |= WK;
        if (c == 'Q') castlingRights |= WQ;
        if (c == 'k') castlingRights |= BK;
        if (c == 'q') castlingRights |= BQ;
    }

    if (ep != "-")
        enPassantSquare = Square((ep[1] - '1') * 8 + (ep[0] - 'a'));

    updateOccupancy();
    rebuildMailbox();
    zobristKey = computeZobrist(*this);
    initEval();
    return true;
}

std::string Position::toFEN() const {
    std::ostringstream fen;

    for (int rank = 7; rank >= 0; rank--) {
        int emptyCount = 0;
        for (int file = 0; file < 8; file++) {
            Square sq = static_cast<Square>(rank * 8 + file);
            Piece p = pieceAt(sq);
            if (p == NO_PIECE) {
                emptyCount++;
            } else {
                if (emptyCount > 0) { fen << emptyCount; emptyCount = 0; }
                Color c = colorAt(sq);
                char ch;
                switch (p) {
                    case PAWN: ch = 'p'; break;
                    case ROOK: ch = 'r'; break;
                    case KNIGHT: ch = 'n'; break;
                    case BISHOP: ch = 'b'; break;
                    case QUEEN: ch = 'q'; break;
                    case KING: ch = 'k'; break;
                    default: ch = '?'; break;
                }
                if (c == WHITE) ch = toupper(ch);
                fen << ch;
            }
        }
        if (emptyCount > 0) fen << emptyCount;
        if (rank > 0) fen << '/';
    }

    fen << (sideToMove == WHITE ? " w " : " b ");

    if (castlingRights == 0) fen << '-';
    else {
        if (castlingRights & WK) fen << 'K';
        if (castlingRights & WQ) fen << 'Q';
        if (castlingRights & BK) fen << 'k';
        if (castlingRights & BQ) fen << 'q';
    }

    fen << ' ';
    if (enPassantSquare == NO_SQUARE) fen << '-';
    else fen << char('a' + enPassantSquare % 8) << char('1' + enPassantSquare / 8);

    fen << ' ' << halfmoveClock << " 1";
    return fen.str();
}

void Position::printBoard() const {
    const char pieceChars[2][6] = {
        {'P','R','N','B','Q','K'},
        {'p','r','n','b','q','k'}
    };

    for (int rank = 7; rank >= 0; rank--) {
        std::cout << rank + 1 << " ";
        for (int file = 0; file < 8; file++) {
            Square sq = static_cast<Square>(rank * 8 + file);
            char ch = '.';
            for (int color = 0; color < 2; color++)
                for (int pType = 0; pType < 6; pType++)
                    if (pieces[color][pType] & (1ULL << sq))
                        ch = pieceChars[color][pType];
            std::cout << ch << " ";
        }
        std::cout << std::endl;
    }
    std::cout << "  a b c d e f g h" << std::endl;
    std::cout << "FEN: " << toFEN() << std::endl;
}

void Position::makeMove(const Move& move, Undo& undo) {
#ifndef NDEBUG
    // Validate position consistency at entry. If this fires, we got here
    // with already-corrupted state from an earlier bug -- not our problem
    // to silently paper over.
    validatePosition();

    if (move.from >= 64 || move.to >= 64) {
        std::cerr << "ILLEGAL MOVE SQUARES: " << (int)move.from << " -> " << (int)move.to << std::endl;
        printBoard();
        assert(false && "makeMove called with out-of-range squares");
    }

    // CONTRACT: caller must have verified pseudo-legality. If this fires,
    // there is a bug in the caller (almost always: a TT move was applied
    // without isPseudoLegal() validation). Do NOT silently return -- that
    // path is what was corrupting the position in earlier versions.
    if (board[move.from] == NO_PIECE) {
        std::cerr << "\n=== makeMove CONTRACT VIOLATION ===" << std::endl;
        std::cerr << "Empty source square: " << squareToNotation(move.from)
                  << " -> " << squareToNotation(move.to)
                  << " flags=" << (int)move.flags << std::endl;
        std::cerr << "This means a TT/move-collision slipped past isPseudoLegal()" << std::endl;
        printBoard();
        assert(false && "makeMove called with no piece on `from`");
    }
    if (colorBoard[move.from] != sideToMove) {
        std::cerr << "\n=== makeMove CONTRACT VIOLATION ===" << std::endl;
        std::cerr << "Wrong color on source: " << squareToNotation(move.from)
                  << " has color " << (int)colorBoard[move.from]
                  << " but side-to-move is " << (int)sideToMove << std::endl;
        printBoard();
        assert(false && "makeMove called with wrong-color piece on `from`");
    }
#else
    // RELEASE BUILD: still refuse to corrupt the position. Caller MUST
    // call isPseudoLegal() first; if they didn't and we got here with
    // garbage, the only safe action is to abort the move cleanly without
    // touching state. We still need to give the caller a balanced undo
    // record so their undoMove() doesn't corrupt anything either.
    //
    // Note: this is a defense-in-depth backstop, not the primary fix.
    // The primary fix is that search.cpp now validates every TT move
    // with isPseudoLegal() before calling makeMove.
    if (board[move.from] == NO_PIECE || colorBoard[move.from] != sideToMove) {
        // Zero-initialize undo so undoMove won't try to do anything.
        // We set `moved = NO_PIECE` and the caller treats this as a
        // skip. The caller is responsible for detecting the failure by
        // checking the return path -- but since this signature returns
        // void, the only contract we can keep is "don't corrupt anything".
        undo.moved = NO_PIECE;
        undo.captured = NO_PIECE;
        undo.ep = enPassantSquare;
        undo.castling = castlingRights;
        undo.halfmove = halfmoveClock;
        undo.zobristKey = zobristKey;
        // CRITICAL: do NOT increment posHistoryCount, do NOT modify any
        // bitboards, mailbox, or zobrist. State is preserved exactly.
        return;
    }
#endif

    posHistory[posHistoryCount++] = zobristKey;

    undo.moved      = board[move.from];
    undo.captured   = NO_PIECE;
    undo.ep         = enPassantSquare;
    undo.castling   = castlingRights;
    undo.halfmove   = halfmoveClock;
    undo.zobristKey = zobristKey;

    Color us   = sideToMove;
    Color them = (us == WHITE ? BLACK : WHITE);

    int fromFile = move.from & 7;
    int toFile   = move.to   & 7;

    U64 fromBB = 1ULL << move.from;
    U64 toBB   = 1ULL << move.to;

    // Pawn file count updates
    if (undo.moved == PAWN) {
        pawnFileCount[us][fromFile]--;
        pawnFileCount[us][toFile]++;
    }

    // CRITICAL: Record captured piece BEFORE any mailbox modifications
    if (move.flags & CAPTURE) {
        Square capSq = move.to;
        if (move.flags & EN_PASSANT)
            capSq = (us == WHITE) ? Square(move.to - 8) : Square(move.to + 8);
        undo.captured = pieceAt(capSq);
        if (undo.captured == PAWN)
            pawnFileCount[them][capSq & 7]--;
    }

    if (move.flags & PROMOTION)
        pawnFileCount[us][toFile]--;

    // Zobrist: remove EP
    if (enPassantSquare != NO_SQUARE)
        zobristKey ^= Zobrist::enPassant[enPassantSquare];
    zobristKey ^= Zobrist::castling[castlingRights];
    zobristKey ^= Zobrist::piece[us][undo.moved][move.from];

    enPassantSquare = NO_SQUARE;

    // === PROMOTION ===
    if (move.flags & PROMOTION) {
        removePieceEval(us, PAWN, move.from);

        if (move.flags & CAPTURE) {
            zobristKey ^= Zobrist::piece[them][undo.captured][move.to];
            pieces[them][undo.captured] &= ~toBB;
            removePieceEval(them, undo.captured, move.to);
            
            // Mailbox will be overwritten by promotion piece - no need to clear
            
            if (undo.captured == KNIGHT || undo.captured == BISHOP) gamePhase--;
            else if (undo.captured == ROOK) gamePhase -= 2;
            else if (undo.captured == QUEEN) gamePhase -= 4;
            gamePhase = std::max(0, gamePhase);
        }
        halfmoveClock = 0;

        pieces[us][PAWN] &= ~fromBB;
        pieces[us][move.promotion] |= toBB;
        addPieceEval(us, move.promotion, move.to);

        // Ã¢Å¡Â¡ MAILBOX: Clear source, set destination to promotion piece
        board[move.from] = NO_PIECE;
        colorBoard[move.from] = NO_COLOR;
        board[move.to] = move.promotion;
        colorBoard[move.to] = us;

        if (move.promotion == KNIGHT || move.promotion == BISHOP) gamePhase++;
        else if (move.promotion == ROOK) gamePhase += 2;
        else if (move.promotion == QUEEN) gamePhase += 4;
        gamePhase = std::min(24, gamePhase);

        zobristKey ^= Zobrist::piece[us][move.promotion][move.to];
        zobristKey ^= Zobrist::castling[castlingRights];
        zobristKey ^= Zobrist::sideToMove;
        sideToMove = them;
        updateOccupancy();
#ifndef NDEBUG
        validatePosition();
#endif
        return;
    }

    // === CASTLING ===
    if (move.flags & CASTLING) {
        removePieceEval(us, KING, move.from);
        pieces[us][KING] &= ~fromBB;
        pieces[us][KING] |= toBB;
        addPieceEval(us, KING, move.to);
        
        // Ã¢Å¡Â¡ MAILBOX: Move king
        board[move.from] = NO_PIECE;
        colorBoard[move.from] = NO_COLOR;
        board[move.to] = KING;
        colorBoard[move.to] = us;
        
        zobristKey ^= Zobrist::piece[us][KING][move.to];

        // Handle rook movement for castling
        Square rookFrom, rookTo;
        if (move.to == G1) { rookFrom = H1; rookTo = F1; }
        else if (move.to == C1) { rookFrom = A1; rookTo = D1; }
        else if (move.to == G8) { rookFrom = H8; rookTo = F8; }
        else { rookFrom = A8; rookTo = D8; }

#ifndef NDEBUG
        // Defensive: castling squares must be empty or have our rook
        if (board[rookFrom] != ROOK || colorBoard[rookFrom] != us) {
            std::cerr << "\n=== CASTLING BUG ===" << std::endl;
            std::cerr << "Expected rook at " << (int)rookFrom << " but found piece=" 
                      << (int)board[rookFrom] << " color=" << (int)colorBoard[rookFrom] << std::endl;
            printBoard();
            assert(false);
        }
        if (board[rookTo] != NO_PIECE) {
            std::cerr << "\n=== CASTLING BUG ===" << std::endl;
            std::cerr << "Rook destination " << (int)rookTo << " is occupied by piece=" 
                      << (int)board[rookTo] << " color=" << (int)colorBoard[rookTo] << std::endl;
            printBoard();
            assert(false);
        }
#endif

        removePieceEval(us, ROOK, rookFrom);
        pieces[us][ROOK] &= ~(1ULL << rookFrom);
        pieces[us][ROOK] |= (1ULL << rookTo);
        addPieceEval(us, ROOK, rookTo);
        
        // Ã¢Å¡Â¡ MAILBOX: Move rook
        board[rookFrom] = NO_PIECE;
        colorBoard[rookFrom] = NO_COLOR;
        board[rookTo] = ROOK;
        colorBoard[rookTo] = us;

        castlingRights &= (us == WHITE) ? ~(WK | WQ) : ~(BK | BQ);
        halfmoveClock++;
        zobristKey ^= Zobrist::castling[castlingRights];
        zobristKey ^= Zobrist::sideToMove;
        sideToMove = them;
        updateOccupancy();
#ifndef NDEBUG
        validatePosition();
#endif
        return;
    }

    // === EN PASSANT ===
    if (move.flags & EN_PASSANT) {
        Square capSq = (us == WHITE) ? Square(move.to - 8) : Square(move.to + 8);
        zobristKey ^= Zobrist::piece[them][PAWN][capSq];
        pieces[them][PAWN] &= ~(1ULL << capSq);
        removePieceEval(them, PAWN, capSq);
        
        // Ã¢Å¡Â¡ MAILBOX: Clear captured pawn
        board[capSq] = NO_PIECE;
        colorBoard[capSq] = NO_COLOR;

        pieces[us][PAWN] &= ~fromBB;
        pieces[us][PAWN] |= toBB;
        removePieceEval(us, PAWN, move.from);
        addPieceEval(us, PAWN, move.to);
        
        // Ã¢Å¡Â¡ MAILBOX: Move pawn
        board[move.from] = NO_PIECE;
        colorBoard[move.from] = NO_COLOR;
        board[move.to] = PAWN;
        colorBoard[move.to] = us;

        zobristKey ^= Zobrist::piece[us][PAWN][move.to];
        halfmoveClock = 0;
        zobristKey ^= Zobrist::castling[castlingRights];
        zobristKey ^= Zobrist::sideToMove;
        sideToMove = them;
        updateOccupancy();
#ifndef NDEBUG
        validatePosition();
#endif
        return;
    }

    // === NORMAL CAPTURE ===
    if (move.flags & CAPTURE) {
        zobristKey ^= Zobrist::piece[them][undo.captured][move.to];
        pieces[them][undo.captured] &= ~toBB;
        removePieceEval(them, undo.captured, move.to);
        
        // Mailbox destination will be overwritten by moving piece
        
        if (undo.captured == KNIGHT || undo.captured == BISHOP) gamePhase--;
        else if (undo.captured == ROOK) gamePhase -= 2;
        else if (undo.captured == QUEEN) gamePhase -= 4;
        gamePhase = std::max(0, gamePhase);
        halfmoveClock = 0;
    } else {
        halfmoveClock = (undo.moved == PAWN) ? 0 : halfmoveClock + 1;
    }

    // === PAWN DOUBLE PUSH ===
    if (move.flags & PAWN_DOUBLE) {
        enPassantSquare = (us == WHITE) ? Square(move.to - 8) : Square(move.to + 8);
        zobristKey ^= Zobrist::enPassant[enPassantSquare];
    }

    // === CASTLING RIGHTS UPDATE ===
    if (undo.moved == KING)
        castlingRights &= (us == WHITE) ? ~(WK | WQ) : ~(BK | BQ);
    else if (undo.moved == ROOK) {
        if (us == WHITE) {
            if (move.from == A1) castlingRights &= ~WQ;
            else if (move.from == H1) castlingRights &= ~WK;
        } else {
            if (move.from == A8) castlingRights &= ~BQ;
            else if (move.from == H8) castlingRights &= ~BK;
        }
    }

    if (move.flags & CAPTURE) {
        if (move.to == A1) castlingRights &= ~WQ;
        else if (move.to == H1) castlingRights &= ~WK;
        else if (move.to == A8) castlingRights &= ~BQ;
        else if (move.to == H8) castlingRights &= ~BK;
    }

    // === NORMAL MOVE ===
    removePieceEval(us, undo.moved, move.from);
    pieces[us][undo.moved] &= ~fromBB;
    pieces[us][undo.moved] |= toBB;
    addPieceEval(us, undo.moved, move.to);
    
    // Ã¢Å¡Â¡ MAILBOX: Normal move (atomically update both squares)
    board[move.from] = NO_PIECE;
    colorBoard[move.from] = NO_COLOR;
    board[move.to] = undo.moved;
    colorBoard[move.to] = us;

    zobristKey ^= Zobrist::piece[us][undo.moved][move.to];
    zobristKey ^= Zobrist::castling[castlingRights];
    zobristKey ^= Zobrist::sideToMove;
    sideToMove = them;
    updateOccupancy();

#ifndef NDEBUG
    // After make/undo, the mailbox and bitboards must be perfectly in sync.
    // If they're not, the make/undo logic itself has a bug -- silently
    // rebuilding the mailbox would mask it. Compare against a fresh rebuild
    // and assert HARD if anything differs.
    Piece expectedBoard[64];
    Color expectedColor[64];
    for (int sq = 0; sq < 64; sq++) {
        expectedBoard[sq] = NO_PIECE;
        expectedColor[sq] = NO_COLOR;
    }
    for (int c = 0; c < 2; c++) {
        for (int p = 0; p < 6; p++) {
            U64 bb = pieces[c][p];
            while (bb) {
                int sq = __builtin_ctzll(bb);
                bb &= bb - 1;
                expectedBoard[sq] = static_cast<Piece>(p);
                expectedColor[sq] = static_cast<Color>(c);
            }
        }
    }
    for (int sq = 0; sq < 64; sq++) {
        if (board[sq] != expectedBoard[sq] || colorBoard[sq] != expectedColor[sq]) {
            std::cerr << "\n=== MAILBOX/BITBOARD DESYNC AFTER makeMove ===" << std::endl;
            std::cerr << "Move: " << squareToNotation(move.from) << "->" << squareToNotation(move.to)
                      << " flags=" << (int)move.flags
                      << " promo=" << (int)move.promotion << std::endl;
            std::cerr << "Square " << squareToNotation(static_cast<Square>(sq)) << ":" << std::endl;
            std::cerr << "  Mailbox says: piece=" << (int)board[sq]
                      << " color=" << (int)colorBoard[sq] << std::endl;
            std::cerr << "  Bitboards say: piece=" << (int)expectedBoard[sq]
                      << " color=" << (int)expectedColor[sq] << std::endl;
            printBoard();
            assert(false && "makeMove produced mailbox/bitboard desync");
        }
    }
    validatePosition();
    assert(NNUE::verifyAccumulator(*this) && "NNUE accumulator desync after makeMove");
#endif
}

void Position::undoMove(const Move& move, const Undo& undo) {
    // Backstop for the release-build contract-violation path in makeMove.
    // If undo.moved == NO_PIECE, then makeMove was called with an invalid
    // move (failed the source-square check), did NOT modify any state,
    // and did NOT increment posHistoryCount. So undoMove must also be a
    // no-op. In debug builds, makeMove asserts hard before getting here,
    // so this path is unreachable; in release builds, it preserves state
    // exactly so the search can recover cleanly by trying the next move.
    if (undo.moved == NO_PIECE) {
        return;
    }

    posHistoryCount--;
    sideToMove = (sideToMove == WHITE ? BLACK : WHITE);
    Color us   = sideToMove;
    Color them = (us == WHITE ? BLACK : WHITE);

    int fromFile = move.from & 7;
    int toFile   = move.to   & 7;
    U64 fromBB = 1ULL << move.from;
    U64 toBB   = 1ULL << move.to;

    // Pawn file count undo
    if (undo.moved == PAWN) {
        pawnFileCount[us][fromFile]++;
        pawnFileCount[us][toFile]--;
    }
    if (move.flags & CAPTURE) {
        Square capSq = move.to;
        if (move.flags & EN_PASSANT)
            capSq = (us == WHITE) ? Square(move.to - 8) : Square(move.to + 8);
        if (undo.captured == PAWN)
            pawnFileCount[them][capSq & 7]++;
    }
    if (move.flags & PROMOTION)
        pawnFileCount[us][toFile]++;

    // === PROMOTION UNDO ===
    if (move.flags & PROMOTION) {
        removePieceEval(us, move.promotion, move.to);
        pieces[us][move.promotion] &= ~toBB;
        pieces[us][PAWN] |= fromBB;
        addPieceEval(us, PAWN, move.from);

        // Ã¢Å¡Â¡ MAILBOX: Restore pawn
        board[move.to] = NO_PIECE;
        colorBoard[move.to] = NO_COLOR;
        board[move.from] = PAWN;
        colorBoard[move.from] = us;

        if (move.promotion == KNIGHT || move.promotion == BISHOP) gamePhase--;
        else if (move.promotion == ROOK) gamePhase -= 2;
        else if (move.promotion == QUEEN) gamePhase -= 4;
        gamePhase = std::max(0, gamePhase);

        if (undo.captured != NO_PIECE) {
            pieces[them][undo.captured] |= toBB;
            addPieceEval(them, undo.captured, move.to);
            
            // Ã¢Å¡Â¡ MAILBOX: Restore captured piece
            board[move.to] = undo.captured;
            colorBoard[move.to] = them;
            
            if (undo.captured == KNIGHT || undo.captured == BISHOP) gamePhase++;
            else if (undo.captured == ROOK) gamePhase += 2;
            else if (undo.captured == QUEEN) gamePhase += 4;
            gamePhase = std::min(24, gamePhase);
        }

        enPassantSquare = undo.ep;
        castlingRights  = undo.castling;
        halfmoveClock   = undo.halfmove;
        zobristKey      = undo.zobristKey;
        updateOccupancy();
#ifndef NDEBUG
        validatePosition();
#endif
        return;
    }

    // === CASTLING UNDO ===
    if (move.flags & CASTLING) {
        removePieceEval(us, KING, move.to);
        pieces[us][KING] &= ~toBB;
        pieces[us][KING] |= fromBB;
        addPieceEval(us, KING, move.from);

        // Ã¢Å¡Â¡ MAILBOX: Move king back
        board[move.to] = NO_PIECE;
        colorBoard[move.to] = NO_COLOR;
        board[move.from] = KING;
        colorBoard[move.from] = us;

        Square rookFrom, rookTo;
        if (move.to == G1) { rookFrom = F1; rookTo = H1; }
        else if (move.to == C1) { rookFrom = D1; rookTo = A1; }
        else if (move.to == G8) { rookFrom = F8; rookTo = H8; }
        else { rookFrom = D8; rookTo = A8; }

        removePieceEval(us, ROOK, rookFrom);
        pieces[us][ROOK] &= ~(1ULL << rookFrom);
        pieces[us][ROOK] |= (1ULL << rookTo);
        addPieceEval(us, ROOK, rookTo);
        
        board[rookFrom] = NO_PIECE;
        colorBoard[rookFrom] = NO_COLOR;
        board[rookTo] = ROOK;
        colorBoard[rookTo] = us;

        enPassantSquare = undo.ep;
        castlingRights  = undo.castling;
        halfmoveClock   = undo.halfmove;
        zobristKey      = undo.zobristKey;
        updateOccupancy();
#ifndef NDEBUG
        validatePosition(); 
#endif
        return;
    }

    // === RESTORE MOVING PIECE ===
    removePieceEval(us, undo.moved, move.to);
    pieces[us][undo.moved] &= ~toBB;
    pieces[us][undo.moved] |= fromBB;
    addPieceEval(us, undo.moved, move.from);

    // Ã¢Å¡Â¡ MAILBOX: Move piece back
    board[move.to] = NO_PIECE;
    colorBoard[move.to] = NO_COLOR;
    board[move.from] = undo.moved;
    colorBoard[move.from] = us;

    // === RESTORE CAPTURE ===
    if (move.flags & EN_PASSANT) {
        Square capSq = (us == WHITE) ? Square(move.to - 8) : Square(move.to + 8);
        pieces[them][PAWN] |= (1ULL << capSq);
        addPieceEval(them, PAWN, capSq);
        
        // Ã¢Å¡Â¡ MAILBOX: Restore captured pawn
        board[capSq] = PAWN;
        colorBoard[capSq] = them;
    } else if (undo.captured != NO_PIECE) {
        pieces[them][undo.captured] |= toBB;
        addPieceEval(them, undo.captured, move.to);
        
        // Ã¢Å¡Â¡ MAILBOX: Restore captured piece
        board[move.to] = undo.captured;
        colorBoard[move.to] = them;
        
        if (undo.captured == KNIGHT || undo.captured == BISHOP) gamePhase++;
        else if (undo.captured == ROOK) gamePhase += 2;
        else if (undo.captured == QUEEN) gamePhase += 4;
        gamePhase = std::min(24, gamePhase);
    }

    enPassantSquare = undo.ep;
    castlingRights  = undo.castling;
    halfmoveClock   = undo.halfmove;
    zobristKey      = undo.zobristKey;
    updateOccupancy();

#ifndef NDEBUG
    // Hard-assert mailbox/bitboard consistency after undo. If this fires,
    // the undoMove logic has a bug -- masking it via rebuildMailbox()
    // would let it propagate silently. Better to crash visibly here than
    // produce wrong moves at depth 20.
    Piece expectedBoard[64];
    Color expectedColor[64];
    for (int sq = 0; sq < 64; sq++) {
        expectedBoard[sq] = NO_PIECE;
        expectedColor[sq] = NO_COLOR;
    }
    for (int c = 0; c < 2; c++) {
        for (int p = 0; p < 6; p++) {
            U64 bb = pieces[c][p];
            while (bb) {
                int sq = __builtin_ctzll(bb);
                bb &= bb - 1;
                expectedBoard[sq] = static_cast<Piece>(p);
                expectedColor[sq] = static_cast<Color>(c);
            }
        }
    }
    for (int sq = 0; sq < 64; sq++) {
        if (board[sq] != expectedBoard[sq] || colorBoard[sq] != expectedColor[sq]) {
            std::cerr << "\n=== MAILBOX/BITBOARD DESYNC AFTER undoMove ===" << std::endl;
            std::cerr << "Move: " << squareToNotation(move.from) << "->" << squareToNotation(move.to)
                      << " flags=" << (int)move.flags
                      << " promo=" << (int)move.promotion << std::endl;
            std::cerr << "undo.moved=" << (int)undo.moved
                      << " undo.captured=" << (int)undo.captured << std::endl;
            std::cerr << "Square " << squareToNotation(static_cast<Square>(sq)) << ":" << std::endl;
            std::cerr << "  Mailbox says: piece=" << (int)board[sq]
                      << " color=" << (int)colorBoard[sq] << std::endl;
            std::cerr << "  Bitboards say: piece=" << (int)expectedBoard[sq]
                      << " color=" << (int)expectedColor[sq] << std::endl;
            printBoard();
            assert(false && "undoMove produced mailbox/bitboard desync");
        }
    }
    validatePosition();
    assert(NNUE::verifyAccumulator(*this) && "NNUE accumulator desync after undoMove");
#endif
}

void Position::makeNullMove(Undo& undo) {
    undo.ep = enPassantSquare;
    undo.zobristKey = zobristKey;
    undo.castling = castlingRights;
    undo.halfmove = halfmoveClock;

    if (enPassantSquare != NO_SQUARE)
        zobristKey ^= Zobrist::enPassant[enPassantSquare];
    
    enPassantSquare = NO_SQUARE;
    sideToMove = (sideToMove == WHITE ? BLACK : WHITE);
    zobristKey ^= Zobrist::sideToMove;
    halfmoveClock++;
}

void Position::undoNullMove(const Undo& undo) {
    sideToMove = (sideToMove == WHITE ? BLACK : WHITE);
    enPassantSquare = undo.ep;
    zobristKey = undo.zobristKey;
    castlingRights = undo.castling;
    halfmoveClock = undo.halfmove;
}

bool Position::operator==(const Position& other) const {
    for (int color = 0; color < 2; color++)
        for (int pType = 0; pType < 6; pType++)
            if (pieces[color][pType] != other.pieces[color][pType])
                return false;
    return sideToMove == other.sideToMove &&
           castlingRights == other.castlingRights &&
           enPassantSquare == other.enPassantSquare &&
           halfmoveClock == other.halfmoveClock;
}

U64 Position::computeZobrist(const Position& pos) {
    U64 key = 0;
    for (int c = 0; c < 2; c++)
        for (int p = 0; p < 6; p++) {
            U64 bb = pos.pieces[c][p];
            while (bb) {
                int sq = __builtin_ctzll(bb);
                bb &= bb - 1;
                key ^= Zobrist::piece[c][p][sq];
            }
        }
    key ^= Zobrist::castling[pos.castlingRights];
    if (pos.enPassantSquare != NO_SQUARE)
        key ^= Zobrist::enPassant[pos.enPassantSquare];
    if (pos.sideToMove == BLACK)
        key ^= Zobrist::sideToMove;
    return key;
}

// ===========================================================================
// isPseudoLegal
// ---------------------------------------------------------------------------
// Validates that a candidate Move is mechanically playable in the current
// position. This is the primary defense against TT collisions / lockless-TT
// torn reads producing a Move that decodes to a syntactically valid Move
// struct but refers to a piece/square layout that doesn't exist here.
//
// Returns false if ANY of:
//   - from/to/promotion enum values are out of range
//   - source square is empty or holds an enemy piece
//   - target holds a friendly piece (would be self-capture)
//   - CAPTURE flag set but target is empty (and not en passant)
//   - CAPTURE flag clear but target is occupied
//   - The piece on `from` cannot geometrically reach `to`
//   - Castling/en-passant/promotion preconditions are violated
//
// Does NOT check:
//   - Whether the move leaves our king in check (search does this after
//     makeMove via isKingInCheck(us))
//   - Castling-through-check (king walks through attacked squares).
//     Move generation already filters this at generation time; a TT move
//     that bypasses that filter is rare enough that we accept the small
//     waste of one search node on it, in exchange for keeping this check
//     cheap.
// ===========================================================================
bool Position::isPseudoLegal(const Move& move) const {
    // --- Range checks ---
    if (move.from < 0 || move.from >= 64) return false;
    if (move.to   < 0 || move.to   >= 64) return false;
    if (move.from == move.to) return false;

    const Color us   = sideToMove;
    const Color them = (us == WHITE ? BLACK : WHITE);

    // --- Source must hold a friendly piece ---
    const Piece moved = board[move.from];
    if (moved == NO_PIECE) return false;
    if (colorBoard[move.from] != us) return false;

    // --- Target square color check ---
    const Piece targetPiece = board[move.to];
    if (targetPiece != NO_PIECE && colorBoard[move.to] == us) {
        // Self-capture is never legal except as the rook destination of
        // a chess960-style castling encoding -- we don't support that, so
        // for orthodox castling encoding the king's `to` is never our rook.
        return false;
    }

    const U64 occAll = occupancy[WHITE] | occupancy[BLACK];
    const U64 toBB   = 1ULL << move.to;

    // --- Capture flag must be consistent with mailbox ---
    const bool isCaptureFlag  = (move.flags & CAPTURE)   != 0;
    const bool isEnPassantFlag= (move.flags & EN_PASSANT)!= 0;
    const bool isCastlingFlag = (move.flags & CASTLING)  != 0;
    const bool isPromotionFlag= (move.flags & PROMOTION) != 0;
    const bool isDoublePush   = (move.flags & PAWN_DOUBLE)!= 0;

    if (isEnPassantFlag) {
        // EN_PASSANT implies CAPTURE in this encoding, target square is
        // empty, captured pawn sits on the adjacent rank.
        if (!isCaptureFlag) return false;
        if (moved != PAWN) return false;
        if (move.to != enPassantSquare) return false;
        if (targetPiece != NO_PIECE) return false; // EP target is empty
        const Square capSq = (us == WHITE) ? Square(move.to - 8) : Square(move.to + 8);
        if (capSq < 0 || capSq >= 64) return false;
        if (board[capSq] != PAWN) return false;
        if (colorBoard[capSq] != them) return false;
    } else {
        if (isCaptureFlag) {
            // Normal capture: target must hold an enemy piece.
            if (targetPiece == NO_PIECE) return false;
            if (colorBoard[move.to] != them) return false;
        } else {
            // Quiet move: target must be empty.
            if (targetPiece != NO_PIECE) return false;
        }
    }

    // --- Promotion flag consistency ---
    if (isPromotionFlag) {
        if (moved != PAWN) return false;
        const int toRank = move.to >> 3;
        if (us == WHITE && toRank != 7) return false;
        if (us == BLACK && toRank != 0) return false;
        // Promotion piece must be Knight/Bishop/Rook/Queen
        if (move.promotion != KNIGHT && move.promotion != BISHOP &&
            move.promotion != ROOK   && move.promotion != QUEEN) return false;
    } else {
        // Non-promotion pawn pushes must NOT reach the last rank.
        if (moved == PAWN) {
            const int toRank = move.to >> 3;
            if ((us == WHITE && toRank == 7) || (us == BLACK && toRank == 0))
                return false;
        }
    }

    // --- Castling flag consistency ---
    if (isCastlingFlag) {
        if (moved != KING) return false;
        if (isCaptureFlag) return false;
        if (us == WHITE) {
            if (move.from != E1) return false;
            if (move.to == G1) {
                if (!(castlingRights & WK)) return false;
                if (board[H1] != ROOK || colorBoard[H1] != WHITE) return false;
                if (board[F1] != NO_PIECE || board[G1] != NO_PIECE) return false;
                // King must not currently be in check or pass through check
                if (isSquareAttacked(E1, BLACK)) return false;
                if (isSquareAttacked(F1, BLACK)) return false;
                if (isSquareAttacked(G1, BLACK)) return false;
            } else if (move.to == C1) {
                if (!(castlingRights & WQ)) return false;
                if (board[A1] != ROOK || colorBoard[A1] != WHITE) return false;
                if (board[B1] != NO_PIECE || board[C1] != NO_PIECE || board[D1] != NO_PIECE) return false;
                if (isSquareAttacked(E1, BLACK)) return false;
                if (isSquareAttacked(D1, BLACK)) return false;
                if (isSquareAttacked(C1, BLACK)) return false;
            } else {
                return false;
            }
        } else {
            if (move.from != E8) return false;
            if (move.to == G8) {
                if (!(castlingRights & BK)) return false;
                if (board[H8] != ROOK || colorBoard[H8] != BLACK) return false;
                if (board[F8] != NO_PIECE || board[G8] != NO_PIECE) return false;
                if (isSquareAttacked(E8, WHITE)) return false;
                if (isSquareAttacked(F8, WHITE)) return false;
                if (isSquareAttacked(G8, WHITE)) return false;
            } else if (move.to == C8) {
                if (!(castlingRights & BQ)) return false;
                if (board[A8] != ROOK || colorBoard[A8] != BLACK) return false;
                if (board[B8] != NO_PIECE || board[C8] != NO_PIECE || board[D8] != NO_PIECE) return false;
                if (isSquareAttacked(E8, WHITE)) return false;
                if (isSquareAttacked(D8, WHITE)) return false;
                if (isSquareAttacked(C8, WHITE)) return false;
            } else {
                return false;
            }
        }
        // Castling reachability is fully validated above; skip geometry check.
        return true;
    }

    // --- Geometric reachability per piece type ---
    switch (moved) {
        case PAWN: {
            const int fromRank = move.from >> 3;
            const int toRank   = move.to   >> 3;
            const int fromFile = move.from & 7;
            const int toFile   = move.to   & 7;
            const int dir      = (us == WHITE) ? 1 : -1;
            const int startRank= (us == WHITE) ? 1 : 6;

            if (isCaptureFlag) {
                // Diagonal capture: rank changes by +/-1, file changes by +/-1
                if (toRank - fromRank != dir) return false;
                if (std::abs(toFile - fromFile) != 1) return false;
                // (capture-flag/EP consistency already validated above)
            } else if (isDoublePush) {
                // Double push: must be from start rank, both squares empty
                if (fromRank != startRank) return false;
                if (toRank - fromRank != 2 * dir) return false;
                if (fromFile != toFile) return false;
                const Square intermediate = Square(move.from + 8 * dir);
                if (board[intermediate] != NO_PIECE) return false;
                // (target empty already validated)
            } else {
                // Single push
                if (toRank - fromRank != dir) return false;
                if (fromFile != toFile) return false;
            }
            break;
        }
        case KNIGHT: {
            if (!(Attacks::knightAttacks[move.from] & toBB)) return false;
            break;
        }
        case BISHOP: {
            if (!(Attacks::bishopAttacks(move.from, occAll) & toBB)) return false;
            break;
        }
        case ROOK: {
            if (!(Attacks::rookAttacks(move.from, occAll) & toBB)) return false;
            break;
        }
        case QUEEN: {
            if (!(Attacks::queenAttacks(move.from, occAll) & toBB)) return false;
            break;
        }
        case KING: {
            // Non-castling king move: target must be a king-step away
            if (!(Attacks::kingAttacks[move.from] & toBB)) return false;
            break;
        }
        default:
            return false;
    }

    return true;
}
