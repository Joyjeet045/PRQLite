#include "txn/recovery.hpp"

#include <unordered_set>

namespace db::recovery {

void recover(const std::vector<txn::LogRecord>& records, vm::TableManager& tables) {
    std::unordered_set<int> committed;
    for (const txn::LogRecord& r : records) {
        if (r.type == txn::LogType::Commit) committed.insert(r.txnId);
    }

    /* Redo committed changes in log (LSN) order. */
    for (const txn::LogRecord& r : records) {
        if (committed.count(r.txnId) == 0) continue;
        if (r.type == txn::LogType::Insert) {
            tables.redoInsert(r.tableId, r.rid, r.afterImage, r.lsn);
        } else if (r.type == txn::LogType::Delete) {
            tables.redoDelete(r.tableId, r.rid, r.lsn);
        }
    }

    /* Undo losers in reverse order. */
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        const txn::LogRecord& r = *it;
        if (committed.count(r.txnId) != 0) continue;
        if (r.type == txn::LogType::Insert) {
            tables.undoInsert(r.tableId, r.rid);
        } else if (r.type == txn::LogType::Delete) {
            tables.undoDelete(r.tableId, r.rid, r.beforeImage);
        }
    }
}

}
