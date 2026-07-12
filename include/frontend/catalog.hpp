#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "frontend/ast.hpp"

namespace db::semantic {

// A single column's schema entry.
struct ColumnSchema {
    std::string name;
    parser::DataType type = parser::DataType::Int;
    int varcharLength = 0;

    // Constraints.
    bool notNull = false;
    bool primaryKey = false;
    bool unique = false;
    bool hasDefault = false;
    parser::CachedValue defaultValue;  // literal default (valid when hasDefault)

    // CHECK (expr): the bound expression (for evaluation) plus its source text
    // (for persistence). checkExpr is null when there is no CHECK.
    std::shared_ptr<parser::Expression> checkExpr;
    std::string checkSource;
};

// A foreign-key constraint: this table's column references a parent column.
struct ForeignKey {
    int columnIndex = -1;
    std::string refTable;
    std::string refColumn;
};

// Full schema for a table plus its assigned id.
struct TableSchema {
    int tableId = -1;
    std::string name;
    std::vector<ColumnSchema> columns;
    std::vector<ForeignKey> foreignKeys;

    // Returns the index of `column` within this schema, or -1 if absent.
    int columnIndex(const std::string& column) const;
};

// Registry of tables and indexes (the article's db::semantic design). Owns
// table ids and schemas: CREATE statements write into it and the semantic
// analyzer reads from it to bind and validate references. A process-wide
// instance is available via instance(), but each DB owns its own Catalog so
// that multiple databases in one process stay independent.
class Catalog {
public:
    Catalog() = default;

    static Catalog& instance();

    // Registers a new table. Returns false (without modifying state) if the
    // name already exists; otherwise assigns a fresh id via `outTableId`.
    bool createTable(const std::string& name,
                     const std::vector<ColumnSchema>& columns,
                     int& outTableId);

    bool hasTable(const std::string& name) const;
    const TableSchema* getTable(const std::string& name) const;
    const TableSchema* getTableById(int tableId) const;
    std::vector<const TableSchema*> allTables() const;

    // Removes a table (and any indexes defined on it). Returns false if absent.
    bool dropTable(const std::string& name);

    // Schema mutation (ALTER TABLE) and foreign keys.
    bool addColumn(const std::string& table, const ColumnSchema& column);
    bool dropColumn(const std::string& table, const std::string& column);
    bool addForeignKey(const std::string& table, int columnIndex,
                       const std::string& refTable, const std::string& refColumn);

    // Registers an index. Returns false on a duplicate index name or an
    // unknown table/column.
    bool createIndex(const std::string& indexName, const std::string& table,
                     const std::string& column);
    bool hasIndex(const std::string& indexName) const;
    bool dropIndex(const std::string& indexName);

    // Test / REPL helper: wipes all registered tables and indexes.
    void reset();

    // --- Persistence support ---
    // Snapshot of one index for saving to disk.
    struct IndexRef {
        std::string name;
        std::string table;
        std::string column;
    };
    std::vector<IndexRef> allIndexes() const;

    int nextTableId() const { return nextTableId_; }
    void setNextTableId(int value) { nextTableId_ = value; }

    // Reinserts a table with its stored id (used when loading from disk).
    void restoreTable(const TableSchema& schema);

    // Attaches a (re-parsed, bound) CHECK expression to a stored column after
    // loading. Returns false if the table/column is unknown.
    bool setColumnCheckExpr(const std::string& table, int columnIndex,
                            std::shared_ptr<parser::Expression> expr);

private:
    int nextTableId_ = 0;
    std::unordered_map<std::string, TableSchema> tables_;
    std::unordered_map<int, std::string> tableNamesById_;
    std::unordered_map<std::string, std::pair<std::string, std::string>> indexes_;
};

}  // namespace db::semantic
