#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

#include "frontend/catalog.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"
#include "vm/executor_engine.hpp"
#include "vm/storage_engine.hpp"

using namespace db;

namespace {

vm::ResultSet exec(vm::StorageEngine& se, const std::string& sql) {
    parser::Lexer lexer(sql);
    parser::Parser parser(lexer.tokenize());
    auto stmt = parser.parseStatement();
    semantic::SemanticAnalyzer analyzer(semantic::Catalog::instance());
    analyzer.analyze(*stmt);
    vm::ExecutorEngine engine(se, semantic::Catalog::instance());
    return engine.run(*stmt);
}

void run() {
    semantic::Catalog::instance().reset();
    std::string path = "prqlite_test_engine.db";
    vm::StorageEngine se(path, /*truncate=*/true);

    exec(se, "CREATE TABLE friend (id INT, name TEXT, active BOOL);");
    exec(se, "INSERT INTO friend VALUES (1, 'alice', TRUE), (2, 'bob', FALSE), (3, 'carol', TRUE);");

    // SELECT *
    auto all = exec(se, "SELECT * FROM friend;");
    assert(all.isQuery);
    assert(all.columns.size() == 3 && all.columns[0] == "id");
    assert(all.rows.size() == 3);

    // Projection + WHERE equality.
    auto one = exec(se, "SELECT name FROM friend WHERE id = 2;");
    assert(one.rows.size() == 1);
    assert(one.columns.size() == 1 && one.columns[0] == "name");
    assert(one.rows[0][0].textValue == "bob");

    // Boolean column predicate + OR.
    auto actives = exec(se, "SELECT id FROM friend WHERE active OR id = 2;");
    assert(actives.rows.size() == 3);

    // Comparison operators.
    auto gt = exec(se, "SELECT id FROM friend WHERE id >= 2;");
    assert(gt.rows.size() == 2);

    // NULL via partial INSERT column list.
    exec(se, "INSERT INTO friend (id, name) VALUES (4, 'dave');");
    auto nullCheck = exec(se, "SELECT active FROM friend WHERE id = 4;");
    assert(nullCheck.rows.size() == 1);
    assert(nullCheck.rows[0][0].isNull());

    // NULL comparisons are never true.
    auto nullCmp = exec(se, "SELECT id FROM friend WHERE active = TRUE;");
    assert(nullCmp.rows.size() == 2);  // rows 1 and 3, not the NULL row

    // DELETE with predicate.
    auto del = exec(se, "DELETE FROM friend WHERE id = 2;");
    assert(del.message == "DELETE 1");
    auto afterDel = exec(se, "SELECT * FROM friend;");
    assert(afterDel.rows.size() == 3);

    // DELETE all.
    exec(se, "DELETE FROM friend;");
    auto empty = exec(se, "SELECT * FROM friend;");
    assert(empty.rows.empty());

    semantic::Catalog::instance().reset();
    std::remove(path.c_str());
    std::cout << "All engine tests passed.\n";
}

}  // namespace

int main() {
    run();
    return 0;
}
