#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace db::vm {

// A single runtime value. Mirrors the SQL types plus SQL NULL. Double is a
// computed-only type (produced by AVG); it is never stored in a table.
enum class ValueType { Null, Int, Bool, Text, Double };

struct Value {
    ValueType type = ValueType::Null;
    std::int64_t intValue = 0;
    bool boolValue = false;
    double doubleValue = 0.0;
    std::string textValue;

    static Value null() { return Value{}; }
    static Value makeInt(std::int64_t v) {
        Value x;
        x.type = ValueType::Int;
        x.intValue = v;
        return x;
    }
    static Value makeBool(bool v) {
        Value x;
        x.type = ValueType::Bool;
        x.boolValue = v;
        return x;
    }
    static Value makeText(std::string v) {
        Value x;
        x.type = ValueType::Text;
        x.textValue = std::move(v);
        return x;
    }
    static Value makeDouble(double v) {
        Value x;
        x.type = ValueType::Double;
        x.doubleValue = v;
        return x;
    }

    bool isNull() const { return type == ValueType::Null; }
    std::string toString() const;
};

// Three-valued comparison. Returns nullopt if either operand is NULL; otherwise
// a negative/zero/positive int (like strcmp) for less/equal/greater. Only
// values of the same family (Int/Bool/Text) are compared.
std::optional<int> compareValues(const Value& a, const Value& b);

// Total ordering used by ORDER BY. NULLs sort first (smallest). Mixed types are
// ordered by their ValueType rank, which never happens within one column.
bool valueLess(const Value& a, const Value& b);

}  // namespace db::vm
