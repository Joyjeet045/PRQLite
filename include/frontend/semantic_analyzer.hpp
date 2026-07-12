#pragma once

#include <stdexcept>
#include <string>

#include "frontend/ast.hpp"
#include "frontend/catalog.hpp"

namespace db::semantic {

// Raised when a statement parses cleanly but is semantically invalid: unknown
// table/column, type mismatch, arity mismatch, duplicate names, etc.
class SemanticError : public std::runtime_error {
public:
    explicit SemanticError(const std::string& message);
};

// Walks a parsed statement, validates it against the Catalog, and binds the
// resolved table ids / column indices / expression types back into the AST.
// CREATE statements additionally mutate the catalog (registering the new
// table or index).
class SemanticAnalyzer : public parser::ASTVisitor {
public:
    explicit SemanticAnalyzer(Catalog& catalog);

    // Analyzes `node` in place. Throws SemanticError on any violation.
    void analyze(parser::ASTNode& node);

    // Binds and validates a standalone boolean expression against `tableName`
    // (used to re-bind a persisted CHECK constraint after loading).
    void bindExpression(parser::Expression& expr, const std::string& tableName);

    void visit(parser::LiteralExpr& node) override;
    void visit(parser::ColumnRef& node) override;
    void visit(parser::BinaryExpr& node) override;
    void visit(parser::LogicalExpr& node) override;
    void visit(parser::UnaryExpr& node) override;
    void visit(parser::IsNullExpr& node) override;
    void visit(parser::InExpr& node) override;
    void visit(parser::BetweenExpr& node) override;
    void visit(parser::LikeExpr& node) override;
    void visit(parser::FunctionExpr& node) override;
    void visit(parser::SubqueryExpr& node) override;
    void visit(parser::CreateStatement& node) override;
    void visit(parser::CreateIdxStatement& node) override;
    void visit(parser::InsertStatement& node) override;
    void visit(parser::SelectStatement& node) override;
    void visit(parser::DeleteStatement& node) override;
    void visit(parser::UpdateStatement& node) override;
    void visit(parser::DropStatement& node) override;
    void visit(parser::AlterStatement& node) override;
    void visit(parser::TransactionStatement& node) override;

private:
    Catalog& catalog_;
    const TableSchema* currentTable_ = nullptr;  // table in scope for column binding

    // Two-table scope for JOIN column resolution.
    const TableSchema* leftTable_ = nullptr;
    const TableSchema* rightTable_ = nullptr;
    int leftColumnCount_ = 0;
    bool joinMode_ = false;

    // Analyzes `expr` and requires it to be a boolean predicate.
    void checkPredicate(parser::Expression& expr);
};

}  // namespace db::semantic
