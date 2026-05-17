#include "uci.h"
#include "search.h"
#include "thread_pool.h"
#include "tt_atomic.h"

namespace UCI {

    std::string moveToUCI(const Move& move) {
        std::string uci;
        uci += char('a' + move.from % 8);
        uci += char('1' + move.from / 8);
        uci += char('a' + move.to % 8);
        uci += char('1' + move.to / 8);

        if (move.flags & PROMOTION) {
            switch (move.promotion) {
                case QUEEN: uci += 'q'; break;
                case ROOK: uci += 'r'; break;
                case BISHOP: uci += 'b'; break;
                case KNIGHT: uci += 'n'; break;
                default: break;
            }
        }
        return uci;
    }

    Move parseUCIMove(const Position& pos, const std::string& uciMove) {
        if (uciMove.length() < 4) return nullMove();

        Square from = Square((uciMove[1] - '1') * 8 + (uciMove[0] - 'a'));
        Square to = Square((uciMove[3] - '1') * 8 + (uciMove[2] - 'a'));

        Piece promotion = NO_PIECE;
        if (uciMove.length() >= 5) {
            switch (uciMove[4]) {
                case 'q': promotion = QUEEN; break;
                case 'r': promotion = ROOK; break;
                case 'b': promotion = BISHOP; break;
                case 'n': promotion = KNIGHT; break;
                default: break;
            }
        }

        std::vector<Move> moves;
        MoveGenerator::generateAllMoves(pos, moves);

        for (const Move& m : moves) {
            if (m.from == from && m.to == to) {
                if ((m.flags & PROMOTION) && promotion != NO_PIECE) {
                    if (m.promotion == promotion) return m;
                } else if (!(m.flags & PROMOTION)) {
                    return m;
                }
            }
        }
        return nullMove();
    }

    void loop() {
        Position pos;
        pos.setStartPosition();

        std::string line;
        while (std::getline(std::cin, line)) {
            std::istringstream iss(line);
            std::string token;
            iss >> token;

            if (token == "uci") {
                std::cout << "id name ChessEngine v2.0" << std::endl;
                std::cout << "id author Ashvin" << std::endl;
                std::cout << "option name Hash type spin default 512 min 1 max 65536" << std::endl;
                std::cout << "option name Threads type spin default 8 min 1 max 256" << std::endl;
                std::cout << "uciok" << std::endl;
            }
            else if (token == "setoption") {
                std::string name, value;
                iss >> token; // "name"
                iss >> name;
                iss >> token; // "value"
                iss >> value;

                if (name == "Hash") {
                    int sizeMB = std::stoi(value);
                    if (globalTT) globalTT->resize(sizeMB);
                } else if (name == "Threads") {
                    int threads = std::stoi(value);
                    if (globalThreadPool) globalThreadPool->setNumThreads(threads);
                }
            }
            else if (token == "isready") {
                std::cout << "readyok" << std::endl;
            }
            else if (token == "ucinewgame") {
                pos.setStartPosition();
                if (globalTT) {
                    globalTT->clear();
                    globalTT->nextGeneration();
                }
                if (globalThreadPool) globalThreadPool->clear();
            }
            else if (token == "position") {
                iss >> token;

                if (token == "startpos") {
                    pos.setStartPosition();
                    iss >> token;
                } else if (token == "fen") {
                    std::string fen;
                    std::string part;
                    for (int i = 0; i < 6 && iss >> part; i++) {
                        if (!fen.empty()) fen += " ";
                        fen += part;
                    }
                    if (!pos.setFromFEN(fen)) {
                        std::cerr << "Invalid FEN" << std::endl;
                        continue;
                    }
                    iss >> token;
                }

                if (token == "moves") {
                    std::string moveStr;
                    while (iss >> moveStr) {
                        Move move = parseUCIMove(pos, moveStr);
                        if (!move.isNull()) {
                            Undo undo;
                            pos.makeMove(move, undo);
                        }
                    }
                }
            }
            else if (token == "go") {
                int depth = 50;
                int movetime = -1;
                int wtime = -1, btime = -1;
                int winc = 0, binc = 0;
                bool infinite = false;
                bool depthSpecified = false;

                while (iss >> token) {
                    if (token == "depth") { iss >> depth; depthSpecified = true; }
                    else if (token == "movetime") iss >> movetime;
                    else if (token == "wtime") iss >> wtime;
                    else if (token == "btime") iss >> btime;
                    else if (token == "winc") iss >> winc;
                    else if (token == "binc") iss >> binc;
                    else if (token == "infinite") infinite = true;
                }

                g_timeUp.store(false, std::memory_order_release);
                g_stopSearch.store(false, std::memory_order_release);
                g_searchStartTime = std::chrono::steady_clock::now();

                if (infinite) {
                    g_allocatedTime = 0;
                } else if (depthSpecified && movetime < 0 && wtime < 0) {
                    g_allocatedTime = 0;
                } else if (movetime > 0) {
                    g_allocatedTime = movetime - 50;
                } else {
                    int timeLeft = (pos.sideToMove == WHITE) ? wtime : btime;
                    int increment = (pos.sideToMove == WHITE) ? winc : binc;
                    if (timeLeft > 0) {
                        g_allocatedTime = timeLeft / 30 + (increment * 85) / 100 - 50;
                        g_allocatedTime = std::max(g_allocatedTime, 10);
                        g_allocatedTime = std::min(g_allocatedTime, timeLeft / 2);
                    } else {
                        g_allocatedTime = 0;
                    }
                }

                Move bestMove = iterativeDeepening(pos, depth);
                std::cout << "bestmove " << moveToUCI(bestMove) << std::endl;
            }
            else if (token == "stop") {
                g_stopSearch.store(true, std::memory_order_release);
                g_timeUp.store(true, std::memory_order_release);

                Move bm = g_bestMove.load(std::memory_order_relaxed);
                if (!bm.isNull()) {
                    std::cout << "bestmove " << moveToUCI(bm) << std::endl;
                }
            }

            else if (token == "quit") {
                break;
            }
            else if (token == "d") {
                pos.printBoard();
            }
        }
    }

} // namespace UCI
