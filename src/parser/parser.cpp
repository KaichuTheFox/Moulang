#include "moulang/parser.hpp"

#include <charconv>
#include <utility>

namespace moulang {
Token Parser::take() {
    previous_ = current_.kind;
    return std::exchange(current_, lexer_.next());
}

Token Parser::expect(TokenKind kind, const char* message) {
    if (current_.kind != kind) {
        throw Diagnostic(current_.location, "ML2001", message);
    }
    return take();
}

Token Parser::expectWord(const char* word, const char* message) {
    if (current_.kind != TokenKind::identifier || current_.text != word) {
        throw Diagnostic(current_.location, "ML2001", message);
    }
    return take();
}

void Parser::skipNewlines() {
    while (current_.kind == TokenKind::newline) {
        take();
    }
}

namespace {
int precedence(std::string_view op) {
    if (op == "??") return 2;
    if (op == "||") return 3;
    if (op == "&&") return 4;
    if (op == "|") return 5;
    if (op == "^") return 6;
    if (op == "&") return 7;
    if (op == "==" || op == "!=") return 8;
    if (op == "<" || op == ">" || op == "<=" || op == ">=") return 9;
    if (op == "<<" || op == ">>") return 10;
    if (op == "+" || op == "-") return 11;
    if (op == "*" || op == "/") return 12;
    return 0;
}

std::unique_ptr<Expression> integerLiteral(Token token, bool negative = false) {
    if (negative) token.text.insert(0, "-");
    auto expression = std::make_unique<Expression>(Expression{
        ExpressionKind::integer, token.location, {}, {}, {}
    });
    const auto parsed = std::from_chars(token.text.data(), token.text.data() + token.text.size(),
        expression->integer);
    if (parsed.ec != std::errc{}) {
        throw Diagnostic(token.location, "ML3003", "Integer literal is outside the signed 64-bit range.");
    }
    return expression;
}
}

std::unique_ptr<Expression> Parser::parseOperand(bool multiline) {
    if (multiline) skipNewlines();
    if (current_.kind == TokenKind::operation &&
        (current_.text == "-" || current_.text == "!" || current_.text == "~")) {
        auto op = take();
        if (multiline) skipNewlines();
        if (op.text == "-" && current_.kind == TokenKind::integer) {
            return integerLiteral(take(), true);
        }
        return std::make_unique<Expression>(Expression{
            ExpressionKind::unary, op.location, std::move(op.text), parseOperand(multiline), {}
        });
    }
    auto value = parsePrimary(multiline);
    if (multiline) skipNewlines();
    while (current_.kind == TokenKind::leftBracket || current_.kind == TokenKind::dot ||
        current_.kind == TokenKind::safeDot) {
        if (current_.kind != TokenKind::leftBracket) {
            value = parseMember(std::move(value), multiline);
        } else {
            value = parseSubscript(std::move(value));
        }
        if (multiline) skipNewlines();
    }
    return value;
}

std::unique_ptr<Expression> Parser::parseSubscript(std::unique_ptr<Expression> base) {
    const auto bracket = take();
    auto index = parseExpression(1, true);
    const bool view = current_.kind == TokenKind::colon;
    std::unique_ptr<Expression> end;
    if (view) {
        take();
        end = parseExpression(1, true);
    }
    expect(TokenKind::rightBracket, "Expected ']' after index or range.");
    auto result = std::make_unique<Expression>(Expression{
        view ? ExpressionKind::view : ExpressionKind::subscript, bracket.location, {},
        std::move(base), std::move(index)
    });
    result->alternative = std::move(end);
    return result;
}

std::unique_ptr<Expression> Parser::parseMember(std::unique_ptr<Expression> base, bool multiline) {
    const bool safe = take().kind == TokenKind::safeDot;
    if (multiline) skipNewlines();
    auto field = expect(TokenKind::identifier, "Expected a field name after '.'.");
    if (multiline) skipNewlines();
    if (current_.kind == TokenKind::leftParen) {
        auto method = parseCall(std::move(field));
        method->kind = safe ? ExpressionKind::safeMethodCall : ExpressionKind::methodCall;
        method->left = std::move(base);
        return method;
    }
    return std::make_unique<Expression>(Expression{
        safe ? ExpressionKind::safeMember : ExpressionKind::member, field.location,
        std::move(field.text), std::move(base), {}
    });
}

std::unique_ptr<Expression> Parser::parsePrimary(bool multiline) {
    if (current_.kind == TokenKind::leftParen) {
        take();
        auto value = parseExpression(1, true);
        expect(TokenKind::rightParen, "Expected ')' after expression.");
        return value;
    }
    if (current_.kind == TokenKind::leftBracket) {
        const auto bracket = take();
        auto slice = std::make_unique<Expression>(Expression{ExpressionKind::slice, bracket.location, {}, {}, {}});
        parseArguments(slice->arguments, TokenKind::rightBracket);
        expect(TokenKind::rightBracket, "Expected ']' after slice literal.");
        return slice;
    }
    if (current_.kind == TokenKind::integer) return integerLiteral(take());
    if (current_.kind == TokenKind::real) {
        const auto token = take();
        auto value = std::make_unique<Expression>(Expression{ExpressionKind::real, token.location, {}, {}, {}});
        const auto end = token.text.data() + token.text.size();
        const auto parsed = std::from_chars(token.text.data(), end, value->real);
        if (parsed.ec != std::errc{} || parsed.ptr != end) {
            throw Diagnostic(token.location, "ML3003", "Real literal is outside the supported double range.");
        }
        return value;
    }
    if (current_.kind == TokenKind::string) {
        auto token = take();
        return std::make_unique<Expression>(Expression{
            ExpressionKind::string, token.location, std::move(token.text), {}, {}
        });
    }
    auto word = expect(TokenKind::identifier, "Expected an expression.");
    if (word.text == "new") return parseNew();
    if (multiline) skipNewlines();
    if (current_.kind == TokenKind::leftParen) return parseCall(std::move(word));
    auto kind = ExpressionKind::variable;
    if (word.text == "true" || word.text == "false") kind = ExpressionKind::boolean;
    if (word.text == "null") kind = ExpressionKind::nullLiteral;
    return std::make_unique<Expression>(Expression{kind, word.location, std::move(word.text), {}, {}});
}

std::unique_ptr<Expression> Parser::parseExpression(int minimum, bool multiline) {
    auto left = parseOperand(multiline);
    if (multiline) skipNewlines();
    while (current_.kind == TokenKind::operation && precedence(current_.text) >= minimum) {
        const int level = precedence(current_.text);
        auto op = take();
        auto right = parseExpression(level + (op.text != "??"), multiline);
        left = std::make_unique<Expression>(Expression{
            ExpressionKind::binary, op.location, std::move(op.text), std::move(left), std::move(right)
        });
    }
    if (minimum == 1 && current_.kind == TokenKind::operation && current_.text == "?") {
        const auto marker = take();
        auto positive = parseExpression(1, multiline);
        expect(TokenKind::colon, "Expected ':' in the conditional expression.");
        auto negative = parseExpression(1, multiline);
        left = std::make_unique<Expression>(Expression{
            ExpressionKind::conditional, marker.location, {}, std::move(left), std::move(positive)
        });
        left->alternative = std::move(negative);
    }
    return left;
}

Statement Parser::parseStatement() {
    auto word = expect(TokenKind::identifier, "Expected a statement.");
    if (word.text == "new") {
        return {StatementKind::call, word.location, {}, Type::none, parseNew()};
    }
    if (word.text == "print") {
        expect(TokenKind::leftParen, "Expected '(' after print.");
        auto value = parseExpression(1, true);
        expect(TokenKind::rightParen, "Expected ')' after the expression.");
        return {StatementKind::print, word.location, {}, Type::inferred, std::move(value)};
    }
    if (word.text == "return") {
        auto value = current_.kind == TokenKind::newline || current_.kind == TokenKind::rightBrace
            ? nullptr : parseExpression();
        return {StatementKind::returnValue, word.location, {}, Type::inferred, std::move(value)};
    }
    if (word.text == "if") return parseBranch(std::move(word));
    if (word.text == "while") {
        auto condition = parseCondition();
        return {StatementKind::whileLoop, word.location, {}, Type::inferred, std::move(condition), parseBlock()};
    }
    if (word.text == "for") {
        expect(TokenKind::leftParen, "Expected '(' after for.");
        skipNewlines();
        auto name = expect(TokenKind::identifier, "Expected the loop variable.");
        skipNewlines();
        expectOperator("=");
        auto start = parseExpression(1, true);
        expectWord("in", "Expected 'in' after the loop start.");
        skipNewlines();
        expectWord(name.text.c_str(), "Expected the same loop variable after 'in'.");
        skipNewlines();
        expectOperator("++");
        auto bound = parseCondition();
        skipNewlines();
        expect(TokenKind::rightParen, "Expected ')' after the for header.");
        return {StatementKind::forLoop, name.location, std::move(name.text), Type::integer,
            std::move(start), parseBlock(), {}, std::move(bound)};
    }
    if (word.text == "foreach") {
        expect(TokenKind::leftParen, "Expected '(' after foreach.");
        skipNewlines();
        auto name = expect(TokenKind::identifier, "Expected a loop variable.");
        skipNewlines();
        expectWord("in", "Expected 'in' after the loop variable.");
        auto source = parseExpression(1, true);
        expect(TokenKind::rightParen, "Expected ')' after the foreach header.");
        return {StatementKind::foreachLoop, name.location, std::move(name.text), Type::integer,
            std::move(source), parseBlock()};
    }
    if (word.text == "break" || word.text == "continue") {
        return {word.text == "break" ? StatementKind::breakLoop : StatementKind::continueLoop,
            word.location, {}, Type::none, nullptr};
    }
    if (current_.kind == TokenKind::leftParen) {
        const auto location = word.location;
        return {StatementKind::call, location, {}, Type::none, parseCall(std::move(word))};
    }
    if (current_.kind == TokenKind::dot || current_.kind == TokenKind::safeDot ||
        current_.kind == TokenKind::leftBracket) {
        const auto location = word.location;
        auto target = std::make_unique<Expression>(Expression{
            ExpressionKind::variable, location, std::move(word.text), {}, {}
        });
        while (current_.kind == TokenKind::dot || current_.kind == TokenKind::safeDot ||
            current_.kind == TokenKind::leftBracket) {
            if (current_.kind == TokenKind::leftBracket) target = parseSubscript(std::move(target));
            else target = parseMember(std::move(target), false);
        }
        if (target->kind == ExpressionKind::methodCall || target->kind == ExpressionKind::safeMethodCall) {
            return {StatementKind::call, location, {}, Type::none, std::move(target)};
        }
        expectOperator("=");
        Statement statement{StatementKind::fieldAssignment, location, {}, Type::inferred, parseExpression()};
        statement.target = std::move(target);
        return statement;
    }
    const bool declaration = namedType(word.text) != Type::inferred || word.text == "var" ||
        current_.kind == TokenKind::identifier || (current_.kind == TokenKind::operation && current_.text == "?");
    Type type = declaration && word.text != "var" ? parseType(word) : Type::inferred;
    auto name = declaration ? expect(TokenKind::identifier, "Expected a variable name.") : std::move(word);
    if (type == Type::integer && current_.kind == TokenKind::leftBracket) {
        take();
        if (current_.kind == TokenKind::rightBracket) {
            type = Type::intSlice;
        } else {
            const auto length = integerLiteral(expect(TokenKind::integer, "Expected an integer array length."));
            if (length->integer > 65536) {
                throw Diagnostic(length->location, "ML3032", "Fixed int arrays support up to 65536 elements.");
            }
            type = fixedIntArray(static_cast<unsigned>(length->integer));
        }
        expect(TokenKind::rightBracket, "Expected ']' after the slice declaration.");
    }
    const auto op = expect(TokenKind::operation, "Expected an assignment operator.");
    const bool increment = op.text == "++" || op.text == "--";
    const bool compound = op.text == "+=" || op.text == "-=" || op.text == "*=" || op.text == "/=";
    if (op.text != "=" && (declaration || (!compound && !increment))) {
        throw Diagnostic(op.location, "ML2001", "Expected an assignment operator.");
    }
    auto value = increment ? integerLiteral({TokenKind::integer, op.location, "1"}) : parseExpression();
    if (compound || increment) {
        auto target = std::make_unique<Expression>(Expression{ExpressionKind::variable, name.location, name.text, {}, {}});
        value = std::make_unique<Expression>(Expression{
            ExpressionKind::binary, op.location, increment ? op.text : op.text.substr(0, 1),
            std::move(target), std::move(value)
        });
    }
    return {declaration ? StatementKind::declaration : StatementKind::assignment, name.location,
        std::move(name.text), type, std::move(value)};
}

bool Parser::atWord(std::string_view word) const {
    return current_.kind == TokenKind::identifier && current_.text == word;
}

Type Parser::internStructure(const std::string& name) {
    const auto next = static_cast<Type>(static_cast<std::uint32_t>(Type::firstStructure) + typeReferences_.size());
    const auto [entry, inserted] = structureTypes_.try_emplace(name, next);
    if (inserted) typeReferences_.push_back({name, current_.location.file});
    return entry->second;
}

Type Parser::parseType(const Token& token) {
    const auto builtin = namedType(token.text);
    return parseTypeSuffix(builtin == Type::inferred ? internStructure(token.text) : builtin);
}

Type Parser::parseTypeSuffix(Type type) {
    if (current_.kind != TokenKind::operation || current_.text != "?") return type;
    const auto marker = take();
    const auto nullable = nullableType(type);
    if (nullable == Type::inferred) {
        throw Diagnostic(marker.location, "ML2001", "Expected a scalar or structure type before '?'.");
    }
    return nullable;
}

void Parser::expectOperator(std::string_view op) {
    if (current_.kind != TokenKind::operation || current_.text != op) {
        const auto message = "Expected '" + std::string(op) + "'.";
        throw Diagnostic(current_.location, "ML2001", message.c_str());
    }
    take();
}

std::unique_ptr<Expression> Parser::parseCall(Token name) {
    expect(TokenKind::leftParen, "Expected '(' before arguments.");
    auto call = std::make_unique<Expression>(Expression{
        ExpressionKind::call, name.location, std::move(name.text), {}, {}
    });
    parseArguments(call->arguments, TokenKind::rightParen);
    expect(TokenKind::rightParen, "Expected ')' after arguments.");
    return call;
}

std::unique_ptr<Expression> Parser::parseNew() {
    auto name = expect(TokenKind::identifier, "Expected a class name after 'new'.");
    auto expression = parseCall(std::move(name));
    expression->kind = ExpressionKind::constructObject;
    return expression;
}

void Parser::parseArguments(std::vector<std::unique_ptr<Expression>>& arguments, TokenKind end) {
    skipNewlines();
    if (current_.kind == end) return;
    for (;;) {
        arguments.push_back(parseExpression(1, true));
        if (current_.kind != TokenKind::comma) break;
        take();
    }
}

std::unique_ptr<Expression> Parser::parseCondition() {
    expect(TokenKind::leftParen, "Expected '('.");
    auto condition = parseExpression(1, true);
    expect(TokenKind::rightParen, "Expected ')'.");
    return condition;
}

Statement Parser::parseBranch(Token word) {
    auto condition = parseCondition();
    Statement branch{StatementKind::branch, word.location, {}, Type::none, std::move(condition), parseBlock()};
    skipNewlines();
    if (atWord("elif")) {
        branch.alternative.push_back(parseBranch(take()));
    } else if (atWord("else")) {
        take();
        branch.alternative = parseBlock();
    }
    return branch;
}

std::vector<Statement> Parser::parseBlock() {
    skipNewlines();
    expect(TokenKind::leftBrace, "Expected '{' to open a block.");
    skipNewlines();
    std::vector<Statement> statements;
    while (current_.kind != TokenKind::rightBrace && current_.kind != TokenKind::end) {
        statements.push_back(parseStatement());
        if (current_.kind != TokenKind::rightBrace && current_.kind != TokenKind::end) {
            if (previous_ != TokenKind::newline) expect(TokenKind::newline, "Expected a newline between statements.");
            skipNewlines();
        }
    }
    expect(TokenKind::rightBrace, "Expected '}' to close the block.");
    return statements;
}

Function Parser::parseFunction() {
    auto first = expect(TokenKind::identifier, "Expected a function.");
    const bool implicitMain = first.text == "main";
    const Type result = implicitMain ? Type::none : first.text == "def" ? Type::inferred : parseType(first);
    auto name = implicitMain ? std::move(first) : expect(TokenKind::identifier, "Expected a function name.");
    return parseFunctionTail(std::move(name), result);
}

Function Parser::parseFunctionTail(Token name, Type result) {
    expect(TokenKind::leftParen, "Expected '(' after the function name.");
    skipNewlines();
    std::vector<TypedName> parameters;
    if (current_.kind != TokenKind::rightParen) {
        for (;;) {
            auto type = expect(TokenKind::identifier, "Expected a parameter type.");
            const auto parameterType = parseType(type);
            auto parameter = expect(TokenKind::identifier, "Expected a parameter name.");
            if (parameterType == Type::none || parameterType == Type::inferred) {
                throw Diagnostic(type.location, "ML2001", "Expected a value type for the parameter.");
            }
            parameters.push_back({parameter.location, std::move(parameter.text), parameterType});
            skipNewlines();
            if (current_.kind != TokenKind::comma) break;
            take();
            skipNewlines();
        }
    }
    expect(TokenKind::rightParen, "Expected ')' after parameters.");
    return {name.location, std::move(name.text), result, std::move(parameters), parseBlock()};
}

void Parser::parseClass(Program& program, bool exported) {
    expectWord("class", "Expected 'class'.");
    auto name = expect(TokenKind::identifier, "Expected a class name.");
    Structure structure{name.location, std::move(name.text), Type::inferred, {}, exported, true};
    structure.type = internStructure(structure.name);
    if (current_.kind == TokenKind::colon) {
        take();
        structure.base = parseType(expect(TokenKind::identifier, "Expected a base class name."));
        if (current_.kind == TokenKind::comma) {
            throw Diagnostic(current_.location, "ML3038", "Multiple base classes are not supported.");
        }
    }
    skipNewlines();
    expect(TokenKind::leftBrace, "Expected '{' to open the class.");
    skipNewlines();
    while (current_.kind != TokenKind::rightBrace) {
        const bool isPublic = atWord("public");
        if (isPublic || atWord("private")) take();
        auto first = expect(TokenKind::identifier, "Expected a class member.");
        const bool constructor = first.text == structure.name && current_.kind == TokenKind::leftParen;
        const auto type = constructor ? Type::none : first.text == "def" ? Type::inferred : parseType(first);
        auto member = constructor ? std::move(first) : expect(TokenKind::identifier, "Expected a member name.");
        if (current_.kind == TokenKind::leftParen) {
            auto function = parseFunctionTail(std::move(member), type);
            function.parameters.insert(function.parameters.begin(), TypedName{function.location, "this", structure.type});
            function.exported = isPublic;
            function.owner = structure.type;
            function.constructor = constructor;
            program.functions.push_back(std::move(function));
        } else {
            std::unique_ptr<Expression> initial;
            if (current_.kind == TokenKind::operation && current_.text == "=") {
                take();
                initial = parseExpression();
            }
            structure.fields.push_back({member.location, std::move(member.text), type, isPublic, std::move(initial)});
        }
        if (current_.kind == TokenKind::rightBrace) break;
        expect(TokenKind::newline, "Expected a newline between class members.");
        skipNewlines();
    }
    expect(TokenKind::rightBrace, "Expected '}' after class members.");
    program.structures.push_back(std::move(structure));
}

Structure Parser::parseStructure() {
    expectWord("struct", "Expected 'struct'.");
    auto name = expect(TokenKind::identifier, "Expected a structure name.");
    const auto type = internStructure(name.text);
    skipNewlines();
    expect(TokenKind::leftBrace, "Expected '{' to open the structure.");
    skipNewlines();
    std::vector<TypedName> fields;
    while (current_.kind != TokenKind::rightBrace) {
        const auto fieldType = parseType(expect(TokenKind::identifier, "Expected a field type."));
        auto field = expect(TokenKind::identifier, "Expected a field name.");
        fields.push_back({field.location, std::move(field.text), fieldType});
        if (current_.kind == TokenKind::rightBrace) break;
        expect(TokenKind::newline, "Expected a newline between fields.");
        skipNewlines();
    }
    expect(TokenKind::rightBrace, "Expected '}' after fields.");
    return {name.location, std::move(name.text), type, std::move(fields)};
}

Program Parser::parse() {
    Program program;
    skipNewlines();
    while (current_.kind != TokenKind::end) {
        const bool visibility = atWord("public") || atWord("private");
        const bool exported = !visibility || current_.text == "public";
        if (visibility) take();
        if (atWord("import")) {
            if (visibility) throw Diagnostic(current_.location, "ML2001", "An import cannot have a visibility modifier.");
            take();
            auto path = expect(TokenKind::string, "Expected a quoted source path after 'import'.");
            program.imports.push_back({path.location, std::move(path.text)});
            if (current_.kind != TokenKind::end) expect(TokenKind::newline, "Expected a newline after import.");
        } else if (atWord("struct")) {
            program.structures.push_back(parseStructure());
            program.structures.back().exported = exported;
        } else if (atWord("class")) {
            parseClass(program, exported);
        } else {
            program.functions.push_back(parseFunction());
            program.functions.back().exported = exported;
        }
        skipNewlines();
    }
    return program;
}
}
