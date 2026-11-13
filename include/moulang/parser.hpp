#pragma once

#include "moulang/ast.hpp"
#include "moulang/lexer.hpp"

#include <unordered_map>
#include <optional>

namespace moulang {
class Parser {
public:
    Parser(std::string_view source, std::size_t file, std::vector<TypeReference>& typeReferences)
        : lexer_(source, file), current_(lexer_.next()), typeReferences_(typeReferences) {}
    Program parse();

private:
    Token take();
    Token expect(TokenKind kind, const char* message);
    Token expectWord(const char* word, const char* message);
    void skipNewlines();
    bool atWord(std::string_view word) const;
    Type parseType(const Token& token);
    Type internStructure(const std::string& name);
    Type parseTypeSuffix(Type type);
    void expectOperator(std::string_view op);
    Function parseFunction();
    Function parseFunctionTail(Token name, Type result);
    Structure parseStructure();
    void parseClass(Program& program, bool exported);
    std::vector<Statement> parseBlock();
    std::unique_ptr<Expression> parseCondition();
    Statement parseBranch(Token word);
    std::unique_ptr<Expression> parseCall(Token name);
    std::unique_ptr<Expression> parseNew();
    std::unique_ptr<Expression> parsePrimary(bool multiline);
    std::unique_ptr<Expression> parseMember(std::unique_ptr<Expression> base, bool multiline);
    std::unique_ptr<Expression> parseSubscript(std::unique_ptr<Expression> base);
    void parseArguments(std::vector<std::unique_ptr<Expression>>& arguments, TokenKind end);
    Statement parseStatement();
    std::unique_ptr<Expression> parseExpression(int minimum = 1, bool multiline = false);
    std::unique_ptr<Expression> parseOperand(bool multiline);

    Lexer lexer_;
    Token current_;
    TokenKind previous_ = TokenKind::end;
    Location lastTokenEnd_;
    std::unordered_map<std::string, Type> structureTypes_;
    std::vector<TypeReference>& typeReferences_;
};

Program loadProgram(const std::filesystem::path& entry, std::vector<std::filesystem::path>& sources,
    std::optional<std::string_view> entryText = std::nullopt, std::vector<std::string>* snapshots = nullptr);
}
