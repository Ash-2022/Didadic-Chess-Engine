#pragma once
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "types.h"
#include "position.h"
#include "search.h"
#include "tt_atomic.h"
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <memory>

/* =========================
   LAZY SMP THREAD POOL (RAII)
   
   - Thread 0 (main) does full ID with UCI output
   - Threads 1..N do ID with depth offsets
   - All threads share lock-free atomic TT
   - Proper shutdown: stop threads BEFORE releasing TT
   ========================= */

struct ThreadData {
    int threadID = 0;
    Position pos;
    // SearchState is non-copyable, use unique_ptr
    std::unique_ptr<SearchState> state;
    int maxDepth = 0;
    Move bestMove;
    int bestScore = 0;
    uint64_t nodes = 0;

    ThreadData();
    ~ThreadData() = default;

    // Non-copyable
    ThreadData(const ThreadData&) = delete;
    ThreadData& operator=(const ThreadData&) = delete;
};

class ThreadPool {
private:
    std::vector<std::thread> threads_;
    std::vector<std::unique_ptr<ThreadData>> threadData_;
    int numThreads_;

    std::mutex startMutex_;
    std::condition_variable startCV_;
    std::atomic<bool> searchActive_{false};
    std::atomic<bool> exitFlag_{false};
    std::atomic<int> helpersFinished_{0};

    void helperLoop(int threadID);
    void helperSearch(ThreadData& td);

public:
    explicit ThreadPool(int threads = 1);
    ~ThreadPool();

    // Non-copyable, non-movable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    Move search(const Position& pos, int maxDepth);
    void stop();
    uint64_t getTotalNodes() const;
    Move getBestMove() const;
    int getNumThreads() const { return numThreads_; }
    void setNumThreads(int threads);
    void clear();
};

// Global thread pool - MUST be destroyed BEFORE globalTT
extern std::unique_ptr<ThreadPool> globalThreadPool;

void initThreadPool(int numThreads = 1);

// Ensure proper destruction order: threads first, then TT
void shutdownEngine();

#endif // THREAD_POOL_H
