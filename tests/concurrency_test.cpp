#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>
#include <vector>

#include "txn/lock_manager.hpp"
#include "vm/record_id.hpp"

using namespace db;
using db::txn::LockMode;

namespace {

/*
 * Exercises the reader/writer lock manager under real threads: an exclusive
 * (writer) lock waits while a shared (reader) lock is held and is granted only
 * after it is released; many readers share a row concurrently; and writers
 * mutually exclude one another. (MVCC snapshot isolation, where readers take no
 * locks at all, is covered in features_test.)
 */

void testWriterWaitsForReaders() {
    txn::LockManager lm;
    vm::RecordID rid{0, 0};

    assert(lm.lock(1, rid, LockMode::Shared));  /* reader txn 1 holds shared */

    std::atomic<bool> writerAcquired{false};
    std::thread writer([&] {
        lm.lock(2, rid, LockMode::Exclusive);  /* blocks until reader releases */
        writerAcquired.store(true);
    });

    /* While the reader holds the shared lock, the writer cannot proceed. */
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(!writerAcquired.load());

    lm.unlock(1, rid);  /* release the reader */
    writer.join();
    assert(writerAcquired.load());  /* writer granted only after the reader left */
    lm.unlockAll(2);
}

void testManyConcurrentReaders() {
    txn::LockManager lm;
    vm::RecordID rid{1, 0};
    const int n = 16;
    std::atomic<int> granted{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < n; ++i) {
        threads.emplace_back([&, i] {
            if (lm.lock(i + 1, rid, LockMode::Shared)) granted.fetch_add(1);
        });
    }
    for (auto& t : threads) t.join();

    assert(granted.load() == n);  /* every reader coexists on the same row */
    for (int i = 0; i < n; ++i) lm.unlockAll(i + 1);
}

void testWritersSerialize() {
    txn::LockManager lm;
    vm::RecordID rid{2, 0};
    std::atomic<int> concurrent{0};
    std::atomic<int> maxConcurrent{0};
    const int n = 8;

    std::vector<std::thread> threads;
    for (int i = 0; i < n; ++i) {
        threads.emplace_back([&, i] {
            lm.lock(i + 1, rid, LockMode::Exclusive);
            int c = concurrent.fetch_add(1) + 1;
            int prev = maxConcurrent.load();
            while (c > prev && !maxConcurrent.compare_exchange_weak(prev, c)) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            concurrent.fetch_sub(1);
            lm.unlockAll(i + 1);
        });
    }
    for (auto& t : threads) t.join();

    assert(maxConcurrent.load() == 1);  /* never two writers on the row at once */
}

}  // namespace

int main() {
    testWriterWaitsForReaders();
    testManyConcurrentReaders();
    testWritersSerialize();
    std::cout << "concurrency_test passed\n";
    return 0;
}
