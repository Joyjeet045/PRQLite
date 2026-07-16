#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/buffer_pool.hpp"
#include "vm/clustered_tree.hpp"
#include "vm/record_id.hpp"

namespace db::vm {

/*
 * InnoDB-style clustered (index-organized) table storage. Each table is a
 * disk-backed B+ tree keyed by a clustered key; the row bytes live in the tree's
 * leaf pages, so there is no separate heap. The clustered key is a hidden,
 * monotonically increasing row id (InnoDB's GEN_CLUST_INDEX / DB_ROW_ID model)
 * carried by a RecordID as {0, rowid}. Secondary indexes reference this clustered
 * key, so a secondary lookup is a two-hop probe: index -> RecordID -> getTuple.
 */
class TableManager {
public:
    explicit TableManager(backend::BufferPool* pool) : pool_(pool) {}

    void registerTable(int tableId);
    bool hasTable(int tableId) const;
    void dropTable(int tableId);
    void truncateTable(int tableId);

    RecordID insertTuple(int tableId, const std::string& bytes);

    bool getTuple(int tableId, const RecordID& rid, std::string& out);

    bool eraseTuple(int tableId, const RecordID& rid);

    RecordID updateTuple(int tableId, const RecordID& rid, const std::string& bytes);

    /* Recovery hooks: place / clear a row at an exact clustered key. Logical redo
     * is naturally idempotent (put replaces, erase is a no-op if absent), so the
     * LSN argument is accepted for interface compatibility but not required. */
    void redoInsert(int tableId, const RecordID& rid, const std::string& bytes,
                    std::uint64_t lsn);
    void redoDelete(int tableId, const RecordID& rid, std::uint64_t lsn);
    void undoInsert(int tableId, const RecordID& rid);
    void undoDelete(int tableId, const RecordID& rid, const std::string& bytes);

    const std::vector<backend::PageId>& pageList(int tableId) const;
    backend::BufferPool* pool() const { return pool_; }

    /* Persistence: restore a table's clustered-tree node pages, root page and
     * row-id counter. */
    void restorePages(int tableId, std::vector<backend::PageId> pages);
    void restoreClustered(int tableId, int rootPage, long long nextRowid);
    int rootPage(int tableId) const;
    long long nextRowidValue(int tableId) const;

    /* Ordered scan support for TableIterator. */
    int firstLeafPage(int tableId) const;
    void readLeaf(int tableId, int pageId, std::vector<long long>& keys,
                  std::vector<std::string>& vals, int& nextLeaf) const;

private:
    struct TableState {
        ClusteredTree tree;
        long long nextRowid = 0;
        explicit TableState(backend::BufferPool* pool) : tree(pool) {}
    };

    TableState* state(int tableId) const;

    backend::BufferPool* pool_;
    mutable std::recursive_mutex mutex_;
    std::unordered_map<int, std::unique_ptr<TableState>> tables_;
    static const std::vector<backend::PageId> kEmpty;
};

class TableIterator {
public:
    TableIterator(TableManager* manager, int tableId);

    bool valid() const { return valid_; }
    void next();
    const RecordID& rid() const { return rid_; }
    const std::string& bytes() const { return bytes_; }

private:
    void loadLeaf(int page);
    void advanceToLive();

    TableManager* manager_;
    int tableId_;
    int nextLeaf_ = -1;
    std::vector<long long> keys_;
    std::vector<std::string> vals_;
    std::size_t idx_ = 0;
    RecordID rid_;
    std::string bytes_;
    bool valid_ = false;
};

}
