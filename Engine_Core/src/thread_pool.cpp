#include "thread_pool.h"
#include "position.h"
#include "search.h"
#include "uci.h"
#include <iostream>
#include <chrono>

// Globals
std::unique_ptr<ThreadPool> globalThreadPool;
std::atomic<bool> g_stopSearch{false};

void initThreadPool(int numThreads) {
    globalThreadPool = std::make_unique<ThreadPool>(numThreads);
}

void shutdownEngine() {
    globalThreadPool.reset();
    globalTT.reset();
}

/* ThreadData */
ThreadData::ThreadData()
    : state(std::make_unique<SearchState>())
{
    bestMove = nullMove();
}

/* ThreadPool */
ThreadPool::ThreadPool(int threads) : numThreads_(threads) {
    threadData_.reserve(numThreads_);
    for (int i = 0; i < numThreads_; i++) {
        threadData_.push_back(std::make_unique<ThreadData>());
        threadData_[i]->threadID = i;
    }

    for (int i = 1; i < numThreads_; i++) {
        threads_.emplace_back(&ThreadPool::helperLoop, this, i);
    }
}

ThreadPool::~ThreadPool() {
    exitFlag_.store(true, std::memory_order_release);
    g_stopSearch.store(true, std::memory_order_release);
    
    {
        std::lock_guard<std::mutex> lock(startMutex_);
        searchActive_.store(true, std::memory_order_release);
    }
    startCV_.notify_all();

    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
}

void ThreadPool::helperLoop(int threadID) {
    while (true) {
        {
            std::unique_lock<std::mutex> lock(startMutex_);
            startCV_.wait(lock, [&] {
                return searchActive_.load(std::memory_order_acquire) ||
                       exitFlag_.load(std::memory_order_acquire);
            });
        }

        if (exitFlag_.load(std::memory_order_acquire))
            break;

        helperSearch(*threadData_[threadID]);
        helpersFinished_.fetch_add(1, std::memory_order_release);
    }
}

/* ✅ OPTIMIZED: Helpers do FULL iterative deepening with slight depth offsets */
void ThreadPool::helperSearch(ThreadData& td) {
    td.state->clear();
    td.state->threadID = td.threadID;
    td.nodes = 0;

    // Each helper searches to maxDepth +/- offset for diversity
    int depthOffset = (td.threadID % 3) - 1; // -1, 0, +1
    int targetDepth = td.maxDepth + depthOffset;
    
    // Full iterative deepening from depth 1
    for (int depth = 1; depth <= targetDepth; depth++) {
        if (g_stopSearch.load(std::memory_order_relaxed)) break;

        // Direct negamax call
        negamax(td.pos, *td.state, depth, 0, -INF, INF, true);
        
        // Update best move from TT after each depth.
        // Validate isPseudoLegal: a torn TT read or hash collision can
        // produce a syntactically-valid Move that isn't actually playable
        // in this position. If we stored that as bestMove, getBestMove()
        // could return it and the engine would emit an illegal move.
        int16_t ttScore;
        Move ttMove = nullMove();
        int8_t ttDepth;
        uint8_t ttFlag;

        if (globalTT->probe(td.pos.zobristKey, ttScore, ttMove, ttDepth, ttFlag, 0) &&
            !ttMove.isNull() && td.pos.isPseudoLegal(ttMove)) {
            td.bestMove = ttMove;
        }
    }

    td.nodes = td.state->nodesSearched;
}

Move ThreadPool::search(const Position& pos, int maxDepth) {
    g_stopSearch.store(false, std::memory_order_release);
    helpersFinished_.store(0, std::memory_order_release);

    // Setup all threads
    for (int i = 0; i < numThreads_; i++) {
        threadData_[i]->pos = pos;
        threadData_[i]->maxDepth = maxDepth;
        threadData_[i]->state->clear();
        threadData_[i]->state->threadID = i;
        threadData_[i]->nodes = 0;
        threadData_[i]->bestMove = nullMove();
        threadData_[i]->bestScore = 0;
    }

    // Start helper threads
    {
        std::lock_guard<std::mutex> lock(startMutex_);
        searchActive_.store(true, std::memory_order_release);
    }
    startCV_.notify_all();

    auto start = std::chrono::high_resolution_clock::now();

    // Main thread does full ID with UCI output
    ThreadData& mainTD = *threadData_[0];
    mainTD.bestMove = iterativeDeepeningST(mainTD.pos, mainTD.maxDepth, *mainTD.state);
    mainTD.nodes = mainTD.state->nodesSearched;

    // Stop all helpers
    g_stopSearch.store(true, std::memory_order_release);

    // Wait for helpers to finish
    int expectedHelpers = numThreads_ - 1;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    
    while (helpersFinished_.load(std::memory_order_acquire) < expectedHelpers) {
        if (std::chrono::steady_clock::now() > deadline) {
            std::cerr << "Warning: helper thread timeout" << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    searchActive_.store(false, std::memory_order_release);

    auto end = std::chrono::high_resolution_clock::now();
    double sec = std::chrono::duration<double>(end - start).count();
    uint64_t totalNodes = getTotalNodes();
    uint64_t nps = sec > 0 ? static_cast<uint64_t>(totalNodes / sec) : 0;

    std::cout << std::endl;
    std::cout << "Search summary:" << std::endl;
    std::cout << "  Total nodes: " << totalNodes << std::endl;
    std::cout << "  Time: " << sec << " seconds" << std::endl;
    std::cout << "  NPS: " << nps << std::endl;
    std::cout << std::endl;

    return mainTD.bestMove;
}

void ThreadPool::stop() {
    g_stopSearch.store(true, std::memory_order_release);
}

uint64_t ThreadPool::getTotalNodes() const {
    uint64_t total = 0;
    for (int i = 0; i < numThreads_; i++) {
        total += threadData_[i]->nodes;
    }
    return total;
}

Move ThreadPool::getBestMove() const {
    return threadData_[0]->bestMove;
}

void ThreadPool::setNumThreads(int threads) {
    if (threads == numThreads_) return;

    exitFlag_.store(true, std::memory_order_release);
    g_stopSearch.store(true, std::memory_order_release);
    
    {
        std::lock_guard<std::mutex> lock(startMutex_);
        searchActive_.store(true, std::memory_order_release);
    }
    startCV_.notify_all();

    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();

    numThreads_ = threads;
    exitFlag_.store(false, std::memory_order_release);
    g_stopSearch.store(false, std::memory_order_release);
    searchActive_.store(false, std::memory_order_release);

    threadData_.clear();
    threadData_.reserve(numThreads_);
    
    for (int i = 0; i < numThreads_; i++) {
        threadData_.push_back(std::make_unique<ThreadData>());
        threadData_[i]->threadID = i;
    }

    for (int i = 1; i < numThreads_; i++) {
        threads_.emplace_back(&ThreadPool::helperLoop, this, i);
    }
}

void ThreadPool::clear() {
    for (int i = 0; i < numThreads_; i++) {
        threadData_[i]->state->clear();
        threadData_[i]->nodes = 0;
    }
}
