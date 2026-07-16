#pragma once

#include <vector>

#include "txn/wal.hpp"
#include "vm/table_manager.hpp"

namespace db::recovery {

/*
 * ARIES-style recovery over a write-ahead log: an analysis pass finds the
 * committed transactions, a redo pass idempotently replays every committed
 * change in log order (so committed work survives a no-force buffer policy),
 * and an undo pass rolls back the losers in reverse order.
 */
void recover(const std::vector<txn::LogRecord>& records, vm::TableManager& tables);

}
