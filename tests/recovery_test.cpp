#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "txn/recovery.hpp"
#include "txn/wal.hpp"
#include "vm/storage_engine.hpp"

using namespace db;

namespace {

txn::LogRecord rec(txn::lsn_t lsn, int txnId, txn::LogType type, int tableId,
                   vm::RecordID rid, std::string before, std::string after) {
    txn::LogRecord r;
    r.lsn = lsn;
    r.txnId = txnId;
    r.type = type;
    r.tableId = tableId;
    r.rid = rid;
    r.beforeImage = std::move(before);
    r.afterImage = std::move(after);
    return r;
}

/*
 * Simulates a crash under a no-force buffer policy: a committed transaction's
 * change never reached the heap (must be redone), while a loser transaction's
 * change did reach the heap (must be undone). Recovery must reconcile both.
 */
void run() {
    backend::DiskManager disk("relite_test_recovery.db", /*truncate=*/true);
    backend::BufferPool pool(&disk, /*numFrames=*/8);
    vm::TableManager tables(&pool);
    tables.registerTable(0);

    /* Allocate page 0 with a durable committed row A. */
    vm::RecordID rA = tables.insertTuple(0, "row-A");
    assert(rA.pageId == 0);

    /* A loser transaction's uncommitted row C is already on the heap (steal). */
    vm::RecordID rC{0, 2};
    tables.redoInsert(0, rC, "row-C", 0);
    std::string tmp;
    assert(tables.getTuple(0, rC, tmp) && tmp == "row-C");

    vm::RecordID rB{0, 1};
    std::vector<txn::LogRecord> log = {
        rec(1, 1, txn::LogType::Begin, 0, {}, "", ""),
        rec(2, 1, txn::LogType::Insert, 0, rB, "", "row-B"),
        rec(3, 1, txn::LogType::Commit, 0, {}, "", ""),
        rec(4, 2, txn::LogType::Begin, 0, {}, "", ""),
        rec(5, 2, txn::LogType::Insert, 0, rC, "", "row-C"),
    };

    /* Committed row B is NOT on the heap yet (no-force). */
    assert(!tables.getTuple(0, rB, tmp));

    recovery::recover(log, tables);

    /* Redo restored the committed row B; undo removed the loser row C. */
    assert(tables.getTuple(0, rB, tmp) && tmp == "row-B");
    assert(tables.getTuple(0, rA, tmp) && tmp == "row-A");
    assert(!tables.getTuple(0, rC, tmp));

    /* Redo is idempotent: running recovery again changes nothing. */
    recovery::recover(log, tables);
    assert(tables.getTuple(0, rB, tmp) && tmp == "row-B");
    assert(!tables.getTuple(0, rC, tmp));

    /* Committed delete is redone; loser delete (already applied to the heap via
     * steal) is undone. */
    tables.redoDelete(0, rB, 0);
    assert(!tables.getTuple(0, rB, tmp));
    std::vector<txn::LogRecord> log2 = {
        rec(6, 3, txn::LogType::Begin, 0, {}, "", ""),
        rec(7, 3, txn::LogType::Delete, 0, rA, "row-A", ""),
        rec(8, 3, txn::LogType::Commit, 0, {}, "", ""),
        rec(9, 4, txn::LogType::Begin, 0, {}, "", ""),
        rec(10, 4, txn::LogType::Delete, 0, rB, "row-B", ""),
    };
    recovery::recover(log2, tables);
    assert(!tables.getTuple(0, rA, tmp));
    assert(tables.getTuple(0, rB, tmp) && tmp == "row-B");

    std::remove("relite_test_recovery.db");
    std::cout << "recovery_test passed\n";
}

}  // namespace

int main() {
    run();
    return 0;
}
