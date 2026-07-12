#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

#include "frontend/catalog.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"
#include "txn/lock_manager.hpp"
#include "txn/transaction_manager.hpp"
#include "txn/wal.hpp"
#include "vm/executor_engine.hpp"
#include "vm/storage_engine.hpp"

using namespace db;

namespace {

// A per-session harness: storage + WAL + locks + transaction manager, sharing a
// current-transaction id across statements.
struct Harness {
    vm::StorageEngine se;
    txn::WriteAheadLog wal;
    txn::LockManager locks;
    txn::TransactionManager tm;
    int cur = 0;

    Harness(const std::string& db, const std::string& wl)
        : se(db, true), wal(wl, true), locks(), tm(&wal, &locks) {}

    vm::ResultSet run(const std::string& sql) {
        parser::Lexer lexer(sql);
        parser::Parser parser(lexer.tokenize());
        auto stmt = parser.parseStatement();
        semantic::SemanticAnalyzer analyzer(semantic::Catalog::instance());
        analyzer.analyze(*stmt);
        vm::ExecutorEngine engine(se, semantic::Catalog::instance(), &tm, &cur);
        return engine.run(*stmt);
    }
};

int scalarInt(const vm::ResultSet& rs) {
    assert(rs.rows.size() == 1 && rs.rows[0].size() == 1);
    return static_cast<int>(rs.rows[0][0].intValue);
}

void run() {
    semantic::Catalog::instance().reset();
    Harness h("prqlite_test_p5.db", "prqlite_test_p5.wal");

    h.run("CREATE TABLE emp (id INT, name TEXT, dept TEXT, salary INT);");
    h.run("INSERT INTO emp VALUES (1,'Alice','eng',100),(2,'Bob','eng',200),"
          "(3,'Carol','sales',150),(4,'Dave','sales',NULL);");

    // Aggregates.
    assert(scalarInt(h.run("SELECT COUNT(*) FROM emp;")) == 4);
    assert(scalarInt(h.run("SELECT SUM(salary) FROM emp;")) == 450);  // NULL ignored
    assert(scalarInt(h.run("SELECT MIN(salary) FROM emp;")) == 100);
    assert(scalarInt(h.run("SELECT MAX(salary) FROM emp;")) == 200);
    assert(scalarInt(h.run("SELECT COUNT(salary) FROM emp;")) == 3);  // NULL not counted

    // GROUP BY.
    auto grouped = h.run("SELECT dept, COUNT(*) FROM emp GROUP BY dept;");
    assert(grouped.columns.size() == 2 && grouped.rows.size() == 2);
    assert(grouped.rows[0][0].textValue == "eng" && grouped.rows[0][1].intValue == 2);

    // HAVING (on the grouping column).
    auto having = h.run("SELECT dept FROM emp GROUP BY dept HAVING dept = 'sales';");
    assert(having.rows.size() == 1 && having.rows[0][0].textValue == "sales");

    // NULL handling.
    assert(h.run("SELECT id FROM emp WHERE salary IS NULL;").rows.size() == 1);
    assert(h.run("SELECT id FROM emp WHERE salary IS NOT NULL;").rows.size() == 3);

    // IN / NOT IN.
    assert(h.run("SELECT id FROM emp WHERE id IN (1, 2);").rows.size() == 2);
    assert(h.run("SELECT id FROM emp WHERE id NOT IN (1, 2);").rows.size() == 2);

    // BETWEEN.
    assert(h.run("SELECT id FROM emp WHERE id BETWEEN 2 AND 3;").rows.size() == 2);

    // LIKE.
    auto like = h.run("SELECT name FROM emp WHERE name LIKE 'A%';");
    assert(like.rows.size() == 1 && like.rows[0][0].textValue == "Alice");
    assert(h.run("SELECT name FROM emp WHERE name LIKE '_o%';").rows.size() == 1);  // Bob

    // ORDER BY + LIMIT.
    auto ordered = h.run("SELECT id FROM emp ORDER BY id DESC;");
    assert(ordered.rows.size() == 4 && ordered.rows[0][0].intValue == 4);
    auto limited = h.run("SELECT id FROM emp ORDER BY id ASC LIMIT 2;");
    assert(limited.rows.size() == 2 && limited.rows[0][0].intValue == 1 &&
           limited.rows[1][0].intValue == 2);

    // UPDATE.
    h.run("UPDATE emp SET salary = 175 WHERE id = 4;");
    assert(scalarInt(h.run("SELECT salary FROM emp WHERE id = 4;")) == 175);
    assert(h.run("SELECT id FROM emp WHERE salary IS NULL;").rows.empty());

    // Transactions: rollback an INSERT.
    int before = scalarInt(h.run("SELECT COUNT(*) FROM emp;"));
    h.run("BEGIN;");
    h.run("INSERT INTO emp VALUES (5,'Eve','eng',300);");
    assert(scalarInt(h.run("SELECT COUNT(*) FROM emp;")) == before + 1);
    h.run("ROLLBACK;");
    assert(scalarInt(h.run("SELECT COUNT(*) FROM emp;")) == before);

    // Transactions: rollback a DELETE.
    h.run("BEGIN;");
    h.run("DELETE FROM emp WHERE id = 1;");
    assert(h.run("SELECT id FROM emp WHERE id = 1;").rows.empty());
    h.run("ROLLBACK;");
    assert(h.run("SELECT id FROM emp WHERE id = 1;").rows.size() == 1);

    // Transactions: commit an UPDATE.
    h.run("BEGIN;");
    h.run("UPDATE emp SET dept = 'mgmt' WHERE id = 1;");
    h.run("COMMIT;");
    auto committed = h.run("SELECT dept FROM emp WHERE id = 1;");
    assert(committed.rows[0][0].textValue == "mgmt");

    // INNER JOIN. emp.dept values are now {mgmt, eng, sales, sales}.
    h.run("CREATE TABLE dept (dname TEXT, floor INT);");
    h.run("INSERT INTO dept VALUES ('eng', 3), ('sales', 1), ('mgmt', 5);");
    auto joined =
        h.run("SELECT emp.name, dept.floor FROM emp JOIN dept ON emp.dept = dept.dname;");
    assert(joined.columns.size() == 2 && joined.rows.size() == 4);
    auto joinFiltered = h.run(
        "SELECT emp.name FROM emp INNER JOIN dept ON emp.dept = dept.dname "
        "WHERE dept.floor = 1;");
    assert(joinFiltered.rows.size() == 2);  // both sales employees
    h.run("DROP TABLE dept;");

    // DROP INDEX / DROP TABLE.
    h.run("CREATE INDEX emp_id ON emp (id);");
    h.run("DROP INDEX emp_id;");
    h.run("DROP TABLE emp;");
    assert(!semantic::Catalog::instance().hasTable("emp"));

    semantic::Catalog::instance().reset();
    std::remove("prqlite_test_p5.db");
    std::remove("prqlite_test_p5.wal");
    std::cout << "All Phase V tests passed.\n";
}

}  // namespace

int main() {
    run();
    return 0;
}
