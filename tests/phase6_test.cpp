#include <cassert>
#include <cstdio>
#include <exception>
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

void expectThrow(Harness& h, const std::string& sql) {
    bool threw = false;
    try {
        h.run(sql);
    } catch (const std::exception&) {
        threw = true;
    }
    if (!threw) {
        std::cerr << "Expected error for: " << sql << "\n";
        assert(false);
    }
}

void testForeignKeys(Harness& h) {
    h.run("CREATE TABLE dept (id INT, dname TEXT);");
    h.run("INSERT INTO dept VALUES (1,'eng'),(2,'sales');");
    h.run("CREATE TABLE emp (id INT, name TEXT, dept_id INT REFERENCES dept(id));");
    h.run("INSERT INTO emp VALUES (1,'Alice',1);");
    h.run("INSERT INTO emp VALUES (2,'Bob',2);");

    expectThrow(h, "INSERT INTO emp VALUES (3,'Carol',99);");  // parent 99 missing
    h.run("INSERT INTO emp VALUES (4,'Dave',NULL);");          // NULL FK allowed
    assert(h.run("SELECT id FROM emp;").rows.size() == 3);

    expectThrow(h, "UPDATE emp SET dept_id = 99 WHERE id = 1;");  // FK on update
    expectThrow(h, "DELETE FROM dept WHERE id = 1;");             // referenced by Alice
}

void testSubqueries(Harness& h) {
    // Scalar subquery.
    auto s1 = h.run(
        "SELECT name FROM emp WHERE dept_id = (SELECT id FROM dept WHERE dname = 'sales');");
    assert(s1.rows.size() == 1 && s1.rows[0][0].textValue == "Bob");

    // IN (subquery).
    auto s2 = h.run("SELECT name FROM emp WHERE dept_id IN (SELECT id FROM dept);");
    assert(s2.rows.size() == 2);  // Alice, Bob (Dave's NULL excluded)

    // NOT IN (subquery).
    auto s3 = h.run(
        "SELECT name FROM emp WHERE dept_id NOT IN (SELECT id FROM dept WHERE dname='eng');");
    assert(s3.rows.size() == 1 && s3.rows[0][0].textValue == "Bob");

    // EXISTS (uncorrelated) -> true for all rows.
    auto s4 = h.run("SELECT name FROM emp WHERE EXISTS (SELECT id FROM dept WHERE id = 2);");
    assert(s4.rows.size() == 3);

    // NOT EXISTS -> false for all -> no rows.
    auto s5 =
        h.run("SELECT name FROM emp WHERE NOT EXISTS (SELECT id FROM dept WHERE id = 2);");
    assert(s5.rows.empty());
}

void testAlterTable(Harness& h) {
    h.run("CREATE TABLE t (id INT, name TEXT);");
    h.run("INSERT INTO t VALUES (1,'a'),(2,'b');");

    // ADD COLUMN: existing rows get NULL.
    h.run("ALTER TABLE t ADD COLUMN age INT;");
    auto a1 = h.run("SELECT age FROM t WHERE id = 1;");
    assert(a1.rows.size() == 1 && a1.rows[0][0].isNull());

    h.run("INSERT INTO t VALUES (3,'c',30);");
    auto a2 = h.run("SELECT age FROM t WHERE id = 3;");
    assert(a2.rows[0][0].intValue == 30);

    // DROP COLUMN: schema shrinks.
    h.run("ALTER TABLE t DROP COLUMN name;");
    auto a3 = h.run("SELECT * FROM t WHERE id = 3;");
    assert(a3.columns.size() == 2);
    assert(a3.columns[0] == "id" && a3.columns[1] == "age");
    assert(a3.rows[0][1].intValue == 30);
}

void run() {
    semantic::Catalog::instance().reset();
    Harness h("prqlite_test_p6.db", "prqlite_test_p6.wal");

    testForeignKeys(h);
    testSubqueries(h);
    testAlterTable(h);

    semantic::Catalog::instance().reset();
    std::remove("prqlite_test_p6.db");
    std::remove("prqlite_test_p6.wal");
    std::cout << "All Phase V+ tests passed (subqueries, foreign keys, ALTER).\n";
}

}  // namespace

int main() {
    run();
    return 0;
}
