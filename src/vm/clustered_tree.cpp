#include "vm/clustered_tree.hpp"

#include <cstdint>
#include <cstring>

#include "backend/page.hpp"
#include "backend/page_guard.hpp"

namespace db::vm {

namespace {

constexpr int kSplitLimit = backend::PAGE_SIZE;

void putU16(char* d, int off, std::uint16_t v) {
    d[off] = static_cast<char>(v & 0xFF);
    d[off + 1] = static_cast<char>((v >> 8) & 0xFF);
}
std::uint16_t getU16(const char* d, int off) {
    return static_cast<std::uint16_t>(
        (static_cast<unsigned char>(d[off])) |
        (static_cast<unsigned char>(d[off + 1]) << 8));
}
void putI32(char* d, int off, std::int32_t v) {
    std::uint32_t u = static_cast<std::uint32_t>(v);
    for (int i = 0; i < 4; ++i) d[off + i] = static_cast<char>((u >> (8 * i)) & 0xFF);
}
std::int32_t getI32(const char* d, int off) {
    std::uint32_t u = 0;
    for (int i = 0; i < 4; ++i)
        u |= static_cast<std::uint32_t>(static_cast<unsigned char>(d[off + i])) << (8 * i);
    return static_cast<std::int32_t>(u);
}
void putI64(char* d, int off, std::int64_t v) {
    std::uint64_t u = static_cast<std::uint64_t>(v);
    for (int i = 0; i < 8; ++i) d[off + i] = static_cast<char>((u >> (8 * i)) & 0xFF);
}
std::int64_t getI64(const char* d, int off) {
    std::uint64_t u = 0;
    for (int i = 0; i < 8; ++i)
        u |= static_cast<std::uint64_t>(static_cast<unsigned char>(d[off + i])) << (8 * i);
    return static_cast<std::int64_t>(u);
}

}  // namespace

/* Serialized size of a node (used to decide when to split). */
static std::size_t measureLeaf(const std::vector<std::string>& vals) {
    std::size_t bytes = 8;
    for (const auto& v : vals) bytes += 8 + 2 + v.size();
    return bytes;
}
static std::size_t measureInternal(std::size_t nkeys) {
    return 8 + nkeys * 8 + (nkeys + 1) * 4;
}

ClusteredTree::NodeView ClusteredTree::readNode(int pageId) const {
    NodeView view;
    backend::Page* page = pool_->fetchPage(pageId);
    backend::PageGuard guard(pool_, pageId, page);
    const char* d = page->data();
    view.leaf = d[0] != 0;
    int n = getU16(d, 2);
    view.nextLeaf = getI32(d, 4);
    int pos = 8;
    if (view.leaf) {
        view.keys.reserve(n);
        view.vals.reserve(n);
        for (int i = 0; i < n; ++i) {
            long long key = getI64(d, pos);
            pos += 8;
            int vlen = getU16(d, pos);
            pos += 2;
            view.keys.push_back(key);
            view.vals.emplace_back(d + pos, static_cast<std::size_t>(vlen));
            pos += vlen;
        }
    } else {
        view.keys.reserve(n);
        for (int i = 0; i < n; ++i) {
            view.keys.push_back(getI64(d, pos));
            pos += 8;
        }
        view.children.reserve(n + 1);
        for (int i = 0; i < n + 1; ++i) {
            view.children.push_back(getI32(d, pos));
            pos += 4;
        }
    }
    return view;
}

void ClusteredTree::writeNode(int pageId, const NodeView& view) {
    backend::Page* page = pool_->fetchPage(pageId);
    backend::PageGuard guard(pool_, pageId, page);
    guard.markDirty();
    char* d = page->data();
    std::memset(d, 0, backend::PAGE_SIZE);
    d[0] = view.leaf ? 1 : 0;
    putU16(d, 2, static_cast<std::uint16_t>(view.keys.size()));
    putI32(d, 4, view.nextLeaf);
    int pos = 8;
    if (view.leaf) {
        for (std::size_t i = 0; i < view.keys.size(); ++i) {
            putI64(d, pos, view.keys[i]);
            pos += 8;
            const std::string& v = view.vals[i];
            putU16(d, pos, static_cast<std::uint16_t>(v.size()));
            pos += 2;
            std::memcpy(d + pos, v.data(), v.size());
            pos += static_cast<int>(v.size());
        }
    } else {
        for (long long k : view.keys) {
            putI64(d, pos, k);
            pos += 8;
        }
        for (int child : view.children) {
            putI32(d, pos, child);
            pos += 4;
        }
    }
}

int ClusteredTree::allocNode(const NodeView& view) {
    backend::PageId pid = -1;
    backend::Page* page = pool_->newPage(pid);
    (void)page;
    pool_->unpin(pid, false);
    pages_.push_back(pid);
    writeNode(pid, view);
    return pid;
}

int ClusteredTree::findLeaf(long long key, std::vector<int>& path) const {
    int cur = rootId_;
    while (true) {
        NodeView view = readNode(cur);
        if (view.leaf) return cur;
        path.push_back(cur);
        int idx = 0;
        int n = static_cast<int>(view.keys.size());
        while (idx < n && key >= view.keys[idx]) ++idx;
        cur = view.children[idx];
    }
}

int ClusteredTree::leftmostLeaf() const {
    int cur = rootId_;
    while (true) {
        NodeView view = readNode(cur);
        if (view.leaf) return cur;
        cur = view.children.front();
    }
}

void ClusteredTree::put(long long key, const std::string& row) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (rootId_ == -1) {
        NodeView root;
        root.leaf = true;
        root.keys.push_back(key);
        root.vals.push_back(row);
        rootId_ = allocNode(root);
        return;
    }

    std::vector<int> path;
    int leafId = findLeaf(key, path);
    NodeView leaf = readNode(leafId);

    int pos = 0;
    int n = static_cast<int>(leaf.keys.size());
    while (pos < n && leaf.keys[pos] < key) ++pos;
    if (pos < n && leaf.keys[pos] == key) {
        leaf.vals[pos] = row;
    } else {
        leaf.keys.insert(leaf.keys.begin() + pos, key);
        leaf.vals.insert(leaf.vals.begin() + pos, row);
    }

    if (measureLeaf(leaf.vals) <= kSplitLimit || leaf.keys.size() <= 1) {
        writeNode(leafId, leaf);
        return;
    }

    std::size_t mid = leaf.keys.size() / 2;
    NodeView right;
    right.leaf = true;
    right.keys.assign(leaf.keys.begin() + mid, leaf.keys.end());
    right.vals.assign(leaf.vals.begin() + mid, leaf.vals.end());
    right.nextLeaf = leaf.nextLeaf;
    leaf.keys.resize(mid);
    leaf.vals.resize(mid);
    int rightId = allocNode(right);
    leaf.nextLeaf = rightId;
    writeNode(leafId, leaf);

    long long upKey = right.keys.front();
    int childId = rightId;

    while (!path.empty()) {
        int parentId = path.back();
        path.pop_back();
        NodeView parent = readNode(parentId);
        int ip = 0;
        int pn = static_cast<int>(parent.keys.size());
        while (ip < pn && parent.keys[ip] < upKey) ++ip;
        parent.keys.insert(parent.keys.begin() + ip, upKey);
        parent.children.insert(parent.children.begin() + ip + 1, childId);

        if (measureInternal(parent.keys.size()) <= kSplitLimit) {
            writeNode(parentId, parent);
            return;
        }

        std::size_t pmid = parent.keys.size() / 2;
        long long sepKey = parent.keys[pmid];
        NodeView pright;
        pright.leaf = false;
        pright.keys.assign(parent.keys.begin() + pmid + 1, parent.keys.end());
        pright.children.assign(parent.children.begin() + pmid + 1,
                               parent.children.end());
        parent.keys.resize(pmid);
        parent.children.resize(pmid + 1);
        int prightId = allocNode(pright);
        writeNode(parentId, parent);

        upKey = sepKey;
        childId = prightId;
    }

    NodeView newRoot;
    newRoot.leaf = false;
    newRoot.keys.push_back(upKey);
    newRoot.children.push_back(rootId_);
    newRoot.children.push_back(childId);
    rootId_ = allocNode(newRoot);
}

bool ClusteredTree::get(long long key, std::string& out) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (rootId_ == -1) return false;
    std::vector<int> path;
    int leafId = findLeaf(key, path);
    NodeView leaf = readNode(leafId);
    for (std::size_t i = 0; i < leaf.keys.size(); ++i) {
        if (leaf.keys[i] == key) {
            out = leaf.vals[i];
            return true;
        }
    }
    return false;
}

bool ClusteredTree::erase(long long key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (rootId_ == -1) return false;
    std::vector<int> path;
    int leafId = findLeaf(key, path);
    NodeView leaf = readNode(leafId);
    for (std::size_t i = 0; i < leaf.keys.size(); ++i) {
        if (leaf.keys[i] == key) {
            leaf.keys.erase(leaf.keys.begin() + i);
            leaf.vals.erase(leaf.vals.begin() + i);
            writeNode(leafId, leaf);
            return true;
        }
    }
    return false;
}

int ClusteredTree::firstLeafPage() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (rootId_ == -1) return -1;
    return leftmostLeaf();
}

void ClusteredTree::readLeaf(int pageId, std::vector<long long>& keys,
                             std::vector<std::string>& vals, int& nextLeaf) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    NodeView leaf = readNode(pageId);
    keys = std::move(leaf.keys);
    vals = std::move(leaf.vals);
    nextLeaf = leaf.nextLeaf;
}

}
