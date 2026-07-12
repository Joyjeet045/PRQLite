#pragma once

#include <string>
#include <vector>

#include "vm/value.hpp"

namespace db::vm {

// The outcome of running a statement: either a query result (column headers +
// rows) or a status message for DML/DDL.
struct ResultSet {
    bool isQuery = false;
    std::vector<std::string> columns;
    std::vector<std::vector<Value>> rows;
    std::string message;
};

}  // namespace db::vm
