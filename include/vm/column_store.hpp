#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "frontend/ast.hpp"
#include "vm/tuple.hpp"
#include "vm/value.hpp"
#include "vm/vectorized.hpp"

namespace db::vm {

class TableManager;

/*
 * A cached, read-optimized columnar copy of a table. Each column is a
 * contiguous typed array plus a null bitmap, so analytical scans touch only the
 * columns they need and vectorized aggregation runs as tight loops over
 * primitive arrays (no per-row tuple decoding). The copy is built lazily from
 * the row heap and invalidated on write, so it accelerates repeated aggregates
 * over an unchanged table.
 */
struct Column {
    parser::DataType type = parser::DataType::Int;
    std::vector<std::int64_t> ints;
    std::vector<double> doubles;
    std::vector<std::uint8_t> bools;
    std::vector<std::string> texts;
    std::vector<std::uint8_t> isNull;
};

struct TableColumns {
    std::vector<Column> columns;
    std::size_t rows = 0;
};

class ColumnStore {
public:
    void invalidate(int tableId);
    void clear() { cache_.clear(); }

    const TableColumns& getOrBuild(int tableId, const Schema& schema,
                                   TableManager& tables);

private:
    std::unordered_map<int, TableColumns> cache_;
};

/* Columnar aggregate over a built table: one Value per aggregate, matching the
 * row engine's semantics. */
std::vector<Value> columnarAggregate(const TableColumns& table,
                                     const std::vector<VecAggregate>& aggregates,
                                     const std::optional<VecPredicate>& predicate);

}
