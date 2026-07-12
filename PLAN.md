# PRQLite — Relational Database from Scratch (C++)

Implementation plan mirroring the article
[How I built a Relational Database from scratch in C++](https://medium.com/@cruelkratos/database-internals-cpp-guide-5cf198d657db).

We build a single-node relational database supporting `CREATE`, `INSERT`, `SELECT`,
`DELETE` with a SQL-subset parser, a Volcano-model execution engine, a paged storage
engine with a buffer pool, and ACID transactions + indexes.

---

## SQL Scope

### Core (Phases I–IV, matching the article)

- `CREATE TABLE` — define a table + schema
- `CREATE INDEX` — build a B+ Tree index
- `INSERT` — add rows
- `SELECT` — with `WHERE` (`=`, comparisons, `AND`/`OR`) and column projection
- `DELETE` — with `WHERE`
- **Data types:** `INT`, `BOOL`, `TEXT` / `VARCHAR(n)`

### Extended (Phase V, beyond the article — planned)

- `UPDATE`
- `JOIN` (moving execution beyond single-table seq-scan → filter → projection)
- Aggregations / `GROUP BY` / `ORDER BY` / `LIMIT`
- Subqueries, `ALTER TABLE`, `DROP`, foreign keys, `NULL` handling
- Additional `WHERE` operators and predicate forms

---

## Tech Choices

- **Language:** C++20
- **Build:** CMake
- **Tests:** GoogleTest (or Catch2)
- **Namespaces** (matching the article): `db::parser`, `db::semantic`, `db::memory`,
  `db::table`, `db::backend`, `db::txn`

## Proposed Folder Structure

```
Relational-Database/
├── CMakeLists.txt
├── include/
│   ├── frontend/        (repl, lexer, parser, ast, semantic)
│   ├── virtual_machine/ (executor, operators, table_manager, tuple)
│   ├── backend/         (page, buffer_pool, page_table, disk_manager, guards)
│   └── txn/             (lock_manager, wal, transaction_manager)
├── src/                 (mirrors include/)
├── tests/
└── main.cpp
```

---

## Phase I — The Frontend (REPL + Parser)

- **Part 1.1 — Project skeleton:** CMake, folder layout, a minimal `main.cpp` + `DB`
  class with a `connect()` method, and a working REPL loop (Postgres-like prompt).
- **Part 1.2 — Lexer:** Tokenizer for the SQL subset (keywords, identifiers, literals,
  operators, punctuation). No regex/external libs — hand-written scanner.
- **Part 1.3 — AST + Visitor pattern:** `ASTNode` base with `accept(ASTVisitor&)`,
  statement nodes (`SelectStatement`, `InsertStatement`, `CreateStatement`,
  `DeleteStatement`, `CreateIdxStatement`), expression nodes.
- **Part 1.4 — Recursive-descent parser:** `peek()`, `match()`, `advance()`, `reset()`;
  grammar methods that build the AST (left-recursion-free).
- **Part 1.5 — Catalog + Semantic Analyzer:** Singleton `Catalog` (table IDs, schemas),
  a `SemanticAnalyzer` visitor that binds table/column info into nodes and validates.

## Phase II — The Virtual Machine (Execution)

- **Part 2.1 — Tuple:** Fixed-layout byte-packed tuple (INT/BOOL/TEXT),
  serialize/deserialize.
- **Part 2.2 — TableManager + TableIterator:** Iterator-based reads/writes over pages,
  `createTuple`, page allocation.
- **Part 2.3 — AbstractExecutor + operators:** `init()` / `next()` interface;
  `SelectOperator` (seq scan), `FilterOperator` (WHERE eval), `ProjectionOperator`,
  `InsertOperator`, `DeleteOperator`, `CreateOperator`.
- **Part 2.4 — ExecutorEngine:** `ASTVisitor` that builds the operator tree per
  statement type and runs it.

## Phase III — The Backend (Storage Engine)

- **Part 3.1 — Slotted Page:** 4KB page, header slots (offset+size) growing up, tuple
  data growing down, free-space check.
- **Part 3.2 — Disk Manager:** OS-independent read/write of pages to a `.db` file at
  page offsets.
- **Part 3.3 — Page Table:** Thread-safe concurrent hashmap (page_id → frame).
- **Part 3.4 — Buffer Pool:** Frames (page, pin count, dirty bit, frame id), free list,
  `fetchPage`/`writePage`, swappable `Replacer` (start with Random Eviction), dirty
  write-back on eviction.
- **Part 3.5 — Read/Write Guards:** RAII guards using shared/unique mutexes for safe
  concurrent access; wire the storage engine into TableManager.

## Phase IV — Filters, Indexes & ACIDity

- **Part 4.1 — Lock Manager:** Row-level `RecordID` locking (shared/exclusive) with
  `std::mutex` + `condition_variable`.
- **Part 4.2 — Write-Ahead Log (WAL):** Append-only `LogRecord`s with `lsn_t`,
  replay-on-recovery for durability.
- **Part 4.3 — Transaction Manager:** `begin()` / `commit()` / `rollback()` tying guards
  + locks + WAL together, with undo lambdas.
- **Part 4.4 — Bloom Filter:** Lock-free `std::atomic<uint64_t>` bit array, k hash
  functions, membership skip.
- **Part 4.5 — B+ Tree Index:** Insert/lookup/delete + range scans; wire into
  `CREATE INDEX` and the executor.

## Phase V — Extended SQL (beyond the article)

Built on the same lexer → parser → semantic → executor pipeline. Each part extends
the grammar, adds/updates AST nodes, semantic binding, and new Volcano operators.

- **Part 5.1 — `UPDATE`:** Grammar + `UpdateStatement` node, `UpdateOperator`
  (scan → filter → write-back), `SET` clause evaluation, WAL/undo integration.
- **Part 5.2 — Richer `WHERE`:** More operators/predicates (`!=`, `<=`, `>=`, `<`,
  `>`, `NOT`, `IN`, `LIKE`, `BETWEEN`) and full boolean expression trees.
- **Part 5.3 — `NULL` handling:** Nullable columns, null bitmap in the tuple layout,
  three-valued logic in filter evaluation.
- **Part 5.4 — `JOIN`:** `JoinOperator` (nested-loop first, then hash join),
  multi-table `FROM`, qualified column resolution in the semantic analyzer.
- **Part 5.5 — Aggregations + `GROUP BY`:** `AggregateOperator`
  (`COUNT`/`SUM`/`AVG`/`MIN`/`MAX`), grouping, `HAVING`.
- **Part 5.6 — `ORDER BY` + `LIMIT`:** `SortOperator` and `LimitOperator`.
- **Part 5.7 — Subqueries:** Scalar + `IN`/`EXISTS` subqueries in `WHERE`/`FROM`.
- **Part 5.8 — DDL extras:** `ALTER TABLE`, `DROP TABLE`/`DROP INDEX`,
  catalog + on-disk schema migration.
- **Part 5.9 — Foreign keys:** `REFERENCES` constraints, referential-integrity checks
  on insert/update/delete.

---

## How We'll Work

- One part per step. Implement it, build & test it, then move to the next.
- Each part is self-contained and leaves the project in a compiling, runnable state.

## Progress Tracker

| Phase | Part | Status |
| ----- | ---- | ------ |
| I     | 1.1 Project skeleton + REPL      | ✅ |
| I     | 1.2 Lexer                        | ✅ |
| I     | 1.3 AST + Visitor                | ✅ |
| I     | 1.4 Recursive-descent parser     | ✅ |
| I     | 1.5 Catalog + Semantic Analyzer  | ✅ |
| II    | 2.1 Tuple                        | ✅ |
| II    | 2.2 TableManager + TableIterator | ✅ |
| II    | 2.3 AbstractExecutor + operators | ✅ |
| II    | 2.4 ExecutorEngine               | ✅ |
| III   | 3.1 Slotted Page                 | ✅ |
| III   | 3.2 Disk Manager                 | ✅ |
| III   | 3.3 Page Table                   | ✅ (in buffer pool) |
| III   | 3.4 Buffer Pool                  | ✅ (LRU replacer) |
| III   | 3.5 Read/Write Guards            | ✅ (PageGuard RAII) |
| IV    | 4.1 Lock Manager                 | ✅ |
| IV    | 4.2 Write-Ahead Log (WAL)        | ✅ |
| IV    | 4.3 Transaction Manager          | ✅ |
| IV    | 4.4 Bloom Filter                 | ✅ |
| IV    | 4.5 B+ Tree Index                | ✅ |
| V     | 5.1 UPDATE                       | ✅ |
| V     | 5.2 Richer WHERE                 | ✅ (IN/BETWEEN/LIKE/IS NULL) |
| V     | 5.3 NULL handling                | ✅ |
| V     | 5.4 JOIN                         | ✅ (INNER, nested loop) |
| V     | 5.5 Aggregations + GROUP BY      | ✅ (+ HAVING) |
| V     | 5.6 ORDER BY + LIMIT             | ✅ |
| V     | 5.7 Subqueries                   | ✅ (scalar/IN/EXISTS, uncorrelated) |
| V     | 5.8 DDL extras (ALTER/DROP)      | ✅ (ADD/DROP COLUMN, DROP TABLE/INDEX) |
| V     | 5.9 Foreign keys                 | ✅ (REFERENCES; insert/update/delete checks) |

## Test suites

`ctest --test-dir build` — 10 suites, all green:
lexer, parser, semantic, storage (value/tuple/page/disk/buffer pool/guard),
table_manager, engine (CRUD + NULL), index (bloom/B+ tree/indexed queries),
txn (locks/WAL/transactions), phase5 (UPDATE/aggregates/GROUP BY/ORDER BY/LIMIT/
IN/BETWEEN/LIKE/IS NULL/JOIN/DROP/transaction rollback), phase6 (subqueries/
foreign keys/ALTER TABLE).

## References

1. [PRQLite GitHub Repository](https://github.com/cruelkratos/PRQLite)
2. [Bloom Filter GitHub Repository](https://github.com/cruelkratos/multithreaded-bloom-filters/)
3. [Reference SQL Grammar](https://github.com/cruelkratos/PRQLite/blob/main/grammar_rules)
4. [CMU Intro to Database Systems](https://youtube.com/playlist?list=PLSE8ODhjZXjYMAgsGH-GtY5rJYZ6zjsd5)
5. [Volcano Iterator Model](https://paperhub.s3.amazonaws.com/dace52a42c07f7f8348b08dc2b186061.pdf)
6. [Slotted Pages](https://kenwagatsuma.com/blog/postgresql-slotted-pages)
