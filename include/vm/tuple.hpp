#pragma once

#include <string>
#include <vector>

#include "frontend/ast.hpp"
#include "vm/value.hpp"

namespace db::vm {

// Column types of a table, in order. Drives (de)serialization.
using Schema = std::vector<parser::DataType>;

// A row: an ordered list of Values, byte-packable against a Schema.
//
// On-disk layout:
//   [ null bitmap : ceil(n/8) bytes ]
//   for each non-null column, in order:
//     INT           -> 8 bytes (host order)
//     BOOL          -> 1 byte
//     TEXT/VARCHAR  -> 4-byte length + raw bytes
class Tuple {
public:
    Tuple() = default;
    explicit Tuple(std::vector<Value> values) : values_(std::move(values)) {}

    const std::vector<Value>& values() const { return values_; }
    std::vector<Value>& values() { return values_; }
    const Value& at(std::size_t i) const { return values_[i]; }
    std::size_t size() const { return values_.size(); }

    std::string serialize(const Schema& schema) const;
    static Tuple deserialize(const std::string& bytes, const Schema& schema);

private:
    std::vector<Value> values_;
};

}  // namespace db::vm
