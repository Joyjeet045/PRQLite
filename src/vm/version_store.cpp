#include "vm/version_store.hpp"

namespace db::vm {

void VersionStore::stageInsert(int tableId, const RecordID& rid, std::string bytes) {
    Change c;
    c.tableId = tableId;
    c.isDelete = false;
    c.rid = rid;
    c.bytes = std::move(bytes);
    pending_.push_back(std::move(c));
}

void VersionStore::stageDelete(int tableId, const RecordID& rid) {
    Change c;
    c.tableId = tableId;
    c.isDelete = true;
    c.rid = rid;
    pending_.push_back(std::move(c));
}

std::uint64_t VersionStore::commitPending() {
    if (pending_.empty()) return version_;
    ++version_;
    for (auto& c : pending_) {
        c.version = version_;
        log_.push_back(std::move(c));
    }
    pending_.clear();
    return version_;
}

void VersionStore::discardPending() { pending_.clear(); }

std::vector<std::string> VersionStore::snapshotAsOf(int tableId,
                                                    std::uint64_t version) const {
    std::unordered_map<RecordID, const std::string*> live;
    std::vector<RecordID> order;
    for (const Change& c : log_) {
        if (c.version > version) break;
        if (c.tableId != tableId) continue;
        if (c.isDelete) {
            live.erase(c.rid);
        } else {
            if (live.find(c.rid) == live.end()) order.push_back(c.rid);
            live[c.rid] = &c.bytes;
        }
    }
    std::vector<std::string> out;
    out.reserve(live.size());
    std::unordered_set<RecordID> emitted;
    for (const RecordID& rid : order) {
        if (!emitted.insert(rid).second) continue;
        auto it = live.find(rid);
        if (it != live.end()) out.push_back(*it->second);
    }
    return out;
}

}
