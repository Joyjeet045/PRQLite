#pragma once

#include <memory>
#include <string>

#include "frontend/catalog.hpp"

namespace db {

namespace vm {
class StorageEngine;
}
namespace txn {
class WriteAheadLog;
class LockManager;
class TransactionManager;
}

// Top-level database handle. Exposes the query entry point used by the REPL
// (connect) and the interactive loop itself (run).
class DB {
public:
    DB();
    ~DB();

    // Executes a single SQL statement and returns a textual result: a formatted
    // table for queries, or a status line for DML/DDL and errors. Runs the full
    // pipeline: lex -> parse -> semantic analysis -> Volcano execution, with
    // transaction support (BEGIN/COMMIT/ROLLBACK).
    std::string connect(const std::string& query);

    // Runs the interactive Read-Eval-Print Loop until EOF or a quit command.
    void run();

private:
    std::unique_ptr<vm::StorageEngine> storage_;
    std::unique_ptr<txn::WriteAheadLog> wal_;
    std::unique_ptr<txn::LockManager> locks_;
    std::unique_ptr<txn::TransactionManager> txnMgr_;
    semantic::Catalog catalog_;
    int currentTxn_ = 0;

    // Persist / restore the catalog + table page lists to a metadata sidecar so
    // data survives across restarts.
    void saveCatalog();
    void loadCatalog();

    // Recreates and backfills indexes from the current table contents. Called
    // after the catalog + data are loaded (and any recovery has run).
    void rebuildIndexes();

    // Replays the write-ahead log after loading: undoes the effects of any
    // transaction that did not commit, then checkpoints and resets the log.
    void recover();
};

}  // namespace db
