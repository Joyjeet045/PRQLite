#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "backend/buffer_pool.hpp"
#include "backend/disk_manager.hpp"
#include "vm/table_manager.hpp"
#include "vm/tuple.hpp"
#include "vm/value.hpp"

using namespace db;

namespace {

vm::Schema schema = {parser::DataType::Int, parser::DataType::Text};

std::string row(int id, const std::string& name) {
    return vm::Tuple({vm::Value::makeInt(id), vm::Value::makeText(name)}).serialize(schema);
}

int countRows(vm::TableManager& tm, int tableId) {
    int n = 0;
    for (vm::TableIterator it(&tm, tableId); it.valid(); it.next()) {
        ++n;
    }
    return n;
}

void run() {
    std::string path = "prqlite_test_tm.db";
    backend::DiskManager disk(path, /*truncate=*/true);
    backend::BufferPool pool(&disk, /*numFrames=*/4);
    vm::TableManager tm(&pool);
    tm.registerTable(0);

    // Insert three rows and read them back.
    vm::RecordID r0 = tm.insertTuple(0, row(1, "alice"));
    vm::RecordID r1 = tm.insertTuple(0, row(2, "bob"));
    vm::RecordID r2 = tm.insertTuple(0, row(3, "carol"));
    assert(r0.valid() && r1.valid() && r2.valid());
    assert(countRows(tm, 0) == 3);

    std::string bytes;
    assert(tm.getTuple(0, r1, bytes));
    assert(vm::Tuple::deserialize(bytes, schema).at(1).textValue == "bob");

    // Erase one; iteration and point lookup reflect it.
    assert(tm.eraseTuple(0, r1));
    assert(!tm.getTuple(0, r1, bytes));
    assert(countRows(tm, 0) == 2);

    // Update in place (same size id) keeps the rid.
    vm::RecordID same = tm.updateTuple(0, r0, row(9, "alice"));
    assert(same == r0);
    assert(tm.getTuple(0, r0, bytes));
    assert(vm::Tuple::deserialize(bytes, schema).at(0).intValue == 9);

    // Update that grows relocates to a new rid.
    vm::RecordID moved = tm.updateTuple(0, r0, row(9, std::string(300, 'x')));
    assert(moved.valid());
    assert(countRows(tm, 0) == 2);

    // Spill across many pages.
    for (int i = 0; i < 300; ++i) {
        tm.insertTuple(0, row(1000 + i, "name" + std::to_string(i)));
    }
    assert(tm.pageList(0).size() > 1);
    assert(countRows(tm, 0) == 302);

    std::remove(path.c_str());
    std::cout << "All table manager tests passed.\n";
}

}  // namespace

int main() {
    run();
    return 0;
}
