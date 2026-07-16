#pragma once

#include <cstddef>
#include <vector>

#include "vm/value.hpp"

namespace db::vm {

using Row = std::vector<Value>;

/*
 * Query-optimization utilities: cardinality-driven join-algorithm selection,
 * an external merge sort that spills sorted runs to temporary files when the
 * input exceeds a memory budget, and a sort-merge join built on top of it.
 */

enum class JoinAlgorithm { NestedLoop, Hash, Merge };

struct CostModel {
    /* Rows whose smaller join side fits this budget favor an in-memory hash
     * join; larger inputs favor a spill-friendly sort-merge join. */
    std::size_t hashBuildBudget = 10000;

    JoinAlgorithm chooseEquiJoin(std::size_t leftRows, std::size_t rightRows) const;
    double estimateCost(JoinAlgorithm algo, std::size_t leftRows,
                        std::size_t rightRows) const;
};

/*
 * Sort `rows` by the given key columns. When rows.size() exceeds
 * memLimitRows the sort spills sorted runs to temp files and k-way merges
 * them, so it never holds more than one run plus the merge frontier in memory.
 */
void externalSort(std::vector<Row>& rows, const std::vector<int>& keyCols,
                  const std::vector<bool>& ascending, std::size_t memLimitRows);

/* Inner sort-merge equi-join on a single key column from each side. */
std::vector<Row> mergeJoinInner(std::vector<Row> left, int leftKey,
                                std::vector<Row> right, int rightKey,
                                std::size_t memLimitRows = 100000);

}
