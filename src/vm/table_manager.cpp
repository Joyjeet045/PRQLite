#include "vm/table_manager.hpp"

namespace db::vm {

const std::vector<backend::PageId> TableManager::kEmpty;

namespace {

/* A RecordID is the row's clustered key: a data row is {0, rowid}, so the key is
 * simply the rowid. The 64-bit packing keeps room for the recovery test's
 * hand-built {page, slot} ids while staying a unique integer key. */
long long keyOf(const RecordID& rid) {
    return (static_cast<long long>(static_cast<unsigned int>(rid.pageId)) << 32) |
           static_cast<unsigned int>(rid.slotId);
}

RecordID ridOf(long long key) {
    RecordID r;
    r.pageId =
        static_cast<int>((static_cast<unsigned long long>(key) >> 32) & 0xffffffffULL);
    r.slotId = static_cast<int>(static_cast<unsigned long long>(key) & 0xffffffffULL);
    return r;
}

}  // namespace

TableManager::TableState* TableManager::state(int tableId) const {
    auto it = tables_.find(tableId);
    return it == tables_.end() ? nullptr : it->second.get();
}

void TableManager::registerTable(int tableId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (tables_.count(tableId) == 0) {
        tables_.emplace(tableId, std::make_unique<TableState>(pool_));
    }
}

bool TableManager::hasTable(int tableId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return tables_.count(tableId) != 0;
}

void TableManager::dropTable(int tableId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    tables_.erase(tableId);
}

void TableManager::truncateTable(int tableId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    TableState* st = state(tableId);
    if (st == nullptr) return;
    st->tree.clear();
    st->nextRowid = 0;
}

const std::vector<backend::PageId>& TableManager::pageList(int tableId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    TableState* st = state(tableId);
    return st == nullptr ? kEmpty : st->tree.pages();
}

void TableManager::restorePages(int tableId, std::vector<backend::PageId> pages) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    TableState* st = state(tableId);
    if (st == nullptr) return;
    st->tree.setPages(std::move(pages));
}

void TableManager::restoreClustered(int tableId, int rootPage, long long nextRowid) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    TableState* st = state(tableId);
    if (st == nullptr) return;
    st->tree.setRoot(rootPage);
    st->nextRowid = nextRowid;
}

int TableManager::rootPage(int tableId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    TableState* st = state(tableId);
    return st == nullptr ? -1 : st->tree.root();
}

long long TableManager::nextRowidValue(int tableId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    TableState* st = state(tableId);
    return st == nullptr ? 0 : st->nextRowid;
}

RecordID TableManager::insertTuple(int tableId, const std::string& bytes) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    TableState* st = state(tableId);
    if (st == nullptr) {
        registerTable(tableId);
        st = state(tableId);
    }
    RecordID rid{0, static_cast<int>(st->nextRowid)};
    ++st->nextRowid;
    st->tree.put(keyOf(rid), bytes);
    return rid;
}

bool TableManager::getTuple(int tableId, const RecordID& rid, std::string& out) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    TableState* st = state(tableId);
    if (st == nullptr) return false;
    return st->tree.get(keyOf(rid), out);
}

bool TableManager::eraseTuple(int tableId, const RecordID& rid) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    TableState* st = state(tableId);
    if (st == nullptr) return false;
    return st->tree.erase(keyOf(rid));
}

RecordID TableManager::updateTuple(int tableId, const RecordID& rid,
                                   const std::string& bytes) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    TableState* st = state(tableId);
    if (st == nullptr) return RecordID{};
    /* The clustered key never changes on update, so the identity is stable. */
    st->tree.put(keyOf(rid), bytes);
    return rid;
}

void TableManager::redoInsert(int tableId, const RecordID& rid,
                              const std::string& bytes, std::uint64_t lsn) {
    (void)lsn;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (state(tableId) == nullptr) registerTable(tableId);
    TableState* st = state(tableId);
    st->tree.put(keyOf(rid), bytes);
    long long key = keyOf(rid);
    if (key + 1 > st->nextRowid) st->nextRowid = key + 1;
}

void TableManager::redoDelete(int tableId, const RecordID& rid, std::uint64_t lsn) {
    (void)lsn;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    TableState* st = state(tableId);
    if (st == nullptr) return;
    st->tree.erase(keyOf(rid));
}

void TableManager::undoInsert(int tableId, const RecordID& rid) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    TableState* st = state(tableId);
    if (st == nullptr) return;
    st->tree.erase(keyOf(rid));
}

void TableManager::undoDelete(int tableId, const RecordID& rid,
                              const std::string& bytes) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (state(tableId) == nullptr) registerTable(tableId);
    TableState* st = state(tableId);
    st->tree.put(keyOf(rid), bytes);
    long long key = keyOf(rid);
    if (key + 1 > st->nextRowid) st->nextRowid = key + 1;
}

int TableManager::firstLeafPage(int tableId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    TableState* st = state(tableId);
    return st == nullptr ? -1 : st->tree.firstLeafPage();
}

void TableManager::readLeaf(int tableId, int pageId, std::vector<long long>& keys,
                            std::vector<std::string>& vals, int& nextLeaf) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    TableState* st = state(tableId);
    if (st == nullptr) {
        keys.clear();
        vals.clear();
        nextLeaf = -1;
        return;
    }
    st->tree.readLeaf(pageId, keys, vals, nextLeaf);
}

TableIterator::TableIterator(TableManager* manager, int tableId)
    : manager_(manager), tableId_(tableId) {
    loadLeaf(manager_->firstLeafPage(tableId_));
    advanceToLive();
}

void TableIterator::loadLeaf(int page) {
    if (page < 0) {
        keys_.clear();
        vals_.clear();
        nextLeaf_ = -1;
        idx_ = 0;
        return;
    }
    manager_->readLeaf(tableId_, page, keys_, vals_, nextLeaf_);
    idx_ = 0;
}

void TableIterator::next() {
    if (!valid_) return;
    ++idx_;
    advanceToLive();
}

void TableIterator::advanceToLive() {
    while (true) {
        if (idx_ < keys_.size()) {
            rid_ = ridOf(keys_[idx_]);
            bytes_ = vals_[idx_];
            valid_ = true;
            return;
        }
        if (nextLeaf_ < 0) {
            valid_ = false;
            return;
        }
        loadLeaf(nextLeaf_);
    }
}

}
