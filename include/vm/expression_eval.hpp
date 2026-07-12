#pragma once

#include "frontend/ast.hpp"
#include "vm/tuple.hpp"
#include "vm/value.hpp"

namespace db::vm {

// Evaluates a bound expression against a row. Uses SQL three-valued logic:
// comparisons/among NULLs yield NULL, and NULL is treated as "not true" by
// predicateTrue. Column references read via their analyzer-assigned index.
Value evalExpression(const parser::Expression& expr, const Tuple& tuple);

// True only when the predicate evaluates to boolean TRUE (NULL/FALSE excluded).
bool predicateTrue(const parser::Expression& expr, const Tuple& tuple);

}  // namespace db::vm
