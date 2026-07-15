#pragma once

#include <string>
#include <string_view>

namespace db::parser {

// All token categories produced by the lexer for our SQL subset.
enum class TokenType {
    // Keywords - statements / clauses
    SELECT,
    FROM,
    WHERE,
    INSERT,
    INTO,
    VALUES,
    CREATE,
    TABLE,
    INDEX,
    ON,
    DELETE,
    UPDATE,
    SET,
    DROP,
    ALTER,
    ADD,
    COLUMN,
    REFERENCES,
    FOREIGN,
    KEY,
    PRIMARY,
    UNIQUE,
    DEFAULT,
    CHECK,

    // Keywords - clauses
    ORDER,
    BY,
    GROUP,
    HAVING,
    LIMIT,
    AS,
    ASC,
    DESC,
    JOIN,
    INNER,
    LEFT,
    CROSS,
    DISTINCT,

    // Keywords - predicates
    IS,
    IN,
    BETWEEN,
    LIKE,
    NULL_LITERAL,
    EXISTS,

    // Keywords - transactions
    BEGIN,
    COMMIT,
    ROLLBACK,
    TRANSACTION,

    // Keywords - logical operators
    AND,
    OR,
    NOT,

    // Keywords - column types
    INT_TYPE,
    BOOL_TYPE,
    TEXT_TYPE,
    VARCHAR,
    FLOAT_TYPE,

    // Keywords - boolean literals
    TRUE,
    FALSE,

    // Literals / names
    IDENTIFIER,
    INTEGER_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,

    // Comparison operators
    EQ,   // =
    NEQ,  // != or <>
    LT,   // <
    LEQ,  // <=
    GT,   // >
    GEQ,  // >=

    // Arithmetic operators
    PLUS,   // +
    MINUS,  // -
    SLASH,  // /

    // Punctuation
    LPAREN,     // (
    RPAREN,     // )
    COMMA,      // ,
    SEMICOLON,  // ;
    STAR,       // *
    DOT,        // .

    // Special
    END_OF_FILE,
    UNKNOWN
};

// A single lexical token, with source position for diagnostics.
// For string literals, `lexeme` holds the decoded value (no surrounding quotes,
// escaped quotes collapsed). For identifiers it preserves the original casing.
struct Token {
    TokenType type;
    std::string lexeme;
    int line;
    int column;
};

// Returns a stable, human-readable name for a token type (for debugging / REPL).
std::string_view tokenTypeName(TokenType type);

}  // namespace db::parser
