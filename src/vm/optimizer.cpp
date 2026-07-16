#include "vm/optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <queue>
#include <string>

namespace db::vm {

namespace {

bool rowLess(const Row& a, const Row& b, const std::vector<int>& keyCols,
             const std::vector<bool>& ascending) {
    for (std::size_t i = 0; i < keyCols.size(); ++i) {
        int c = keyCols[i];
        bool asc = i < ascending.size() ? ascending[i] : true;
        const Value& va = a[c];
        const Value& vb = b[c];
        if (valueLess(va, vb)) return asc;
        if (valueLess(vb, va)) return !asc;
    }
    return false;
}

void encodeValue(std::string& out, const Value& v) {
    out.push_back(static_cast<char>(v.type));
    switch (v.type) {
        case ValueType::Int: {
            std::uint64_t u = static_cast<std::uint64_t>(v.intValue);
            for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((u >> (8 * i)) & 0xFF));
            break;
        }
        case ValueType::Double: {
            std::uint64_t u;
            std::memcpy(&u, &v.doubleValue, 8);
            for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((u >> (8 * i)) & 0xFF));
            break;
        }
        case ValueType::Bool:
            out.push_back(v.boolValue ? 1 : 0);
            break;
        case ValueType::Text: {
            std::uint32_t n = static_cast<std::uint32_t>(v.textValue.size());
            for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((n >> (8 * i)) & 0xFF));
            out.append(v.textValue);
            break;
        }
        case ValueType::Null:
            break;
    }
}

std::string encodeRow(const Row& row) {
    std::string out;
    std::uint32_t n = static_cast<std::uint32_t>(row.size());
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((n >> (8 * i)) & 0xFF));
    for (const Value& v : row) encodeValue(out, v);
    return out;
}

std::uint32_t readU32(std::istream& in) {
    std::uint32_t u = 0;
    for (int i = 0; i < 4; ++i) {
        int c = in.get();
        if (c == EOF) return u;
        u |= static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << (8 * i);
    }
    return u;
}

bool decodeRow(std::istream& in, Row& row) {
    int first = in.peek();
    if (first == EOF) return false;
    std::uint32_t ncols = readU32(in);
    row.clear();
    row.reserve(ncols);
    for (std::uint32_t c = 0; c < ncols; ++c) {
        int tag = in.get();
        if (tag == EOF) return false;
        Value v;
        v.type = static_cast<ValueType>(tag);
        switch (v.type) {
            case ValueType::Int: {
                std::uint64_t u = 0;
                for (int i = 0; i < 8; ++i)
                    u |= static_cast<std::uint64_t>(static_cast<unsigned char>(in.get())) << (8 * i);
                v.intValue = static_cast<std::int64_t>(u);
                break;
            }
            case ValueType::Double: {
                std::uint64_t u = 0;
                for (int i = 0; i < 8; ++i)
                    u |= static_cast<std::uint64_t>(static_cast<unsigned char>(in.get())) << (8 * i);
                std::memcpy(&v.doubleValue, &u, 8);
                break;
            }
            case ValueType::Bool:
                v.boolValue = in.get() != 0;
                break;
            case ValueType::Text: {
                std::uint32_t len = readU32(in);
                v.textValue.resize(len);
                if (len > 0) in.read(&v.textValue[0], static_cast<std::streamsize>(len));
                break;
            }
            case ValueType::Null:
                break;
        }
        row.push_back(std::move(v));
    }
    return true;
}

}  // namespace

JoinAlgorithm CostModel::chooseEquiJoin(std::size_t leftRows,
                                        std::size_t rightRows) const {
    if (std::min(leftRows, rightRows) <= hashBuildBudget) return JoinAlgorithm::Hash;
    return JoinAlgorithm::Merge;
}

double CostModel::estimateCost(JoinAlgorithm algo, std::size_t leftRows,
                               std::size_t rightRows) const {
    double l = static_cast<double>(leftRows);
    double r = static_cast<double>(rightRows);
    switch (algo) {
        case JoinAlgorithm::NestedLoop:
            return l * r;
        case JoinAlgorithm::Hash:
            return l + r;
        case JoinAlgorithm::Merge:
            return l * std::log2(l + 2.0) + r * std::log2(r + 2.0) + l + r;
    }
    return l * r;
}

void externalSort(std::vector<Row>& rows, const std::vector<int>& keyCols,
                  const std::vector<bool>& ascending, std::size_t memLimitRows) {
    auto cmp = [&](const Row& a, const Row& b) {
        return rowLess(a, b, keyCols, ascending);
    };

    if (memLimitRows == 0) memLimitRows = 1;
    if (rows.size() <= memLimitRows) {
        std::stable_sort(rows.begin(), rows.end(), cmp);
        return;
    }

    std::vector<std::string> runPaths;
    for (std::size_t start = 0; start < rows.size(); start += memLimitRows) {
        std::size_t end = std::min(start + memLimitRows, rows.size());
        std::stable_sort(rows.begin() + start, rows.begin() + end, cmp);
        std::string path = "relite_sort_run_" + std::to_string(runPaths.size()) + "_" +
                           std::to_string(reinterpret_cast<std::uintptr_t>(&rows)) + ".tmp";
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        for (std::size_t i = start; i < end; ++i) {
            std::string enc = encodeRow(rows[i]);
            out.write(enc.data(), static_cast<std::streamsize>(enc.size()));
        }
        runPaths.push_back(path);
    }

    struct Frontier {
        Row row;
        int run;
    };
    std::vector<std::ifstream> readers;
    readers.reserve(runPaths.size());
    for (const std::string& p : runPaths) readers.emplace_back(p, std::ios::binary);

    auto heapCmp = [&](const Frontier& a, const Frontier& b) {
        return cmp(b.row, a.row);
    };
    std::priority_queue<Frontier, std::vector<Frontier>, decltype(heapCmp)> heap(heapCmp);
    for (int i = 0; i < static_cast<int>(readers.size()); ++i) {
        Row row;
        if (decodeRow(readers[i], row)) heap.push({std::move(row), i});
    }

    std::vector<Row> merged;
    merged.reserve(rows.size());
    while (!heap.empty()) {
        Frontier top = heap.top();
        heap.pop();
        int run = top.run;
        merged.push_back(std::move(top.row));
        Row next;
        if (decodeRow(readers[run], next)) heap.push({std::move(next), run});
    }

    for (auto& r : readers) r.close();
    for (const std::string& p : runPaths) std::remove(p.c_str());
    rows.swap(merged);
}

std::vector<Row> mergeJoinInner(std::vector<Row> left, int leftKey,
                                std::vector<Row> right, int rightKey,
                                std::size_t memLimitRows) {
    externalSort(left, {leftKey}, {true}, memLimitRows);
    externalSort(right, {rightKey}, {true}, memLimitRows);

    std::vector<Row> out;
    std::size_t i = 0, j = 0;
    while (i < left.size() && j < right.size()) {
        const Value& lk = left[i][leftKey];
        const Value& rk = right[j][rightKey];
        if (lk.isNull()) { ++i; continue; }
        if (rk.isNull()) { ++j; continue; }
        if (valueLess(lk, rk)) {
            ++i;
        } else if (valueLess(rk, lk)) {
            ++j;
        } else {
            std::size_t iEnd = i;
            while (iEnd < left.size() && !valueLess(lk, left[iEnd][leftKey]) &&
                   !valueLess(left[iEnd][leftKey], lk)) {
                ++iEnd;
            }
            std::size_t jEnd = j;
            while (jEnd < right.size() && !valueLess(rk, right[jEnd][rightKey]) &&
                   !valueLess(right[jEnd][rightKey], rk)) {
                ++jEnd;
            }
            for (std::size_t a = i; a < iEnd; ++a) {
                for (std::size_t b = j; b < jEnd; ++b) {
                    Row combined = left[a];
                    combined.insert(combined.end(), right[b].begin(), right[b].end());
                    out.push_back(std::move(combined));
                }
            }
            i = iEnd;
            j = jEnd;
        }
    }
    return out;
}

}  // namespace db::vm
