# PRQLite

A relational database built from scratch in modern C++ (C++20). PRQLite implements the
full stack of a small single-node RDBMS: a SQL front-end, a Volcano-model execution
engine, a paged storage engine with a buffer pool, B+ tree indexing, and ACID
transactions with write-ahead logging and crash recovery.

It runs as an interactive REPL and persists data across restarts.

## Features

**SQL surface**
- DDL: `CREATE TABLE` / `CREATE INDEX`, `DROP TABLE` / `DROP INDEX`, `ALTER TABLE ADD/DROP COLUMN`
- DML: `INSERT`, `UPDATE`, `DELETE`
- Queries: projection, `WHERE` (`=, !=, <, <=, >, >=`, `AND/OR/NOT`, `IS [NOT] NULL`,
  `[NOT] IN`, `BETWEEN`, `LIKE`), `INNER JOIN ... ON`, `GROUP BY`/`HAVING`, aggregates
  (`COUNT/SUM/AVG/MIN/MAX`), `DISTINCT` / `COUNT(DISTINCT)`, `ORDER BY`, `LIMIT`, and
  uncorrelated scalar/`IN`/`EXISTS` subqueries
- Constraints: `PRIMARY KEY`, `UNIQUE`, `NOT NULL`, `DEFAULT`, `CHECK`, foreign keys
  (`REFERENCES`), and `VARCHAR(n)` length enforcement
- Transactions: `BEGIN` / `COMMIT` / `ROLLBACK`

**Engine internals**
- Hand-written lexer + recursive-descent parser (precedence climbing) → AST (visitor pattern)
- Semantic analyzer binding names/types against a per-database catalog
- Volcano/iterator execution engine with a first-pass optimizer (index range scans) and hash join
- 4 KB slotted pages, disk manager, LRU buffer pool with page guards, page compaction
- Thread-safe B+ tree + Bloom filter indexes
- Write-ahead log with `fsync` durability, group-commit, checkpointing, and crash recovery
- Row-level lock manager (two-phase locking) and a transaction manager with undo

## Build

Requires CMake ≥ 3.16 and a C++20 compiler.

```sh
cmake -S . -B build
cmake --build build
```

## Run

```sh
./build/prqlite        # (build/prqlite.exe on Windows)
```

```sql
prqlite=# CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(16) NOT NULL);
prqlite=# INSERT INTO users VALUES (1, 'ann');
prqlite=# SELECT * FROM users;
```

Type `\h` for help and `\q` to quit.

## Tests

Ten self-contained suites run via CTest:

```sh
ctest --test-dir build --output-on-failure
```

## Project layout

```
include/ , src/
  frontend/   lexer, parser, AST, catalog, semantic analyzer, db/REPL
  vm/         tuple, values, table manager, executor + engine (Volcano)
  backend/    page, disk manager, buffer pool, durability
  index/      B+ tree, bloom filter, index manager
  txn/        WAL, lock manager, transaction manager
tests/        unit + integration suites
docs/         grammar.txt (SQL grammar reference)
```

## Grammar

The accepted SQL subset is documented in [docs/grammar.txt](docs/grammar.txt). The
authoritative grammar is the recursive-descent parser in `src/frontend/parser.cpp`.

## Roadmap

Larger items not yet implemented: ARIES-style redo + `pageLSN`, MVCC / isolation levels,
a paged (disk-backed) B+ tree, a cost-based optimizer with merge join and external-sort
spill, outer/multi-way joins, correlated subqueries, and a `FLOAT` column type.
