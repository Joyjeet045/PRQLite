#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "backend/buffer_pool.hpp"

namespace db::vm {

/*
 * A page-resident, disk-backed clustered (index-organized) B+ tree. Each node is
 * one buffer-pool page. Leaves hold the full row bytes inline, sorted by the
 * clustered key, so the table IS the tree -- there is no separate heap. This is
 * the InnoDB model: the primary structure is a B+ tree keyed by the clustered
 * key, and secondary indexes reference that key (a two-hop lookup lands here).
 *
 * Keys are unique 64-bit integers. Deletion is lazy (no node merge). Leaves are
 * forward-linked for ordered scans.
 */
class ClusteredTree {
public:
    explicit ClusteredTree(backend::BufferPool* pool = nullptr) : pool_(pool) {}

    void setBufferPool(backend::BufferPool* pool) { pool_ = pool; }

    /* Insert or replace the row stored under key. */
    void put(long long key, const std::string& row);

    bool get(long long key, std::string& out) const;

    bool erase(long long key);

    /* Ordered iteration support. */
    int firstLeafPage() const;
    void readLeaf(int pageId, std::vector<long long>& keys,
                  std::vector<std::string>& vals, int& nextLeaf) const;

    int root() const { return rootId_; }
    void setRoot(int rootId) { rootId_ = rootId; }

    const std::vector<backend::PageId>& pages() const { return pages_; }
    void setPages(std::vector<backend::PageId> pages) { pages_ = std::move(pages); }

    void clear() {
        rootId_ = -1;
        pages_.clear();
    }

private:
    struct NodeView {
        bool leaf = true;
        int nextLeaf = -1;
        std::vector<long long> keys;
        std::vector<std::string> vals; /* leaf only */
        std::vector<int> children;     /* internal only */
    };

    NodeView readNode(int pageId) const;
    void writeNode(int pageId, const NodeView& view);
    int allocNode(const NodeView& view);
    int findLeaf(long long key, std::vector<int>& path) const;
    int leftmostLeaf() const;

    backend::BufferPool* pool_;
    int rootId_ = -1;
    std::vector<backend::PageId> pages_;
    mutable std::recursive_mutex mutex_;
};

}
