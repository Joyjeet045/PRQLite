#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "frontend/token.hpp"

namespace db::parser {

// Hand-written scanner for our SQL subset. No regex, no external libraries.
// Produces a token stream that always ends with an END_OF_FILE token.
class Lexer {
public:
    explicit Lexer(std::string source);

    // Scans the entire source and returns the full token stream.
    std::vector<Token> tokenize();

private:
    std::string source_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;

    bool isAtEnd() const;
    char peek() const;
    char peekNext() const;
    char advance();

    void skipWhitespaceAndComments();

    Token scanToken();
    Token scanIdentifierOrKeyword();
    Token scanNumber();
    Token scanString();

    Token makeToken(TokenType type, std::string lexeme, int startColumn) const;
};

}  // namespace db::parser
