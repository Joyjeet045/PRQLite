#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "vm/record_id.hpp"

namespace db::vm {

/*
 * Append-only multiversion store that powers snapshot reads and AS OF time
 * travel. Every committed write statement (autocommit) or transaction advances
 * a global logical version. Row images are retained as serialized bytes keyed
 * by RecordID; reconstructing a table as of some version folds the change log
 * up to that point. The store lives in memory and covers the changes made in
 * the current session; it sits beside the heap so live reads are unaffected.
 */
class VersionStore {
public:
    void stageInsert(int tableId, const RecordID& rid, std::string bytes);
    void stageDelete(int tableId, const RecordID& rid);

    std::uint64_t commitPending();
    void discardPending();

    std::uint64_t currentVersion() const { return version_; }

    std::vector<std::string> snapshotAsOf(int tableId, std::uint64_t version) const;

private:
    struct Change {
        std::uint64_t version = 0;
        int tableId = 0;
        bool isDelete = false;
        RecordID rid;
        std::string bytes;
    };

    std::vector<Change> log_;
    std::vector<Change> pending_;
    std::uint64_t version_ = 0;
};

}
