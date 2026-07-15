#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "frontend/ast.hpp"
#include "frontend/token.hpp"

namespace db::parser {

// Raised on a syntax error. Carries the offending source position so the REPL
// can point at the mistake.
class ParseError : public std::runtime_error {
public:
    ParseError(std::string message, int line, int column);

    int line() const { return line_; }
    int column() const { return column_; }

private:
    int line_;
    int column_;
};

// Recursive-descent parser over the token stream produced by the Lexer.
// The grammar is left-recursion free; expression parsing uses precedence
// climbing (OR < AND < NOT < comparison < primary).
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    // Parses a single statement, optionally terminated by ';'. Requires the
    // token stream to be fully consumed afterwards. Throws ParseError on any
    // malformed input.
    ASTNodePtr parseStatement();

    // Parses one standalone expression (used to re-parse a persisted CHECK
    // constraint from its stored source text).
    ExpressionPtr parseWholeExpression();

private:
    std::vector<Token> tokens_;
    std::size_t pos_ = 0;

    // --- Token cursor helpers ---
    const Token& peek() const;
    const Token& peekAt(std::size_t offset) const;
    const Token& previous() const;
    bool isAtEnd() const;
    const Token& advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    const Token& consume(TokenType type, const std::string& what);
    [[noreturn]] void error(const Token& tok, const std::string& message) const;

    std::int64_t toInt64(const Token& tok) const;
    std::unique_ptr<LiteralExpr> makeLiteral(const Token& tok);

    // --- Statement productions ---
    ASTNodePtr parseCreate();
    ASTNodePtr parseInsert();
    ASTNodePtr parseSelect();
    ASTNodePtr parseDelete();
    ASTNodePtr parseUpdate();
    ASTNodePtr parseDrop();
    ASTNodePtr parseAlter();
    ASTNodePtr parseTransaction();

    ColumnDefinition parseColumnDefinition();
    std::unique_ptr<ColumnRef> parseColumnRef();
    std::unique_ptr<SelectStatement> parseSubquery();
    ExpressionPtr parseLiteral();

    // --- Expression productions (precedence climbing) ---
    ExpressionPtr parseExpression();
    ExpressionPtr parseOr();
    ExpressionPtr parseAnd();
    ExpressionPtr parseNot();
    ExpressionPtr parseComparison();
    ExpressionPtr parseAdditive();
    ExpressionPtr parseMultiplicative();
    ExpressionPtr parseUnary();
    ExpressionPtr parsePrimary();
};

}  // namespace db::parser
