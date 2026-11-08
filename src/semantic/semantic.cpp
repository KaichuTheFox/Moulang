#include "moulang/semantic.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <limits>

namespace moulang {
namespace {
struct Variable {
    Type type;
    bool nonNull = false;
    std::string borrowOwner{};
};

using Variables = std::unordered_map<std::string, Variable>;
template<class Declaration>
using Names = std::unordered_map<std::string, Declaration*>;

bool reserved(std::string_view name) {
    return namedType(name) != Type::inferred || name == "main" || name == "print" ||
        name == "var" || name == "true" || name == "false" ||
        name == "def" || name == "return" || name == "if" || name == "elif" || name == "else" ||
        name == "while" || name == "for" || name == "in" || name == "break" || name == "continue" ||
        name == "append" || name == "null" || name == "struct" || name == "foreach" || name == "import" ||
        name == "readText" || name == "writeText" || name == "public" || name == "private" ||
        name == "class" || name == "new" || name == "this";
}

const Variable& lookup(const Variables& variables, const std::string& name, Location location) {
    if (const auto variable = variables.find(name); variable != variables.end()) return variable->second;
    const auto message = "Undeclared variable '" + name + "'.";
    throw Diagnostic(location, "ML3004", message.c_str());
}

bool scalarAssignable(Type destination, Type source) {
    const auto base = nullableBase(destination);
    return destination == source || losslessNumeric(destination, source) ||
        (base != Type::inferred && (source == base || source == Type::nullValue || losslessNumeric(base, source)));
}

Type receiverType(const Variables& variables) {
    const auto receiver = variables.find("this");
    return receiver == variables.end() ? Type::none : receiver->second.type;
}

std::unique_ptr<Expression> implicitField(Location location, std::string name) {
    auto receiver = std::make_unique<Expression>(Expression{ExpressionKind::variable, location, "this", {}, {}});
    return std::make_unique<Expression>(Expression{
        ExpressionKind::member, location, std::move(name), std::move(receiver), {}
    });
}

void checkArrayMutation(const std::string& owner, const Variables& variables, Location location) {
    for (const auto& entry : variables) {
        if (entry.second.borrowOwner == owner) {
            throw Diagnostic(location, "ML3033", "Cannot modify a fixed array while a view borrows it.");
        }
    }
}

void narrow(const Expression& condition, bool whenTrue, Variables& variables) {
    if (condition.kind == ExpressionKind::unary && condition.text == "!") {
        narrow(*condition.left, !whenTrue, variables);
        return;
    }
    if (condition.kind != ExpressionKind::binary) return;
    if (condition.text == (whenTrue ? "&&" : "||")) {
        narrow(*condition.left, whenTrue, variables);
        narrow(*condition.right, whenTrue, variables);
        return;
    }
    if (condition.text != (whenTrue ? "!=" : "==")) return;
    const auto* name = condition.left.get();
    const auto* literal = condition.right.get();
    if (name->kind == ExpressionKind::nullLiteral) std::swap(name, literal);
    if (name->kind == ExpressionKind::variable && literal->kind == ExpressionKind::nullLiteral) {
        variables.at(name->text).nonNull = true;
    }
}

void forgetNarrowing(Variables& variables) {
    for (auto& [name, variable] : variables) variable.nonNull = false;
}

class Analyzer {
public:
    void check(Program& program, bool requireMain);

private:
    void checkStructures(Program& program);
    void checkLayout(const Structure& structure, std::unordered_set<Type>& visiting,
        std::unordered_set<Type>& checked) const;
    Type resolveType(Type type, Location location) const;
    void checkPublicType(Type type, Location location) const;
    bool canAssign(Type destination, Type source) const;
    bool canInitializeField(Type destination, const Expression& value) const;
    bool derivesFrom(Type source, Type base) const;
    bool hasField(Type owner, const std::string& name) const;
    Function* findMethod(Type receiver, const std::string& name) const;
    void checkBaseConstructors(Program& program);
    bool writable(const Expression& target) const;
    void checkInitialAssignments(Function& function, const Variables& parameters);
    void checkArguments(Expression& expression, const Variables& variables, Function& function, std::size_t first);
    template<class Declaration>
    Declaration* lookupName(const std::vector<Names<Declaration>>& local, const Names<Declaration>& exported,
        const std::string& name, std::size_t file) const {
        if (const auto found = local[file].find(name); found != local[file].end()) return found->second;
        if (const auto found = exported.find(name);
            found != exported.end() && reachable_[file].contains(found->second->location.file)) {
            return found->second;
        }
        return nullptr;
    }
    void checkFunction(Function& function);
    bool checkBlock(std::vector<Statement>& body, Variables variables, unsigned loopDepth,
        std::optional<Type>& returnType, bool inferReturn);
    Type checkExpression(Expression& expression, const Variables& variables,
        bool allowVoid = false, bool keepNullable = false);
    Type checkValue(Expression& expression, const Variables& variables, Type expected);

    Program* program_ = nullptr;
    std::vector<std::unordered_set<std::size_t>> reachable_;
    std::vector<Names<Function>> localFunctions_;
    std::vector<Names<const Structure>> localStructures_;
    Names<Function> exportedFunctions_;
    Names<const Structure> exportedStructures_;
    std::unordered_map<Type, const Structure*> structures_;
    std::unordered_map<Type, Names<Function>> methods_;
    std::unordered_map<Type, Function*> constructors_;
    std::unordered_set<Function*> checking_;
    std::unordered_set<Function*> checked_;
};

Type Analyzer::checkExpression(Expression& expression, const Variables& variables, bool allowVoid,
    bool keepNullable) {
    switch (expression.kind) {
    case ExpressionKind::integer:
        return expression.type = expression.integer >= (std::numeric_limits<std::int32_t>::min)() &&
            expression.integer <= (std::numeric_limits<std::int32_t>::max)() ? Type::integer : Type::longInt;
    case ExpressionKind::real: return expression.type = Type::float64;
    case ExpressionKind::boolean: return expression.type = Type::boolean;
    case ExpressionKind::string: return expression.type = Type::string;
    case ExpressionKind::nullLiteral: return expression.type = Type::nullValue;
    case ExpressionKind::variable: {
        if (!variables.contains(expression.text) && hasField(receiverType(variables), expression.text)) {
            expression = std::move(*implicitField(expression.location, std::move(expression.text)));
            return checkExpression(expression, variables, allowVoid, keepNullable);
        }
        const auto& variable = lookup(variables, expression.text, expression.location);
        expression.borrowOwner = variable.borrowOwner;
        return expression.type = variable.nonNull && !keepNullable ? nullableBase(variable.type) : variable.type;
    }
    case ExpressionKind::slice:
        for (auto& item : expression.arguments) {
            if (checkExpression(*item, variables) != Type::integer) {
                throw Diagnostic(item->location, "ML3018", "A slice literal must contain int values.");
            }
        }
        return expression.type = Type::intSlice;
    case ExpressionKind::subscript:
    case ExpressionKind::view: {
        const auto source = checkExpression(*expression.left, variables);
        if (!intCollection(source) ||
            checkExpression(*expression.right, variables) != Type::integer ||
            (expression.alternative && checkExpression(*expression.alternative, variables) != Type::integer)) {
            throw Diagnostic(expression.location, "ML3018", "Indexing requires an int collection and int indices.");
        }
        if (expression.kind == ExpressionKind::subscript) return expression.type = Type::integer;
        if (fixedIntArrayType(source)) {
            if (expression.left->kind != ExpressionKind::variable) {
                throw Diagnostic(expression.location, "ML3034", "A fixed-array view must borrow a local array.");
            }
            expression.borrowOwner = expression.left->text;
        } else {
            expression.borrowOwner = expression.left->borrowOwner;
        }
        return expression.type = expression.borrowOwner.empty() ? Type::intView : Type::borrowedIntView;
    }
    case ExpressionKind::member:
    case ExpressionKind::safeMember: {
        const bool safe = expression.kind == ExpressionKind::safeMember;
        auto type = checkExpression(*expression.left, variables, false, safe);
        if (safe) type = nullableBase(type);
        auto structure = structures_.find(type);
        if (structure == structures_.end()) {
            throw Diagnostic(expression.location, "ML3022", safe
                ? "Safe field access requires a nullable structure."
                : "Field access requires a non-null structure value.");
        }
        const auto name = structure->second->name;
        while (structure != structures_.end()) {
            const auto& declaration = *structure->second;
            for (unsigned i = 0; i < declaration.fields.size(); ++i) {
                if (declaration.fields[i].name == expression.text) {
                    if (!declaration.fields[i].isPublic && receiverType(variables) != declaration.type) {
                        throw Diagnostic(expression.location, "ML3035", "Cannot access a private field.");
                    }
                    expression.fieldIndex = i;
                    expression.memberOwner = declaration.type;
                    const auto field = declaration.fields[i].type;
                    return expression.type = safe && nullableBase(field) == Type::inferred ? nullableType(field) : field;
                }
            }
            structure = structures_.find(declaration.base);
        }
        const auto message = "Type '" + name + "' has no field '" + expression.text + "'.";
        throw Diagnostic(expression.location, "ML3022", message.c_str());
    }
    case ExpressionKind::constructObject: {
        const auto structure = lookupName(localStructures_, exportedStructures_, expression.text, expression.location.file);
        if (!structure || !structure->reference) {
            throw Diagnostic(expression.location, "ML3036", "'new' requires a visible class.");
        }
        if (const auto found = constructors_.find(structure->type); found != constructors_.end()) {
            auto& constructor = *found->second;
            if (!constructor.exported && receiverType(variables) != structure->type) {
                throw Diagnostic(expression.location, "ML3035", "Cannot access a private constructor.");
            }
            checkArguments(expression, variables, constructor, 1);
        } else if (!expression.arguments.empty() ||
            std::ranges::any_of(structure->fields, [](const auto& field) { return !field.defaultValue; })) {
            throw Diagnostic(expression.location, "ML3036", "Class has no matching constructor.");
        }
        return expression.type = structure->type;
    }
    case ExpressionKind::methodCall:
    case ExpressionKind::safeMethodCall: {
        const bool safe = expression.kind == ExpressionKind::safeMethodCall;
        auto receiver = checkExpression(*expression.left, variables, false, safe);
        if (safe) receiver = nullableBase(receiver);
        const auto method = findMethod(receiver, expression.text);
        if (!method) {
            throw Diagnostic(expression.location, "ML3035", "Unknown method on the receiver type.");
        }
        auto& function = *method;
        if (!function.exported && receiverType(variables) != function.owner) {
            throw Diagnostic(expression.location, "ML3035", "Cannot access a private method.");
        }
        checkArguments(expression, variables, function, 1);
        if (function.result == Type::inferred) checkFunction(function);
        if (function.result == Type::none && !allowVoid) {
            throw Diagnostic(expression.location, "ML3012", "A void method has no value.");
        }
        if (safe && function.result != Type::none && nullableBase(function.result) == Type::inferred) {
            const auto result = nullableType(function.result);
            if (result == Type::inferred) {
                throw Diagnostic(expression.location, "ML3035", "Method result does not have a nullable form.");
            }
            return expression.type = result;
        }
        return expression.type = function.result;
    }
    case ExpressionKind::call: {
        if (expression.text == "readText" || expression.text == "writeText") {
            const bool read = expression.text == "readText";
            if (expression.arguments.size() != (read ? 1u : 2u)) {
                throw Diagnostic(expression.location, "ML3011", "Wrong number of text file arguments.");
            }
            for (const auto& arg : expression.arguments) {
                if (checkExpression(*arg, variables) != Type::string) {
                    throw Diagnostic(arg->location, "ML3011", "Text file arguments must have type string.");
                }
            }
            if (!read && !allowVoid) {
                throw Diagnostic(expression.location, "ML3012", "A void function has no value.");
            }
            return expression.type = read ? Type::string : Type::none;
        }
        if (const auto target = namedType(expression.text); numericType(target)) {
            if (expression.arguments.size() != 1 || !numericType(checkExpression(*expression.arguments[0], variables))) {
                throw Diagnostic(expression.location, "ML3020", "A numeric cast requires exactly one numeric value.");
            }
            return expression.type = target;
        }
        if (expression.text == "append") {
            if (expression.arguments.size() != 2 ||
                checkExpression(*expression.arguments[0], variables) != Type::intSlice ||
                checkExpression(*expression.arguments[1], variables) != Type::integer) {
                throw Diagnostic(expression.location, "ML3018", "Append requires an int slice and an int value.");
            }
            return expression.type = Type::intSlice;
        }
        const auto file = expression.location.file;
        if (const auto constructor = localFunctions_[file].contains(expression.text) ? nullptr :
            lookupName(localStructures_, exportedStructures_, expression.text, file)) {
            const auto& structure = *constructor;
            if (structure.reference) {
                throw Diagnostic(expression.location, "ML3036", "Use 'new' to construct a class.");
            }
            if (expression.arguments.size() != structure.fields.size()) {
                throw Diagnostic(expression.location, "ML3023", "Expected one initializer per structure field.");
            }
            for (std::size_t i = 0; i < expression.arguments.size(); ++i) {
                auto& value = *expression.arguments[i];
                checkExpression(value, variables);
                if (!canInitializeField(structure.fields[i].type, value)) {
                    const auto message = "Initializer type does not match field '" + structure.fields[i].name + "'.";
                    throw Diagnostic(value.location, "ML3023", message.c_str());
                }
            }
            return expression.type = structure.type;
        }
        const auto found = lookupName(localFunctions_, exportedFunctions_, expression.text, file);
        if (!found || expression.text == "main") {
            throw Diagnostic(expression.location, "ML3010", "Unknown callable function.");
        }
        std::vector<Function*> candidates;
        for (auto& candidate : program_->functions) {
            if (candidate.owner == Type::none && candidate.name == expression.text &&
                candidate.location.file == found->location.file &&
                (candidate.location.file == file || candidate.exported)) candidates.push_back(&candidate);
        }
        Function* selected = found;
        if (candidates.size() > 1) {
            std::vector<Type> actual;
            for (auto& argument : expression.arguments) actual.push_back(checkExpression(*argument, variables));
            const auto rank = [&](Type target, Type source) {
                if (target == source) return 0;
                if (!canAssign(target, source)) return 10000;
                if (integerBits(target) && integerBits(source)) return 100 + integerBits(target);
                if (realType(target) && realType(source)) return 200 + (target == Type::float32 ? 32 : 64);
                if (realType(target) && integerBits(source)) return 300 + (target == Type::float32 ? 32 : 64);
                return 400;
            };
            std::vector<std::pair<Function*, std::vector<int>>> viable;
            for (auto* candidate : candidates) {
                if (candidate->parameters.size() != actual.size()) continue;
                std::vector<int> ranks;
                for (std::size_t i = 0; i < actual.size(); ++i)
                    ranks.push_back(rank(candidate->parameters[i].type, actual[i]));
                if (std::ranges::none_of(ranks, [](int value) { return value == 10000; }))
                    viable.emplace_back(candidate, std::move(ranks));
            }
            selected = nullptr;
            for (const auto& candidate : viable) {
                bool best = true;
                for (const auto& other : viable) {
                    if (candidate.first == other.first) continue;
                    bool strictlyBetter = false;
                    for (std::size_t i = 0; i < actual.size(); ++i) {
                        if (candidate.second[i] > other.second[i]) best = false;
                        if (candidate.second[i] < other.second[i]) strictlyBetter = true;
                    }
                    if (!strictlyBetter) best = false;
                }
                if (best) selected = candidate.first;
            }
            if (!selected) throw Diagnostic(expression.location, "ML3011", viable.empty()
                ? "No overload accepts these argument types." : "Ambiguous function overload; use explicit conversions.");
        }
        auto& function = *selected;
        checkArguments(expression, variables, function, 0);
        if (function.result == Type::inferred) checkFunction(function);
        if (function.result == Type::none && !allowVoid) {
            throw Diagnostic(expression.location, "ML3012", "A void function has no value.");
        }
        return expression.type = function.result;
    }
    case ExpressionKind::unary: {
        const auto operand = checkExpression(*expression.left, variables);
        const auto& op = expression.text;
        if ((op == "!" && operand == Type::boolean) || (op == "~" && integerBits(operand)) ||
            (op == "-" && numericType(operand))) {
            return expression.type = operand;
        }
        break;
    }
    case ExpressionKind::conditional: {
        if (checkExpression(*expression.left, variables) != Type::boolean) {
            throw Diagnostic(expression.location, "ML3014", "A condition must have type bool.");
        }
        auto positive = variables;
        auto negative = variables;
        narrow(*expression.left, true, positive);
        narrow(*expression.left, false, negative);
        const auto yes = checkExpression(*expression.right, positive);
        const auto no = checkExpression(*expression.alternative, negative);
        if (yes == Type::borrowedIntView && no == yes) {
            if (expression.right->borrowOwner != expression.alternative->borrowOwner) {
                throw Diagnostic(expression.location, "ML3034", "A borrowed conditional view must have one owner.");
            }
            expression.borrowOwner = expression.right->borrowOwner;
        }
        if ((yes == Type::longInt && realType(no)) || (no == Type::longInt && realType(yes)))
            throw Diagnostic(expression.location, "ML3026", "Mixed long and real values require an explicit conversion.");
        if (numericType(yes) && numericType(no)) return expression.type = commonNumeric(yes, no);
        if (yes != Type::nullValue && canAssign(yes, no)) return expression.type = yes;
        if (no != Type::nullValue && canAssign(no, yes)) return expression.type = no;
        throw Diagnostic(expression.location, "ML3026", "Conditional branches must have compatible value types.");
    }
    case ExpressionKind::binary: {
        const auto& op = expression.text;
        const bool nullCheck = (op == "==" || op == "!=") &&
            (expression.left->kind == ExpressionKind::nullLiteral || expression.right->kind == ExpressionKind::nullLiteral);
        const auto left = checkExpression(*expression.left, variables, false, nullCheck || op == "??");
        if (op == "++" || op == "--") {
            if (!numericType(left)) break;
            expression.right->type = left;
            expression.operandType = left;
            return expression.type = left;
        }
        const bool logical = op == "&&" || op == "||";
        Variables rightVariables;
        if (logical) {
            rightVariables = variables;
            narrow(*expression.left, op == "&&", rightVariables);
        }
        const auto right = checkExpression(*expression.right, logical ? rightVariables : variables, false, nullCheck);
        if (op == "??") {
            if (const auto base = nullableBase(left); base != Type::inferred && canAssign(base, right)) {
                return expression.type = base;
            }
            throw Diagnostic(expression.location, "ML3019", "'??' requires a nullable value and a matching default.");
        }
        const bool shift = op == "<<" || op == ">>";
        if (shift || op == "&" || op == "|" || op == "^") {
            if (!integerBits(left) || !integerBits(right)) break;
            expression.operandType = shift ? left : commonNumeric(left, right);
            return expression.type = expression.operandType;
        }
        if (numericType(left) && numericType(right) && !logical) {
            if ((left == Type::longInt && realType(right)) || (right == Type::longInt && realType(left)))
                throw Diagnostic(expression.location, "ML3005", "Mixed long and real values require an explicit conversion.");
            expression.operandType = commonNumeric(left, right);
            const bool arithmetic = op == "+" || op == "-" || op == "*" || op == "/";
            return expression.type = arithmetic ? expression.operandType : Type::boolean;
        }
        if (logical) {
            if (left == Type::boolean && right == left) return expression.type = Type::boolean;
        } else if (op == "==" || op == "!=") {
            if ((left == right && (left == Type::boolean || left == Type::string)) ||
                (left == Type::nullValue && nullableBase(right) != Type::inferred) ||
                (right == Type::nullValue && nullableBase(left) != Type::inferred)) {
                return expression.type = Type::boolean;
            }
        }
        break;
    }
    }
    throw Diagnostic(expression.location, "ML3005", "Invalid operand types for operator.");
}

Type Analyzer::checkValue(Expression& expression, const Variables& variables, Type expected) {
    const auto actual = checkExpression(expression, variables);
    if (fixedIntArrayType(expected) && expression.kind == ExpressionKind::slice) {
        if (expression.arguments.size() != fixedIntArrayLength(expected)) {
            throw Diagnostic(expression.location, "ML3032", "Fixed array initializer length does not match the declaration.");
        }
        return expression.type = expected;
    }
    return actual;
}

void Analyzer::checkArguments(Expression& expression, const Variables& variables, Function& function, std::size_t first) {
    expression.functionIndex = static_cast<std::size_t>(&function - program_->functions.data());
    if (expression.arguments.size() != function.parameters.size() - first) {
        throw Diagnostic(expression.location, "ML3011", "Wrong number of function arguments.");
    }
    for (std::size_t i = 0; i < expression.arguments.size(); ++i) {
        auto& argument = *expression.arguments[i];
        if (!canAssign(function.parameters[i + first].type, checkExpression(argument, variables))) {
            throw Diagnostic(argument.location, "ML3011", "Argument type does not match parameter type.");
        }
    }
}

bool Analyzer::hasField(Type owner, const std::string& name) const {
    for (auto structure = structures_.find(owner); structure != structures_.end();
        structure = structures_.find(structure->second->base)) {
        if (std::ranges::any_of(structure->second->fields, [&](const auto& field) { return field.name == name; })) {
            return true;
        }
    }
    return false;
}

Function* Analyzer::findMethod(Type receiver, const std::string& name) const {
    for (auto structure = structures_.find(receiver); structure != structures_.end();
        structure = structures_.find(structure->second->base)) {
        if (const auto methods = methods_.find(structure->first); methods != methods_.end()) {
            if (const auto method = methods->second.find(name); method != methods->second.end()) return method->second;
        }
    }
    return nullptr;
}

bool Analyzer::derivesFrom(Type source, Type base) const {
    for (auto structure = structures_.find(source); structure != structures_.end() &&
        structure->second->base != Type::none; structure = structures_.find(structure->second->base)) {
        if (structure->second->base == base) return true;
    }
    return false;
}

bool Analyzer::canAssign(Type destination, Type source) const {
    if (scalarAssignable(destination, source) || derivesFrom(source, destination)) return true;
    const auto base = nullableBase(destination);
    return base != Type::inferred &&
        (derivesFrom(source, base) || derivesFrom(nullableBase(source), base));
}

bool Analyzer::canInitializeField(Type destination, const Expression& value) const {
    if (canAssign(destination, value.type)) return true;
    // A positional float field can give an exact integer literal its context.
    return destination == Type::float32 && value.type == Type::integer && value.kind == ExpressionKind::integer &&
        static_cast<double>(static_cast<float>(value.integer)) == static_cast<double>(value.integer);
}

bool Analyzer::writable(const Expression& target) const {
    if (target.kind == ExpressionKind::variable) return true;
    if (target.kind != ExpressionKind::member) return false;
    const auto receiver = structures_.find(target.left->type);
    return (receiver != structures_.end() && receiver->second->reference) || writable(*target.left);
}

bool Analyzer::checkBlock(std::vector<Statement>& body, Variables variables, unsigned loopDepth,
    std::optional<Type>& returnType, bool inferReturn) {
    bool terminates = false;
    for (auto& statement : body) {
        const auto location = statement.location;
        if (statement.kind == StatementKind::assignment && !variables.contains(statement.name) &&
            hasField(receiverType(variables), statement.name)) {
            statement.target = implicitField(location, statement.name);
            statement.kind = StatementKind::fieldAssignment;
        }
        switch (statement.kind) {
        case StatementKind::print: {
            const auto type = checkExpression(*statement.value, variables);
            if (!numericType(type) && type != Type::boolean && type != Type::string) {
                throw Diagnostic(location, "ML3018", "Print requires a non-null scalar value.");
            }
            break;
        }
        case StatementKind::call:
            checkExpression(*statement.value, variables, true);
            break;
        case StatementKind::fieldAssignment:
            statement.type = checkExpression(*statement.target, variables);
            if (statement.target->kind == ExpressionKind::member) {
                if (!writable(*statement.target)) {
                    throw Diagnostic(location, "ML3035", "Field assignment requires writable storage.");
                }
            } else {
                const auto& target = *statement.target;
                if (target.kind != ExpressionKind::subscript || !fixedIntArrayType(target.left->type) ||
                    target.left->kind != ExpressionKind::variable) {
                    throw Diagnostic(location, "ML3034", "Only a local fixed array supports indexed assignment.");
                }
                checkArrayMutation(target.left->text, variables, location);
            }
            if (!canAssign(statement.type, checkValue(*statement.value, variables, statement.type))) {
                throw Diagnostic(location, "ML3007", "Field and expression types must match.");
            }
            break;
        case StatementKind::declaration:
        case StatementKind::assignment: {
            const bool declaration = statement.kind == StatementKind::declaration;
            if (!declaration && statement.name == "this") {
                throw Diagnostic(location, "ML3035", "Cannot reassign the method receiver.");
            }
            statement.type = declaration ? resolveType(statement.type, location)
                : lookup(variables, statement.name, location).type;
            if (!declaration && fixedIntArrayType(statement.type)) checkArrayMutation(statement.name, variables, location);
            if (!declaration && statement.type == Type::borrowedIntView) {
                throw Diagnostic(location, "ML3034", "A borrowed view binding cannot be reassigned.");
            }
            const auto type = checkValue(*statement.value, variables, statement.type);
            if (declaration) {
                if (reserved(statement.name) || variables.contains(statement.name)) {
                    throw Diagnostic(location, "ML3006", "Reserved or duplicate variable name.");
                }
                if (statement.type == Type::inferred) {
                    if (type == Type::nullValue) {
                        throw Diagnostic(location, "ML3019", "Specify a nullable type when initializing with null.");
                    }
                    statement.type = type;
                }
                variables.emplace(statement.name, Variable{statement.type, false, statement.value->borrowOwner});
            }
            if (!canAssign(statement.type, type)) {
                throw Diagnostic(location, "ML3007", "Variable and expression types must match.");
            }
            const auto base = nullableBase(statement.type);
            variables.at(statement.name).nonNull = base != Type::inferred && canAssign(base, type);
            break;
        }
        case StatementKind::returnValue: {
            const auto type = statement.value ? checkExpression(*statement.value, variables) : Type::none;
            if (type == Type::borrowedIntView) {
                throw Diagnostic(location, "ML3034", "A view of a local fixed array cannot escape the function.");
            }
            const bool compatible = returnType && (inferReturn ? *returnType == type : canAssign(*returnType, type));
            if (returnType ? !compatible : type == Type::nullValue) {
                throw Diagnostic(location, "ML3013", "Return type does not match the function.");
            }
            if (!returnType) returnType = type;
            statement.type = *returnType;
            terminates = true;
            break;
        }
        case StatementKind::branch:
        case StatementKind::whileLoop: {
            const bool loop = statement.kind == StatementKind::whileLoop;
            if (loop) forgetNarrowing(variables);
            if (checkExpression(*statement.value, variables) != Type::boolean) {
                throw Diagnostic(location, "ML3014", "A condition must have type bool.");
            }
            auto positive = variables;
            narrow(*statement.value, true, positive);
            const bool first = checkBlock(statement.body, std::move(positive), loopDepth + loop, returnType, inferReturn);
            if (!loop) {
                auto negative = variables;
                narrow(*statement.value, false, negative);
                const bool second = checkBlock(statement.alternative, std::move(negative), loopDepth, returnType, inferReturn);
                terminates = terminates || (first && second);
            }
            forgetNarrowing(variables);
            break;
        }
        case StatementKind::forLoop:
        case StatementKind::foreachLoop: {
            const auto source = checkExpression(*statement.value, variables);
            if (statement.kind == StatementKind::foreachLoop) {
                if (!intCollection(source)) {
                    throw Diagnostic(location, "ML3027", "Foreach requires an int slice or view.");
                }
            } else if (source != Type::integer || checkExpression(*statement.bound, variables) != Type::integer) {
                throw Diagnostic(location, "ML3014", "For loop start and limit must have type int.");
            }
            auto scoped = variables;
            forgetNarrowing(scoped);
            if (reserved(statement.name) || !scoped.emplace(statement.name, Variable{Type::integer}).second) {
                throw Diagnostic(location, "ML3006", "Reserved or duplicate loop variable name.");
            }
            checkBlock(statement.body, std::move(scoped), loopDepth + 1, returnType, inferReturn);
            forgetNarrowing(variables);
            break;
        }
        case StatementKind::breakLoop:
        case StatementKind::continueLoop:
            if (!loopDepth) throw Diagnostic(location, "ML3015", "This statement must be inside a loop.");
            terminates = true;
            break;
        }
    }
    return terminates;
}

void Analyzer::checkFunction(Function& function) {
    if (checked_.contains(&function)) return;
    if (!checking_.insert(&function).second) {
        throw Diagnostic(function.location, "ML3016", "Recursive return inference requires an explicit return type.");
    }
    Variables variables;
    for (const auto& parameter : function.parameters) {
        const bool receiver = function.owner != Type::none && &parameter == &function.parameters.front();
        if ((!receiver && reserved(parameter.name)) ||
            !variables.emplace(parameter.name, Variable{parameter.type}).second) {
            throw Diagnostic(parameter.location, "ML3006", "Reserved or duplicate parameter name.");
        }
    }
    if (function.constructor) {
        auto parameters = variables;
        parameters.erase("this");
        checkInitialAssignments(function, parameters);
    }
    std::optional<Type> returnType;
    if (function.result != Type::inferred) returnType = function.result;
    const bool terminates = checkBlock(function.body, std::move(variables), 0, returnType, !returnType);
    function.result = returnType.value_or(Type::none);
    if (function.exported && (function.owner == Type::none || structures_.at(function.owner)->exported)) {
        checkPublicType(function.result, function.location);
    }
    if (function.result != Type::none && !terminates) {
        throw Diagnostic(function.location, "ML3013", "This function must return a value on every path.");
    }
    checking_.erase(&function);
    checked_.insert(&function);
}

void Analyzer::checkInitialAssignments(Function& function, const Variables& parameters) {
    std::unordered_set<std::string> pending;
    for (const auto& field : structures_.at(function.owner)->fields) {
        if (!field.defaultValue) pending.insert(field.name);
    }
    for (auto& statement : function.body) {
        if (pending.empty()) break;
        const std::string* name = nullptr;
        if (statement.kind == StatementKind::fieldAssignment && statement.target->kind == ExpressionKind::member &&
            statement.target->left->kind == ExpressionKind::variable && statement.target->left->text == "this") {
            name = &statement.target->text;
        } else if (statement.kind == StatementKind::assignment && !parameters.contains(statement.name)) {
            name = &statement.name;
        }
        if (!name || !pending.erase(*name)) break;
        checkExpression(*statement.value, parameters);
    }
    if (!pending.empty()) {
        throw Diagnostic(function.location, "ML3037",
            "Constructor must initialize every field without a default before using the object.");
    }
}

Type Analyzer::resolveType(Type type, Location location) const {
    const auto base = nullableBase(type);
    const bool nullable = structureType(base);
    if (nullable) type = base;
    if (!structureType(type)) return type;
    const auto index = static_cast<std::uint32_t>(type) - static_cast<std::uint32_t>(Type::firstStructure);
    const auto& reference = program_->typeReferences.at(index);
    const auto found = lookupName(localStructures_, exportedStructures_, reference.name, reference.file);
    if (!found) {
        const auto message = "Unknown or inaccessible structure type '" + reference.name + "'.";
        throw Diagnostic(location, "ML3021", message.c_str());
    }
    return nullable ? nullableType(found->type) : found->type;
}

void Analyzer::checkPublicType(Type type, Location location) const {
    if (const auto base = nullableBase(type); structureType(base)) type = base;
    if (structureType(type) && !structures_.at(type)->exported) {
        throw Diagnostic(location, "ML3031", "A public declaration cannot expose a private type.");
    }
}

void Analyzer::checkLayout(const Structure& structure, std::unordered_set<Type>& visiting,
    std::unordered_set<Type>& checked) const {
    if (checked.contains(structure.type)) return;
    if (!visiting.insert(structure.type).second) {
        throw Diagnostic(structure.location, structure.reference ? "ML3038" : "ML3025", structure.reference
            ? "Cyclic class inheritance." : "A structure cannot contain itself by value.");
    }
    if (structure.reference) {
        if (structure.base != Type::none) checkLayout(*structures_.at(structure.base), visiting, checked);
    } else {
        for (const auto& field : structure.fields) {
            const auto type = structureType(field.type) ? field.type : nullableBase(field.type);
            if (structureType(type) && !structures_.at(type)->reference) {
                checkLayout(*structures_.at(type), visiting, checked);
            }
        }
    }
    visiting.erase(structure.type);
    checked.insert(structure.type);
}

void Analyzer::checkStructures(Program& program) {
    for (const auto& structure : program.structures) {
        if (reserved(structure.name) ||
            !localStructures_[structure.location.file].emplace(structure.name, &structure).second ||
            (structure.exported && !exportedStructures_.emplace(structure.name, &structure).second)) {
            throw Diagnostic(structure.location, "ML3024", "Reserved or duplicate structure name.");
        }
        structures_.emplace(structure.type, &structure);
    }
    for (auto& structure : program.structures) {
        if (structure.base != Type::none) {
            structure.base = resolveType(structure.base, structure.location);
            if (!structureType(structure.base) || !structures_.at(structure.base)->reference) {
                throw Diagnostic(structure.location, "ML3038", "A base type must be a class.");
            }
            if (structure.exported) checkPublicType(structure.base, structure.location);
        }
        std::unordered_set<std::string> names;
        for (auto& field : structure.fields) {
            field.type = resolveType(field.type, field.location);
            if (structure.exported && field.isPublic) checkPublicType(field.type, field.location);
            if (field.type == Type::none || reserved(field.name) || !names.insert(field.name).second) {
                throw Diagnostic(field.location, "ML3024", "Invalid field type or reserved or duplicate field name.");
            }
        }
    }
    std::unordered_set<Type> visiting, checked;
    for (const auto& structure : program.structures) checkLayout(structure, visiting, checked);
}

void Analyzer::checkBaseConstructors(Program& program) {
    for (auto& structure : program.structures) {
        if (structure.base == Type::none) continue;
        const auto base = structures_.at(structure.base);
        if (const auto constructor = constructors_.find(base->type); constructor != constructors_.end()) {
            if (!constructor->second->exported || constructor->second->parameters.size() != 1) {
                throw Diagnostic(structure.location, "ML3038", "The base class needs a public zero-argument constructor.");
            }
            structure.baseConstructor = static_cast<std::size_t>(constructor->second - program.functions.data());
        } else if (std::ranges::any_of(base->fields, [](const auto& field) { return !field.defaultValue; })) {
            throw Diagnostic(structure.location, "ML3038", "The base class needs a default constructor.");
        }
        for (const auto& field : structure.fields) {
            if (hasField(base->type, field.name) || findMethod(base->type, field.name)) {
                throw Diagnostic(field.location, "ML3038", "A derived field cannot hide an inherited member.");
            }
        }
        if (const auto methods = methods_.find(structure.type); methods != methods_.end()) {
            for (const auto& [name, method] : methods->second) {
                if (findMethod(base->type, name)) {
                    throw Diagnostic(method->location, "ML3038", "Method overrides are not supported.");
                }
            }
        }
    }
}

void Analyzer::check(Program& program, bool requireMain) {
    program_ = &program;
    const auto count = program.dependencies.size();
    localFunctions_.resize(count);
    localStructures_.resize(count);
    reachable_.resize(count);
    for (std::size_t file = 0; file < count; ++file) {
        auto pending = program.dependencies[file];
        while (!pending.empty()) {
            const auto dependency = pending.back();
            pending.pop_back();
            if (reachable_[file].insert(dependency).second) {
                const auto& transitive = program.dependencies[dependency];
                pending.insert(pending.end(), transitive.begin(), transitive.end());
            }
        }
    }
    checkStructures(program);
    for (auto& function : program.functions) {
        function.result = resolveType(function.result, function.location);
        const bool publicApi = function.exported &&
            (function.owner == Type::none || structures_.at(function.owner)->exported);
        if (publicApi) checkPublicType(function.result, function.location);
        for (auto& parameter : function.parameters) {
            parameter.type = resolveType(parameter.type, parameter.location);
            if (publicApi && (function.owner == Type::none || &parameter != &function.parameters.front())) {
                checkPublicType(parameter.type, parameter.location);
            }
        }
        if (function.owner != Type::none) {
            if (function.constructor) {
                if (!constructors_.emplace(function.owner, &function).second) {
                    throw Diagnostic(function.location, "ML3036", "A class supports one explicit constructor.");
                }
            } else if (reserved(function.name) || hasField(function.owner, function.name) ||
                !methods_[function.owner].emplace(function.name, &function).second) {
                throw Diagnostic(function.location, "ML3035", "Reserved or duplicate class member name.");
            }
            continue;
        }
        const auto file = function.location.file;
        const auto previous = localFunctions_[file].find(function.name);
        bool duplicate = false;
        if (previous != localFunctions_[file].end()) {
            for (const auto& prior : program.functions) {
                if (&prior == &function) break;
                if (prior.owner != Type::none || prior.location.file != file || prior.name != function.name ||
                    prior.parameters.size() != function.parameters.size()) continue;
                bool same = true;
                for (std::size_t i = 0; i < prior.parameters.size(); ++i)
                    same = same && prior.parameters[i].type == function.parameters[i].type;
                duplicate = duplicate || same;
            }
        }
        const auto exported = exportedFunctions_.find(function.name);
        if (duplicate || (previous != localFunctions_[file].end() && function.name == "main") ||
            (function.exported && exported != exportedFunctions_.end() && exported->second->location.file != file)) {
            if (function.name == "main") {
                throw Diagnostic(function.location, "ML3002", "Duplicate program entry point 'main'.");
            }
            throw Diagnostic(function.location, "ML3017", "Duplicate function name.");
        }
        localFunctions_[file].emplace(function.name, &function);
        if (function.exported) exportedFunctions_.emplace(function.name, &function);
        if (function.name == "main") {
            if (function.result != Type::none || !function.parameters.empty()) {
                throw Diagnostic(function.location, "ML3017", "The entry point must be 'main()' or 'void main()'.");
            }
        } else if (reserved(function.name) || localStructures_[file].contains(function.name) ||
            (function.exported && exportedStructures_.contains(function.name))) {
            throw Diagnostic(function.location, "ML3017", "Reserved function name or name already used by a structure.");
        }
    }
    if (requireMain && !localFunctions_[0].contains("main")) {
        throw Diagnostic({1, 1}, "ML3001", "Missing program entry point 'main'.");
    }
    checkBaseConstructors(program);
    for (auto& structure : program.structures) {
        for (auto& field : structure.fields) {
            if (field.defaultValue &&
                !canAssign(field.type, checkValue(*field.defaultValue, {}, field.type))) {
                throw Diagnostic(field.location, "ML3007", "Field default does not match its type.");
            }
        }
    }
    for (auto& function : program.functions) checkFunction(function);
}
}

void analyze(Program& program, bool requireMain) {
    Analyzer().check(program, requireMain);
}
}
