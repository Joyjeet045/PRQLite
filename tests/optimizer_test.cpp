#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "vm/optimizer.hpp"
#include "vm/value.hpp"

using namespace db;

namespace {

vm::Row row2(long long a, const std::string& b) {
    return {vm::Value::makeInt(a), vm::Value::makeText(b)};
}

/*
 * Verifies the external merge sort (including the spill path), the sort-merge
 * join, and the cost model's algorithm selection.
 */
void run() {
    /* In-memory sort. */
    {
        std::vector<vm::Row> rows = {row2(3, "c"), row2(1, "a"), row2(2, "b")};
        vm::externalSort(rows, {0}, {true}, 100);
        assert(rows.size() == 3);
        assert(rows[0][0].intValue == 1 && rows[2][0].intValue == 3);
    }

    /* Spilling sort: memLimit far below input size forces run files + k-way
     * merge; result must be fully, stably sorted. */
    {
        std::vector<vm::Row> rows;
        for (int i = 0; i < 2000; ++i) {
            rows.push_back(row2((i * 7919) % 1000, "x"));
        }
        vm::externalSort(rows, {0}, {true}, /*memLimitRows=*/64);
        assert(rows.size() == 2000);
        for (std::size_t i = 1; i < rows.size(); ++i) {
            assert(rows[i - 1][0].intValue <= rows[i][0].intValue);
        }
    }

    /* Descending multi-key spill sort. */
    {
        std::vector<vm::Row> rows;
        for (int i = 0; i < 500; ++i) rows.push_back(row2(i % 10, std::to_string(i)));
        vm::externalSort(rows, {0}, {false}, 32);
        for (std::size_t i = 1; i < rows.size(); ++i) {
            assert(rows[i - 1][0].intValue >= rows[i][0].intValue);
        }
    }

    /* Sort-merge join with duplicate keys (cross product per key group). */
    {
        std::vector<vm::Row> left = {row2(1, "l1"), row2(2, "l2"), row2(2, "l3"),
                                     row2(4, "l4")};
        std::vector<vm::Row> right = {row2(2, "r1"), row2(2, "r2"), row2(3, "r3"),
                                      row2(4, "r4")};
        auto joined = vm::mergeJoinInner(left, 0, right, 0, /*memLimitRows=*/2);
        /* key 2: 2 left x 2 right = 4; key 4: 1 x 1 = 1; total 5. */
        assert(joined.size() == 5);
        for (const auto& r : joined) {
            assert(r.size() == 4);
            assert(r[0].intValue == r[2].intValue);
        }
    }

    /* Cost model: small side -> hash; both large -> merge. */
    {
        vm::CostModel cm;
        cm.hashBuildBudget = 1000;
        assert(cm.chooseEquiJoin(50, 1000000) == vm::JoinAlgorithm::Hash);
        assert(cm.chooseEquiJoin(500000, 800000) == vm::JoinAlgorithm::Merge);
        assert(cm.estimateCost(vm::JoinAlgorithm::NestedLoop, 1000, 1000) >
               cm.estimateCost(vm::JoinAlgorithm::Hash, 1000, 1000));
    }

    std::cout << "optimizer_test passed\n";
}

}  // namespace

int main() {
    run();
    return 0;
}
