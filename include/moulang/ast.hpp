#pragma once

#include "moulang/source.hpp"
#include "moulang/types.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace moulang {
enum class ExpressionKind { integer, real, boolean, string, variable, unary, binary, call, slice, subscript,
    member, safeMember, methodCall, safeMethodCall, standardCall, constructObject, view, conditional, nullLiteral };

struct Expression {
    ExpressionKind kind;
    Location location;
    std::string text;
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> right;
    Type type = Type::inferred;
    std::int64_t integer = 0;
    std::vector<std::unique_ptr<Expression>> arguments{};
    double real = 0;
    Type operandType = Type::inferred;
    unsigned fieldIndex = 0;
    std::unique_ptr<Expression> alternative{};
    static constexpr std::size_t noFunction = static_cast<std::size_t>(-1);
    std::size_t functionIndex = noFunction;
    std::string borrowOwner{};
    Type memberOwner = Type::none;
    std::unique_ptr<Expression> step{};
    std::optional<SourceSpan> span{};
};

enum class StatementKind { print, declaration, assignment, fieldAssignment, call, returnValue, branch,
    whileLoop, forLoop, foreachLoop, breakLoop, continueLoop };

struct Statement {
    StatementKind kind;
    Location location;
    std::string name;
    Type type;
    std::unique_ptr<Expression> value;
    std::vector<Statement> body{};
    std::vector<Statement> alternative{};
    std::unique_ptr<Expression> bound{};
    std::unique_ptr<Expression> target{};
    std::vector<std::unique_ptr<Expression>> arguments{};
    std::unique_ptr<Expression> format{};
    std::vector<Expression> inputTargets{};
    bool postMutation = false;
};

struct TypedName {
    Location location;
    std::string name;
    Type type;
    bool isPublic = true;
    std::unique_ptr<Expression> defaultValue{};
};

struct Function {
    Location location;
    std::string name;
    Type result;
    std::vector<TypedName> parameters;
    std::vector<Statement> body;
    bool exported = true;
    Type owner = Type::none;
    bool constructor = false;
};

struct Structure {
    Location location;
    std::string name;
    Type type;
    std::vector<TypedName> fields;
    bool exported = true;
    bool reference = false;
    Type base = Type::none;
    std::size_t baseConstructor = Expression::noFunction;
};

struct TypeReference {
    std::string name;
    std::size_t file;
};

struct Program {
    struct Import {
        Location location;
        std::string path;
        std::string alias{};
    };

    std::vector<Function> functions;
    std::vector<Structure> structures;
    std::vector<Import> imports;
    std::vector<std::string> sourceNames;
    std::vector<std::vector<std::size_t>> dependencies;
    std::vector<TypeReference> typeReferences;
};
}
