#pragma once

#include <memory>
#include <vector>

#include "frontend/ast.hpp"
#include "vm/record_id.hpp"
#include "vm/table_manager.hpp"
#include "vm/tuple.hpp"

namespace db::vm {

// Volcano iterator-model operator: init() then repeated next() until it returns
// false. `outRid` is the source row id (valid for base scans, invalid for
// derived tuples like projections).
class AbstractExecutor {
public:
    virtual ~AbstractExecutor() = default;
    virtual void init() = 0;
    virtual bool next(Tuple& outTuple, RecordID& outRid) = 0;
};

// Sequential scan over a table's live rows.
class SeqScanExecutor : public AbstractExecutor {
public:
    SeqScanExecutor(TableManager* tables, int tableId, Schema schema);
    void init() override;
    bool next(Tuple& outTuple, RecordID& outRid) override;

private:
    TableManager* tables_;
    int tableId_;
    Schema schema_;
    std::unique_ptr<TableIterator> it_;
};

// Passes through only rows for which `predicate` is TRUE.
class FilterExecutor : public AbstractExecutor {
public:
    FilterExecutor(std::unique_ptr<AbstractExecutor> child,
                   const parser::Expression* predicate);
    void init() override;
    bool next(Tuple& outTuple, RecordID& outRid) override;

private:
    std::unique_ptr<AbstractExecutor> child_;
    const parser::Expression* predicate_;
};

// Emits a subset of columns (by index) from its child.
class ProjectionExecutor : public AbstractExecutor {
public:
    ProjectionExecutor(std::unique_ptr<AbstractExecutor> child,
                       std::vector<int> columnIndices);
    void init() override;
    bool next(Tuple& outTuple, RecordID& outRid) override;

private:
    std::unique_ptr<AbstractExecutor> child_;
    std::vector<int> columns_;
};

}  // namespace db::vm
