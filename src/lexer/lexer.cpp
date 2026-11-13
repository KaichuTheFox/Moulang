#include "moulang/lexer.hpp"

#include <cstdint>
#include <optional>
#include <utility>

namespace moulang {
namespace {
bool identifierStart(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool identifierPart(char c) {
    return identifierStart(c) || (c >= '0' && c <= '9');
}

bool hangul(std::string_view source) {
    if (source.size() < 3) return false;
    const auto first = static_cast<unsigned char>(source[0]);
    const auto second = static_cast<unsigned char>(source[1]);
    const auto third = static_cast<unsigned char>(source[2]);
    if ((first & 0xf0) != 0xe0 || (second & 0xc0) != 0x80 || (third & 0xc0) != 0x80) return false;
    const unsigned point = ((first & 0xf) << 12) | ((second & 0x3f) << 6) | (third & 0x3f);
    return (point >= 0x1100 && point <= 0x11ff) || (point >= 0x3131 && point <= 0x318e) ||
        (point >= 0xa960 && point <= 0xa97c) || (point >= 0xac00 && point <= 0xd7a3) ||
        (point >= 0xd7b0 && point <= 0xd7c6) || (point >= 0xd7cb && point <= 0xd7fb);
}

bool digit(char c) {
    return c >= '0' && c <= '9';
}

int hexadecimal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
}

char Lexer::advance() {
    const char c = source_[offset_++];
    if (c == '\r' || c == '\n') {
        if (c == '\r' && offset_ < source_.size() && source_[offset_] == '\n') {
            ++offset_;
        }
        ++location_.line;
        location_.column = 1;
    } else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
        ++location_.column;
    }
    return c;
}

Token Lexer::readString(Location start) {
    std::string value;
    unsigned remaining = 0;
    unsigned lower = 0x80;
    unsigned upper = 0xBF;
    Location sequenceStart = start;
    const auto invalidUtf8 = [](Location origin) {
        throw Diagnostic(origin, "ML1005", "String literal must decode to valid UTF-8.");
    };
    const auto appendByte = [&](unsigned byte, Location origin) {
        if (remaining) {
            if (byte < lower || byte > upper) invalidUtf8(sequenceStart);
            --remaining;
            lower = 0x80;
            upper = 0xBF;
        } else if (byte >= 0x80) {
            sequenceStart = origin;
            if (byte >= 0xC2 && byte <= 0xDF) remaining = 1;
            else if (byte >= 0xE0 && byte <= 0xEF) {
                remaining = 2;
                lower = byte == 0xE0 ? 0xA0 : 0x80;
                upper = byte == 0xED ? 0x9F : 0xBF;
            } else if (byte >= 0xF0 && byte <= 0xF4) {
                remaining = 3;
                lower = byte == 0xF0 ? 0x90 : 0x80;
                upper = byte == 0xF4 ? 0x8F : 0xBF;
            } else invalidUtf8(origin);
        }
        value.push_back(static_cast<char>(byte));
    };
    while (offset_ < source_.size()) {
        const auto origin = location_;
        const char c = advance();
        if (c == '"') {
            if (remaining) invalidUtf8(sequenceStart);
            return {TokenKind::string, start, std::move(value)};
        }
        if (c == '\r' || c == '\n') {
            break;
        }
        if (c == '\0') {
            throw Diagnostic(origin, "ML1001", "Unexpected raw NUL in string literal. Use a NUL escape.");
        }
        if (c != '\\') {
            appendByte(static_cast<unsigned char>(c), origin);
            continue;
        }
        if (offset_ == source_.size() || source_[offset_] == '\r' || source_[offset_] == '\n') {
            throw Diagnostic(origin, "ML1004", "Incomplete string escape.");
        }
        switch (advance()) {
        case 'n': appendByte('\n', origin); break;
        case 'r': appendByte('\r', origin); break;
        case 't': appendByte('\t', origin); break;
        case '\\': appendByte('\\', origin); break;
        case '"': appendByte('"', origin); break;
        case '0': appendByte(0, origin); break;
        case 'x': {
            unsigned byte = 0;
            for (unsigned i = 0; i < 2; ++i) {
                if (offset_ == source_.size() || hexadecimal(source_[offset_]) < 0) {
                    throw Diagnostic(origin, "ML1004", "Hexadecimal escape requires exactly two hexadecimal digits.");
                }
                byte = byte * 16 + static_cast<unsigned>(hexadecimal(advance()));
            }
            appendByte(byte, origin);
            break;
        }
        case 'u': {
            if (offset_ == source_.size() || source_[offset_] != '{') {
                throw Diagnostic(origin, "ML1004", "Unicode escape requires braces with one to six hexadecimal digits.");
            }
            advance();
            unsigned digits = 0;
            std::uint32_t scalar = 0;
            while (offset_ < source_.size() && hexadecimal(source_[offset_]) >= 0 && digits < 6) {
                scalar = scalar * 16 + static_cast<unsigned>(hexadecimal(advance()));
                ++digits;
            }
            if (digits == 0 || offset_ == source_.size() || source_[offset_] != '}') {
                throw Diagnostic(origin, "ML1004", "Unicode escape requires braces with one to six hexadecimal digits.");
            }
            advance();
            if (scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF)) {
                throw Diagnostic(origin, "ML1004", "Unicode escape must be a scalar value at most U+10FFFF, excluding surrogates.");
            }
            if (scalar <= 0x7F) appendByte(scalar, origin);
            else if (scalar <= 0x7FF) {
                appendByte(0xC0 | (scalar >> 6), origin);
                appendByte(0x80 | (scalar & 0x3F), origin);
            } else if (scalar <= 0xFFFF) {
                appendByte(0xE0 | (scalar >> 12), origin);
                appendByte(0x80 | ((scalar >> 6) & 0x3F), origin);
                appendByte(0x80 | (scalar & 0x3F), origin);
            } else {
                appendByte(0xF0 | (scalar >> 18), origin);
                appendByte(0x80 | ((scalar >> 12) & 0x3F), origin);
                appendByte(0x80 | ((scalar >> 6) & 0x3F), origin);
                appendByte(0x80 | (scalar & 0x3F), origin);
            }
            break;
        }
        default: throw Diagnostic(origin, "ML1004", "Unknown string escape. Use a supported escape or escape the backslash.",
            {SourceSpan{origin, location_}, {}, "Write \\\\ for a literal backslash."});
        }
    }
    throw Diagnostic(start, "ML1002", "Unterminated string literal.");
}

Token Lexer::next() {
    auto token = scan();
    token.end = location_;
    return token;
}

Token Lexer::scan() {
    while (offset_ < source_.size()) {
        const auto start = location_;
        const auto begin = offset_;
        const char c = advance();
        switch (c) {
        case ' ':
        case '\t':
            continue;
        case '\n':
        case '\r':
            return {TokenKind::newline, start, {}};
        case '(':
            return {TokenKind::leftParen, start, {}};
        case ')':
            return {TokenKind::rightParen, start, {}};
        case '{':
            return {TokenKind::leftBrace, start, {}};
        case '}':
            return {TokenKind::rightBrace, start, {}};
        case '[':
            return {TokenKind::leftBracket, start, {}};
        case ']':
            return {TokenKind::rightBracket, start, {}};
        case ',':
            return {TokenKind::comma, start, {}};
        case '.':
            return {TokenKind::dot, start, {}};
        case ':':
            return {TokenKind::colon, start, {}};
        case '"':
            return readString(start);
        case '/':
            if (offset_ < source_.size() && source_[offset_] == '/') {
                while (offset_ < source_.size() && source_[offset_] != '\r' && source_[offset_] != '\n') {
                    advance();
                }
                continue;
            }
            if (offset_ < source_.size() && source_[offset_] == '*') {
                advance();
                std::optional<Location> newline;
                bool closed = false;
                while (offset_ < source_.size()) {
                    const auto position = location_;
                    const char next = advance();
                    if (next == '*' && offset_ < source_.size() && source_[offset_] == '/') {
                        advance();
                        closed = true;
                        break;
                    }
                    if (!newline && (next == '\n' || next == '\r')) newline = position;
                }
                if (!closed) throw Diagnostic(start, "ML1006", "Unterminated block comment.");
                if (newline) return {TokenKind::newline, *newline, {}};
                continue;
            }
            break;
        }
        if (digit(c)) {
            const auto peek = [&](std::size_t distance = 0) {
                return distance < source_.size() - offset_ ? source_[offset_ + distance] : '\0';
            };
            while (digit(peek())) advance();
            auto kind = TokenKind::integer;
            if (peek() == '.' && digit(peek(1))) {
                kind = TokenKind::real;
                advance();
                while (digit(peek())) advance();
            }
            if (peek() == 'e' || peek() == 'E') {
                kind = TokenKind::real;
                advance();
                if (peek() == '+' || peek() == '-') advance();
                if (!digit(peek())) throw Diagnostic(start, "ML1003", "Expected digits in the exponent.");
                while (digit(peek())) advance();
            }
            return {kind, start, std::string(source_.substr(begin, offset_ - begin))};
        }
        if (std::string_view("+-*/=<>!&|?^~").find(c) != std::string_view::npos) {
            const char next = offset_ < source_.size() ? source_[offset_] : '\0';
            if (c == '?' && next == '.') {
                advance();
                return {TokenKind::safeDot, start, {}};
            }
            if ((std::string_view("=!<>+-*/").find(c) != std::string_view::npos && next == '=') ||
                (std::string_view("+-&|?<>").find(c) != std::string_view::npos && next == c)) {
                advance();
            }
            return {TokenKind::operation, start, std::string(source_.substr(begin, offset_ - begin))};
        }
        const bool hangulStart = hangul(source_.substr(begin));
        if (identifierStart(c) || hangulStart) {
            if (hangulStart) {
                advance();
                advance();
            }
            while (offset_ < source_.size()) {
                if (identifierPart(source_[offset_])) advance();
                else if (hangul(source_.substr(offset_))) {
                    advance();
                    advance();
                    advance();
                } else break;
            }
            return {TokenKind::identifier, start, std::string(source_.substr(begin, offset_ - begin))};
        }
        throw Diagnostic(start, "ML1001", "Unexpected character.");
    }
    return {TokenKind::end, location_, {}};
}
}
