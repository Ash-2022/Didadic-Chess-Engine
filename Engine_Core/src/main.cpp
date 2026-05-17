#include "position.h"
#include "tt_atomic.h"
#include "thread_pool.h"
#include <chrono>
#include <cassert>
#include <iostream>
#include <string>
#include "perft.h"
#include "search.h"
#include "zobrist.h"
#include "uci.h"
#include "magics.h"
#include "evaluation.h"

struct EngineOptions {
    int ttSizeMB = 1024;
    int numThreads = 8;
    int benchDepth = 15;
    bool useThreading = true;
    bool runBenchmark = false;
    bool runTests = false;
    bool uciMode = true;
};

void printUsage(const char* progName) {
    std::cout << "Chess Engine - Usage:\n"
              << "  " << progName << " [options]\n\n"
              << "Options:\n"
              << "  --uci              Enter UCI mode (default)\n"
              << "  --bench            Run benchmark test\n"
              << "  --test             Run tactical tests\n"
              << "  --depth N          Set benchmark depth (default: 15)\n"
              << "  --threads N        Number of threads (default: 8)\n"
              << "  --hash N           Hash table size in MB (default: 512)\n"
              << "  --help             Show this help\n";
}

EngineOptions parseCommandLine(int argc, char* argv[]) {
    EngineOptions opts;
    if (argc == 1) return opts;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { printUsage(argv[0]); exit(0); }
        else if (arg == "--uci") { opts.uciMode = true; opts.runBenchmark = false; opts.runTests = false; }
        else if (arg == "--bench") { opts.runBenchmark = true; opts.uciMode = false; }
        else if (arg == "--test") { opts.runTests = true; opts.uciMode = false; }
        else if (arg == "--depth" && i+1 < argc) opts.benchDepth = std::stoi(argv[++i]);
        else if (arg == "--threads" && i+1 < argc) { opts.numThreads = std::stoi(argv[++i]); opts.useThreading = (opts.numThreads > 1); }
        else if (arg == "--hash" && i+1 < argc) opts.ttSizeMB = std::stoi(argv[++i]);
        else if (arg == "uci") { opts.uciMode = true; opts.runBenchmark = false; opts.runTests = false; }
        else if (arg == "nouci") { opts.runBenchmark = true; opts.uciMode = false; }
        else { std::cout << "Unknown option: " << arg << std::endl; printUsage(argv[0]); exit(1); }
    }
    return opts;
}

void runBenchmark(const EngineOptions& opts) {
    std::cout << "=== Performance Benchmark ===" << std::endl;

    Position pos;
    pos.setFromFEN("r1bqkbnr/pppppppp/2n5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2");

    std::cout << "Position: " << pos.toFEN() << std::endl;
    std::cout << "Depth: " << opts.benchDepth << std::endl;

    auto startTime = std::chrono::high_resolution_clock::now();
    Move best = iterativeDeepening(pos, opts.benchDepth);
    auto endTime = std::chrono::high_resolution_clock::now();
    double seconds = std::chrono::duration<double>(endTime - startTime).count();

    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Best move: ";
    MoveGenerator::printMove(best, pos);
    std::cout << "Time: " << seconds << " seconds" << std::endl;

    if (globalThreadPool && globalThreadPool->getNumThreads() > 1) {
        uint64_t totalNodes = globalThreadPool->getTotalNodes();
        uint64_t nps = (seconds > 0) ? (totalNodes / seconds) : 0;
        std::cout << "Total nodes: " << totalNodes << std::endl;
        std::cout << "NPS: " << nps << std::endl;
    }
}

void runTacticalTests() {
    std::cout << "=== Tactical Test Suite ===" << std::endl;

    Position pos;
    int passed = 0, total = 5;

    auto runTest = [&](const char* name, const char* fen, const char* expected,
                       Square expFrom, Square expTo, int depth) {
        std::cout << "\n" << name << std::endl;
        pos.setFromFEN(fen);
        std::cout << "Expected: " << expected << std::endl;
        Move best = iterativeDeepening(pos, depth);
        std::cout << "Result: ";
        MoveGenerator::printMove(best, pos);
        if (best.from == expFrom && best.to == expTo) {
            std::cout << "PASSED" << std::endl;
            passed++;
        } else {
            std::cout << "FAILED" << std::endl;
        }
    };

    runTest("Back Rank Mate", "6k1/5ppp/8/8/8/8/5PPP/4R1K1 w - - 0 1", "Re8#", E1, E8, 6);
    runTest("Smothered Mate", "6rk/6pp/7N/8/8/8/8/6K1 w - - 0 1", "Nf7#", H6, F7, 6);
    runTest("Knight Fork", "r1bqkb1r/pppp1ppp/2n5/4p3/4n3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 1", "Nxe5", F3, E5, 8);
    runTest("Fool's Mate", "rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq - 0 1", "Qh4#", D8, H4, 6);
    runTest("Legal's Mate", "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 1", "Qxf7+", H5, F7, 8);

    std::cout << "\nPassed: " << passed << "/" << total << std::endl;
}

int main(int argc, char* argv[]) {
    EngineOptions opts = parseCommandLine(argc, argv);

    std::cout << "=== Chess Engine v2.0 ===" << std::endl;

    // Initialize in correct order
    Zobrist::init();
    Attacks::init();
    Magics::init();
    initEvaluation();
    initLMR();

    // TT first, then thread pool (thread pool depends on TT)
    std::cout << "TT: " << opts.ttSizeMB << " MB" << std::endl;
    initTTAtomic(opts.ttSizeMB);

    if (opts.useThreading) {
        std::cout << "Threads: " << opts.numThreads << std::endl;
        initThreadPool(opts.numThreads);
    } else {
        std::cout << "Single-threaded mode" << std::endl;
        initThreadPool(1);
    }

    std::cout << std::endl;

    if (opts.runBenchmark) {
        runBenchmark(opts);
    } else if (opts.runTests) {
        runTacticalTests();
    } else if (opts.uciMode) {
        UCI::loop();
    }

    // CRITICAL: Proper shutdown order - threads first, then TT
    shutdownEngine();

    return 0;
}
