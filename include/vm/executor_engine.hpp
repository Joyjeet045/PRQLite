#pragma once

#include <string>
#include <utility>
#include <vector>

#include "frontend/ast.hpp"
#include "frontend/catalog.hpp"
#include "txn/transaction_manager.hpp"
#include "vm/record_id.hpp"
#include "vm/result_set.hpp"
#include "vm/storage_engine.hpp"
#include "vm/tuple.hpp"

namespace db::vm {

// Builds and runs a Volcano operator tree for a bound statement, returning a
// ResultSet. Reads the schema from the catalog (populated by the semantic
// analyzer) and data from the storage engine. When a transaction manager and a
// current-transaction pointer are supplied, mutating statements register undo
// actions and BEGIN/COMMIT/ROLLBACK drive the transaction.
class ExecutorEngine : public parser::ASTVisitor {
public:
    ExecutorEngine(StorageEngine& storage, semantic::Catalog& catalog,
                   txn::TransactionManager* txnManager = nullptr,
                   int* currentTxn = nullptr);

    ResultSet run(parser::ASTNode& statement);

    // Statement handlers.
    void visit(parser::CreateStatement& node) override;
    void visit(parser::CreateIdxStatement& node) override;
    void visit(parser::InsertStatement& node) override;
    void visit(parser::SelectStatement& node) override;
    void visit(parser::DeleteStatement& node) override;
    void visit(parser::UpdateStatement& node) override;
    void visit(parser::DropStatement& node) override;
    void visit(parser::AlterStatement& node) override;
    void visit(parser::TransactionStatement& node) override;

    // Expressions are evaluated directly (see expression_eval); unused here.
    void visit(parser::LiteralExpr&) override {}
    void visit(parser::ColumnRef&) override {}
    void visit(parser::BinaryExpr&) override {}
    void visit(parser::ArithmeticExpr&) override {}
    void visit(parser::LogicalExpr&) override {}
    void visit(parser::UnaryExpr&) override {}
    void visit(parser::IsNullExpr&) override {}
    void visit(parser::InExpr&) override {}
    void visit(parser::BetweenExpr&) override {}
    void visit(parser::LikeExpr&) override {}
    void visit(parser::FunctionExpr&) override {}
    void visit(parser::SubqueryExpr&) override {}

private:
    void loadSchema(int tableId, Schema& schema, std::vector<std::string>& names) const;
    bool txnActive() const { return currentTxn_ != nullptr && *currentTxn_ != 0; }

    // Gathers full-width (rid, row) pairs, using an index point lookup when the
    // WHERE clause is a simple equality on an indexed column.
    std::vector<std::pair<RecordID, std::vector<Value>>> gatherRows(
        int tableId, const Schema& schema, parser::Expression* where);

    // Optimizer: attempts to produce candidate rids for `where` from an index on
    // one of its comparison / BETWEEN predicates (descending into AND). Returns
    // true and fills `rids` (a superset that the caller re-checks) when used.
    bool indexCandidates(parser::Expression* where, int tableId,
                         std::vector<RecordID>& rids);

    // Runs uncorrelated subqueries in `expr` once and caches their results.
    void materializeSubqueries(parser::Expression* expr);
    void materializeSubquery(parser::SubqueryExpr* sub);

    // Foreign-key enforcement.
    bool parentHasValue(const std::string& refTable, const std::string& refColumn,
                        const Value& value);
    void checkForeignKeys(const semantic::TableSchema& schema,
                          const std::vector<Value>& row);
    void checkDeleteRestrict(const semantic::TableSchema& schema,
                             const std::vector<Value>& row);

    // Column-constraint enforcement (NOT NULL, VARCHAR length, PRIMARY KEY /
    // UNIQUE). `excludeRid` skips the row being updated in a uniqueness check.
    void enforceConstraints(const semantic::TableSchema& schema, int tableId,
                            const std::vector<Value>& row, const RecordID* excludeRid);
    bool valueExists(int tableId, int columnIndex, const Value& value,
                     const semantic::TableSchema& schema, const RecordID* excludeRid);

    // Acquires a row lock for the current transaction, throwing on timeout so a
    // possible deadlock surfaces as an error instead of hanging.
    void lockOrThrow(const RecordID& rid, bool exclusive);

    StorageEngine& storage_;
    semantic::Catalog& catalog_;
    txn::TransactionManager* txnMgr_;
    int* currentTxn_;
    ResultSet result_;
};

}  // namespace db::vm
