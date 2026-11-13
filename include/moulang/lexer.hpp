#pragma once

#include "moulang/source.hpp"

#include <string_view>

namespace moulang {
enum class TokenKind {
    identifier, string, integer, real, operation, leftParen, rightParen, leftBrace, rightBrace,
    leftBracket, rightBracket, comma, dot, safeDot, colon, newline, end
};

struct Token {
    TokenKind kind;
    Location location;
    std::string text;
    Location end{};
};

class Lexer {
public:
    Lexer(std::string_view source, std::size_t file) : source_(source), location_{1, 1, file} {}
    Token next();

private:
    Token scan();
    char advance();
    Token readString(Location start);

    std::string_view source_;
    std::size_t offset_ = 0;
    Location location_;
};
}
