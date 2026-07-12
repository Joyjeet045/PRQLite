#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/buffer_pool.hpp"
#include "vm/record_id.hpp"

namespace db::vm {

// Heap-file storage for tables on top of the buffer pool. Each table is a list
// of pages; rows are addressed by RecordID {pageId, slotId}. Row bytes are
// opaque here — (de)serialization against a schema happens in the executors.
class TableManager {
public:
    explicit TableManager(backend::BufferPool* pool) : pool_(pool) {}

    void registerTable(int tableId);
    bool hasTable(int tableId) const;
    void dropTable(int tableId);

    // Inserts a serialized row, allocating a page when needed.
    RecordID insertTuple(int tableId, const std::string& bytes);

    // Reads a live row. Returns false for tombstoned / missing rids.
    bool getTuple(int tableId, const RecordID& rid, std::string& out);

    // Tombstones a row.
    bool eraseTuple(int tableId, const RecordID& rid);

    // Updates a row in place when possible, otherwise relocates it. Returns the
    // (possibly new) RecordID; an invalid RecordID signals failure.
    RecordID updateTuple(int tableId, const RecordID& rid, const std::string& bytes);

    const std::vector<backend::PageId>& pageList(int tableId) const;
    backend::BufferPool* pool() const { return pool_; }

    // Restores a table's page list when loading a persisted database.
    void restorePages(int tableId, std::vector<backend::PageId> pages) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        pages_[tableId] = std::move(pages);
    }

private:
    backend::BufferPool* pool_;
    mutable std::recursive_mutex mutex_;
    std::unordered_map<int, std::vector<backend::PageId>> pages_;
    // Free-space map: approximate free bytes per page, so inserts can skip
    // pages known to be full without fetching them.
    std::unordered_map<backend::PageId, int> pageFree_;
    static const std::vector<backend::PageId> kEmpty;
};

// Forward cursor over the live rows of a table (Volcano-friendly). Pins pages
// only transiently, so it is safe to interleave with writes to other rows.
class TableIterator {
public:
    TableIterator(TableManager* manager, int tableId);

    bool valid() const { return valid_; }
    void next();
    const RecordID& rid() const { return rid_; }
    const std::string& bytes() const { return bytes_; }

private:
    void advanceToLive();

    TableManager* manager_;
    int tableId_;
    std::size_t pageIdx_ = 0;
    int slot_ = -1;
    RecordID rid_;
    std::string bytes_;
    bool valid_ = false;
};

}  // namespace db::vm
