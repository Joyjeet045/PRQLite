#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace db::parser {

class ASTVisitor;
class SelectStatement;
class SubqueryExpr;

// ---------------------------------------------------------------------------
// Shared value / type enums
// ---------------------------------------------------------------------------

// SQL column data types supported by the subset.
enum class DataType {
    Int,
    Bool,
    Text,
    Varchar,
    Float,
};

std::string_view dataTypeName(DataType type);
// Comparison operators usable inside a WHERE predicate.
enum class ComparisonOp {
    Eq,
    Neq,
    Lt,
    Leq,
    Gt,
    Geq,
};

std::string_view comparisonOpName(ComparisonOp op);

// Reconstructs SQL source text for an expression (fully parenthesized so it
// re-parses identically). Used to persist CHECK constraints. Throws for
// expression kinds not allowed in a CHECK (subqueries, aggregates).
std::string expressionToString(const class Expression& e);

// Logical connectives for boolean expression trees.
enum class LogicalOp {
    And,
    Or,
};

// Arithmetic operators for numeric expression trees.
enum class ArithmeticOp {
    Add,
    Sub,
    Mul,
    Div,
};

// A scalar value captured from a subquery result. Kept here (rather than
// reusing vm::Value) so the AST stays independent of the execution layer.
struct CachedValue {
    enum class Kind { Null, Int, Bool, Text, Float };
    Kind kind = Kind::Null;
    std::int64_t intValue = 0;
    bool boolValue = false;
    double doubleValue = 0.0;
    std::string stringValue;
};

// ---------------------------------------------------------------------------
// Base node
// ---------------------------------------------------------------------------

// Root of the AST hierarchy. Every node accepts a visitor for double dispatch.
class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor& visitor) = 0;
};

using ASTNodePtr = std::unique_ptr<ASTNode>;

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

// Base class for expression nodes (WHERE predicates, INSERT values).
class Expression : public ASTNode {
public:
    // Data type of the expression, filled in by the semantic analyzer.
    // Remains nullopt until binding succeeds.
    std::optional<DataType> resolvedType;
};

using ExpressionPtr = std::unique_ptr<Expression>;

// A literal constant: 42, 'text', TRUE/FALSE, NULL.
class LiteralExpr : public Expression {
public:
    enum class Kind { Integer, String, Boolean, Null, Float };

    Kind kind = Kind::Integer;
    std::int64_t intValue = 0;
    double doubleValue = 0.0;
    std::string stringValue;
    bool boolValue = false;

    void accept(ASTVisitor& visitor) override;
};

// A reference to a column, optionally table-qualified (t.col).
class ColumnRef : public Expression {
public:
    std::string table;   // empty when unqualified
    std::string column;

    // When set, this select-list item is a computed expression (e.g. a * b)
    // rather than a bare column; `column` then holds a display label.
    ExpressionPtr computed;

    // Filled in by the semantic analyzer.
    int columnIndex = -1;

    void accept(ASTVisitor& visitor) override;
};

// A comparison predicate: left <op> right.
class BinaryExpr : public Expression {
public:
    ComparisonOp op = ComparisonOp::Eq;
    ExpressionPtr left;
    ExpressionPtr right;

    void accept(ASTVisitor& visitor) override;
};

// A logical connective: left AND/OR right.
class LogicalExpr : public Expression {
public:
    LogicalOp op = LogicalOp::And;
    ExpressionPtr left;
    ExpressionPtr right;

    void accept(ASTVisitor& visitor) override;
};

// A numeric arithmetic expression: left (+|-|*|/) right.
class ArithmeticExpr : public Expression {
public:
    ArithmeticOp op = ArithmeticOp::Add;
    ExpressionPtr left;
    ExpressionPtr right;

    void accept(ASTVisitor& visitor) override;
};

// A NOT expression.
class UnaryExpr : public Expression {
public:
    ExpressionPtr operand;

    void accept(ASTVisitor& visitor) override;
};

// operand IS [NOT] NULL.
class IsNullExpr : public Expression {
public:
    ExpressionPtr operand;
    bool negated = false;  // true => IS NOT NULL

    void accept(ASTVisitor& visitor) override;
};

// value [NOT] IN (item, item, ...)  or  value [NOT] IN (subquery).
class InExpr : public Expression {
public:
    ExpressionPtr value;
    std::vector<ExpressionPtr> items;
    std::unique_ptr<SubqueryExpr> subquery;  // non-null => membership over a subquery
    bool negated = false;

    void accept(ASTVisitor& visitor) override;
};

// value [NOT] BETWEEN lo AND hi.
class BetweenExpr : public Expression {
public:
    ExpressionPtr value;
    ExpressionPtr lo;
    ExpressionPtr hi;
    bool negated = false;

    void accept(ASTVisitor& visitor) override;
};

// value [NOT] LIKE pattern  (% = any run, _ = any single char).
class LikeExpr : public Expression {
public:
    ExpressionPtr value;
    ExpressionPtr pattern;
    bool negated = false;

    void accept(ASTVisitor& visitor) override;
};

// An aggregate call in a SELECT list: COUNT(*), SUM(col), MIN/MAX/AVG(col).
class FunctionExpr : public Expression {
public:
    std::string name;                       // upper-cased: COUNT/SUM/AVG/MIN/MAX
    bool star = false;                      // COUNT(*)
    bool distinct = false;                  // COUNT(DISTINCT col)
    std::unique_ptr<ColumnRef> argument;    // null when star

    void accept(ASTVisitor& visitor) override;
};

// An uncorrelated subquery used as a scalar (SELECT single-column) or as
// EXISTS(SELECT ...). The executor evaluates it once and caches the first
// column of each result row.
class SubqueryExpr : public Expression {
public:
    enum class Kind { Scalar, Exists };
    Kind kind = Kind::Scalar;
    std::unique_ptr<SelectStatement> query;

    mutable bool evaluated = false;
    mutable std::vector<CachedValue> results;

    void accept(ASTVisitor& visitor) override;
};

// ---------------------------------------------------------------------------
// Statement support types
// ---------------------------------------------------------------------------

// One column in a CREATE TABLE definition.
struct ColumnDefinition {
    std::string name;
    DataType type = DataType::Int;
    int varcharLength = 0;  // meaningful only for Varchar
    std::string refTable;   // inline FOREIGN KEY: REFERENCES refTable(refColumn)
    std::string refColumn;

    // Column constraints.
    bool notNull = false;
    bool primaryKey = false;
    bool unique = false;
    bool hasDefault = false;
    CachedValue defaultValue;  // literal default (valid when hasDefault)
    std::shared_ptr<Expression> checkExpr;  // CHECK (expr), null when absent
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

class CreateStatement : public ASTNode {
public:
    std::string table;
    std::vector<ColumnDefinition> columns;

    // Filled in by the semantic analyzer.
    int tableId = -1;

    void accept(ASTVisitor& visitor) override;
};

class CreateIdxStatement : public ASTNode {
public:
    std::string indexName;
    std::string table;
    std::string column;

    // Filled in by the semantic analyzer.
    int tableId = -1;
    int columnIndex = -1;

    void accept(ASTVisitor& visitor) override;
};

class InsertStatement : public ASTNode {
public:
    std::string table;
    std::vector<std::string> columns;                 // empty => all columns in order
    std::vector<std::vector<ExpressionPtr>> rows;     // one inner vector per VALUES tuple

    // Filled in by the semantic analyzer.
    int tableId = -1;

    void accept(ASTVisitor& visitor) override;
};

class SelectStatement : public ASTNode {
public:
    bool selectStar = false;
    bool distinct = false;                            // SELECT DISTINCT
    std::vector<std::unique_ptr<ColumnRef>> columns;  // empty when selectStar
    std::string table;
    ExpressionPtr where;                              // null when no WHERE

    // ORDER BY key: a column and a direction.
    struct OrderKey {
        std::unique_ptr<ColumnRef> column;
        bool ascending = true;
    };
    std::vector<OrderKey> orderBy;

    bool hasLimit = false;
    long long limit = 0;

    // Aggregate/group-by support. When `aggregates` is non-empty this is an
    // aggregate query; `columns` then holds the grouping columns echoed in the
    // output and `groupBy` the GROUP BY keys.
    std::vector<std::unique_ptr<FunctionExpr>> aggregates;
    std::vector<std::unique_ptr<ColumnRef>> groupBy;
    ExpressionPtr having;

    // INNER JOIN support. When `joinTable` is non-empty the FROM clause is a
    // two-table join. `joinType` selects inner / left-outer / cross; `joinOn`
    // is the ON predicate (absent for a cross join).
    enum class JoinKind { Inner, Left, Cross };
    std::string joinTable;
    ExpressionPtr joinOn;
    int joinTableId = -1;
    JoinKind joinType = JoinKind::Inner;

    // Filled in by the semantic analyzer.
    int tableId = -1;

    void accept(ASTVisitor& visitor) override;
};

class DeleteStatement : public ASTNode {
public:
    std::string table;
    ExpressionPtr where;  // null when no WHERE

    // Filled in by the semantic analyzer.
    int tableId = -1;

    void accept(ASTVisitor& visitor) override;
};

class UpdateStatement : public ASTNode {
public:
    std::string table;
    std::vector<std::string> targetColumns;
    std::vector<ExpressionPtr> values;  // parallel to targetColumns
    ExpressionPtr where;                // null when no WHERE

    // Filled in by the semantic analyzer.
    int tableId = -1;
    std::vector<int> targetIndices;

    void accept(ASTVisitor& visitor) override;
};

class DropStatement : public ASTNode {
public:
    bool isIndex = false;  // false => DROP TABLE
    std::string name;

    // Filled in by the semantic analyzer (tables only).
    int tableId = -1;

    void accept(ASTVisitor& visitor) override;
};

class AlterStatement : public ASTNode {
public:
    enum class Kind { AddColumn, DropColumn };
    Kind kind = Kind::AddColumn;
    std::string table;
    ColumnDefinition column;   // for AddColumn
    std::string dropColumn;    // for DropColumn

    // Filled in by the semantic analyzer.
    int tableId = -1;

    void accept(ASTVisitor& visitor) override;
};

class TransactionStatement : public ASTNode {
public:
    enum class Kind { Begin, Commit, Rollback };
    Kind kind = Kind::Begin;

    void accept(ASTVisitor& visitor) override;
};

// ---------------------------------------------------------------------------
// Visitor
// ---------------------------------------------------------------------------

// Double-dispatch interface implemented by passes over the AST (semantic
// analysis now; execution planning in later phases).
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visit(LiteralExpr& node) = 0;
    virtual void visit(ColumnRef& node) = 0;
    virtual void visit(BinaryExpr& node) = 0;
    virtual void visit(ArithmeticExpr& node) = 0;
    virtual void visit(LogicalExpr& node) = 0;
    virtual void visit(UnaryExpr& node) = 0;
    virtual void visit(IsNullExpr& node) = 0;
    virtual void visit(InExpr& node) = 0;
    virtual void visit(BetweenExpr& node) = 0;
    virtual void visit(LikeExpr& node) = 0;
    virtual void visit(FunctionExpr& node) = 0;
    virtual void visit(SubqueryExpr& node) = 0;

    virtual void visit(CreateStatement& node) = 0;
    virtual void visit(CreateIdxStatement& node) = 0;
    virtual void visit(InsertStatement& node) = 0;
    virtual void visit(SelectStatement& node) = 0;
    virtual void visit(DeleteStatement& node) = 0;
    virtual void visit(UpdateStatement& node) = 0;
    virtual void visit(DropStatement& node) = 0;
    virtual void visit(AlterStatement& node) = 0;
    virtual void visit(TransactionStatement& node) = 0;
};

}  // namespace db::parser
