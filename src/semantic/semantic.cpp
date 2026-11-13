#include "moulang/semantic.hpp"
#include "moulang/standard.hpp"

#include <algorithm>
#include <array>
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
    return namedType(name) != Type::inferred || name == "string" || name == "type" || name == "len" ||
        name == "main" || name == "print" ||
        name == "var" || name == "true" || name == "false" ||
        name == "def" || name == "return" || name == "if" || name == "elif" || name == "else" ||
        name == "while" || name == "for" || name == "in" || name == "break" || name == "continue" ||
        name == "append" || name == "null" || name == "struct" || name == "foreach" || name == "import" ||
        name == "readText" || name == "writeText" || name == "RText" || name == "WText" || name == "AText" ||
        name == "public" || name == "private" ||
        name == "class" || name == "new" || name == "this";
}

constexpr std::size_t maxSuggestedName = 64;

std::size_t readName(std::string_view text, std::array<char32_t, maxSuggestedName>& points) {
    std::size_t count = 0;
    while (!text.empty() && count < points.size()) points[count++] = takeCodePoint(text);
    return text.empty() ? count : points.size() + 1;
}

class NameSuggestion {
public:
    explicit NameSuggestion(std::string_view name) {
        size_ = readName(name, points_);
        distance_ = size_ <= 4 ? 2 : 3;
    }

    void consider(std::string_view name) {
        if (size_ < 3 || size_ > points_.size() || name == best_) return;
        std::array<char32_t, maxSuggestedName> candidate;
        const auto length = readName(name, candidate);
        if (length > candidate.size() || length + distance_ < size_ || size_ + distance_ < length) return;
        std::array<std::size_t, maxSuggestedName + 1> row;
        for (std::size_t j = 0; j <= length; ++j) row[j] = j;
        for (std::size_t i = 1; i <= size_; ++i) {
            auto diagonal = row[0];
            row[0] = i;
            for (std::size_t j = 1; j <= length; ++j) {
                const auto above = row[j];
                row[j] = (std::min)({above + 1, row[j - 1] + 1, diagonal + (points_[i - 1] != candidate[j - 1])});
                diagonal = above;
            }
        }
        const auto cost = row[length];
        if (!cost) return;
        if (cost < distance_) {
            best_ = name;
            distance_ = cost;
            tied_ = false;
        } else if (cost == distance_) {
            tied_ = true;
        }
    }

    std::string hint(std::string fallback) const {
        if (best_.empty() || tied_) return fallback;
        return "Did you mean '" + std::string(best_) + "'?";
    }

private:
    std::array<char32_t, maxSuggestedName> points_{};
    std::size_t size_ = 0;
    std::size_t distance_ = 0;
    std::string_view best_;
    bool tied_ = false;
};

bool scalarAssignable(Type destination, Type source) {
    const auto base = nullableBase(destination);
    return destination == source || losslessNumeric(destination, source) ||
        (base != Type::inferred && (source == base || source == Type::nullValue || losslessNumeric(base, source)));
}

bool emptyListTree(const Expression& expression) {
    return expression.kind == ExpressionKind::slice &&
        std::ranges::all_of(expression.arguments, [](const auto& item) { return emptyListTree(*item); });
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

constexpr int incompatibleConversion = 10000;

template<class Candidate>
Candidate bestOverload(const std::vector<std::pair<Candidate, std::vector<int>>>& viable) {
    for (const auto& candidate : viable) {
        const bool best = std::ranges::all_of(viable, [&](const auto& other) {
            if (&candidate == &other) return true;
            bool strictlyBetter = false;
            for (std::size_t i = 0; i < candidate.second.size(); ++i) {
                if (candidate.second[i] > other.second[i]) return false;
                strictlyBetter = strictlyBetter || candidate.second[i] < other.second[i];
            }
            return strictlyBetter;
        });
        if (best) return candidate.first;
    }
    return nullptr;
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
    int conversionRank(Type destination, Type source) const;
    Type checkStandardCall(Expression& expression, const Variables& variables, StandardModule module, bool allowVoid);
    bool canInitializeField(Type destination, const Expression& value) const;
    bool derivesFrom(Type source, Type base) const;
    bool hasField(Type owner, const std::string& name) const;
    const Variable& lookup(const Variables& variables, const std::string& name, Location location,
        bool fieldCandidates = false, bool atName = true) const;
    std::string typeName(Type type) const;
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
    std::vector<std::unordered_map<std::string, StandardModule>> standardModules_;
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
        const auto& variable = lookup(variables, expression.text, expression.location, true);
        expression.borrowOwner = variable.borrowOwner;
        return expression.type = variable.nonNull && !keepNullable ? nullableBase(variable.type) : variable.type;
    }
    case ExpressionKind::slice: {
        Type element = Type::inferred;
        std::vector<Expression*> empty;
        for (auto& item : expression.arguments) {
            if (emptyListTree(*item)) {
                empty.push_back(item.get());
                continue;
            }
            const auto itemType = checkExpression(*item, variables);
            if (itemType != Type::integer && itemType != Type::string && !stringListDepth(itemType)) {
                throw Diagnostic(item->location, "ML3018", "A collection literal requires int, str, or string list elements.");
            }
            if (element != Type::inferred && element != itemType) {
                throw Diagnostic(item->location, "ML3018", "A collection literal requires elements of the same type.");
            }
            element = itemType;
        }
        if (!empty.empty() && !stringListDepth(element)) {
            throw Diagnostic(empty.front()->location, "ML3018", "An empty nested literal requires a string list element type.");
        }
        for (auto* item : empty) checkValue(*item, variables, element);
        if (element == Type::integer || element == Type::inferred) return expression.type = Type::intSlice;
        const auto depth = stringListDepth(element) + 1;
        if (depth > maxStringListDepth) {
            throw Diagnostic(expression.location, "ML3018", "A string list literal supports up to two dimensions.");
        }
        return expression.type = stringList(depth);
    }
    case ExpressionKind::subscript:
    case ExpressionKind::view: {
        const auto source = checkExpression(*expression.left, variables);
        if (source == Type::string) {
            for (auto* bound : {expression.right.get(), expression.alternative.get(), expression.step.get()}) {
                if (bound && checkExpression(*bound, variables) != Type::integer) {
                    throw Diagnostic(bound->location, "ML3018", "String indices, bounds and steps must have type int.");
                }
            }
            return expression.type = Type::string;
        }
        if (stringListDepth(source)) {
            if (expression.kind != ExpressionKind::subscript ||
                checkExpression(*expression.right, variables) != Type::integer) {
                throw Diagnostic(expression.location, "ML3018", "A string list requires a single int index.");
            }
            return expression.type = listElement(source);
        }
        if (!intCollection(source) ||
            !expression.right || expression.step ||
            (expression.kind == ExpressionKind::view && !expression.alternative) ||
            checkExpression(*expression.right, variables) != Type::integer ||
            (expression.alternative && checkExpression(*expression.alternative, variables) != Type::integer)) {
            throw Diagnostic(expression.location, "ML3018", "Indexing requires an int collection and int indices.");
        }
        if (expression.kind == ExpressionKind::subscript) return expression.type = Type::integer;
        if (source == Type::intList || source == Type::intListView) return expression.type = Type::intListView;
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
        const auto& target = *expression.left;
        if (target.kind == ExpressionKind::variable && !variables.contains(target.text) &&
            !hasField(receiverType(variables), target.text)) {
            const auto& aliases = standardModules_[expression.location.file];
            if (const auto alias = aliases.find(target.text); alias != aliases.end()) {
                if (safe) throw Diagnostic(expression.location, "ML3045", "A standard module call uses '.'.");
                return checkStandardCall(expression, variables, alias->second, allowVoid);
            }
        }
        auto receiver = checkExpression(*expression.left, variables, false, safe);
        if (safe) receiver = nullableBase(receiver);
        if (listType(receiver)) {
            const auto& name = expression.text;
            if (stringListDepth(receiver) > 1 && (name == "sort" || name == "remove")) {
                throw Diagnostic(expression.location, "ML3043", "Sort and remove require a flat str[] list.");
            }
            const bool insert = name == "insert";
            const bool takesValue = insert || name == "append" || name == "remove";
            const bool pop = name == "pop";
            if (!takesValue && !pop && name != "sort") {
                throw Diagnostic(expression.location, "ML3043", "Unknown list method.");
            }
            if (expression.arguments.size() != (insert ? 2u : takesValue ? 1u : 0u)) {
                throw Diagnostic(expression.location, "ML3011", "Wrong number of list method arguments.");
            }
            if (insert && checkExpression(*expression.arguments.front(), variables) != Type::integer) {
                throw Diagnostic(expression.arguments.front()->location, "ML3011", "List insert requires an int index.");
            }
            if (takesValue && checkValue(*expression.arguments.back(), variables, listElement(receiver)) != listElement(receiver)) {
                throw Diagnostic(expression.arguments.back()->location, "ML3011", "Argument must match the list element type.");
            }
            if (!pop && !allowVoid) {
                throw Diagnostic(expression.location, "ML3012", "This list method does not produce a value.");
            }
            return expression.type = pop ? listElement(receiver) : Type::none;
        }
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
    case ExpressionKind::standardCall:
        if (expression.type == Type::none && !allowVoid) {
            throw Diagnostic(expression.location, "ML3012", "A void standard function has no value.");
        }
        return expression.type;
    case ExpressionKind::call: {
        if (expression.text == "type") {
            if (expression.arguments.size() != 1) {
                throw Diagnostic(expression.location, "ML3011", "Type requires exactly one argument.");
            }
            expression.operandType = checkExpression(*expression.arguments.front(), variables, false, true);
            return expression.type = Type::string;
        }
        if (expression.text == "len") {
            if (expression.arguments.size() != 1 ||
                !listType(checkExpression(*expression.arguments.front(), variables))) {
                throw Diagnostic(expression.location, "ML3011", "Len requires one list argument.");
            }
            return expression.type = Type::integer;
        }
        if (expression.text == "readText" || expression.text == "writeText" ||
            expression.text == "RText" || expression.text == "WText" || expression.text == "AText") {
            const bool read = expression.text == "readText" || expression.text == "RText";
            if (expression.arguments.size() != (read ? 1u : 2u)) {
                throw Diagnostic(expression.location, "ML3011", "Wrong number of text file arguments.");
            }
            for (const auto& arg : expression.arguments) {
                if (checkExpression(*arg, variables) != Type::string) {
                    throw Diagnostic(arg->location, "ML3011", "Text file arguments must have type str.");
                }
            }
            if (!read && !allowVoid) {
                throw Diagnostic(expression.location, "ML3012", "A void function has no value.");
            }
            if (expression.text == "RText") expression.text = "readText";
            if (expression.text == "WText") expression.text = "writeText";
            return expression.type = read ? Type::string : Type::none;
        }
        if (const auto target = namedType(expression.text); numericType(target)) {
            if (expression.arguments.size() != 1) {
                throw Diagnostic(expression.location, "ML3020", "A numeric cast requires exactly one numeric value.",
                    {expression.span, "This cast received " + std::to_string(expression.arguments.size()) + " arguments.", {}});
            }
            const auto& argument = expression.arguments.front();
            const auto actual = checkExpression(*argument, variables);
            if (!numericType(actual)) {
                throw Diagnostic(expression.location, "ML3020", "A numeric cast requires exactly one numeric value.",
                    {argument->span, "This argument has type " + typeName(actual) + ".",
                        "Pass a char, short, int, long, float, or double value."});
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
                if (structure.fields[i].type == Type::intList) checkValue(value, variables, Type::intList);
                else checkExpression(value, variables);
                if (!canInitializeField(structure.fields[i].type, value)) {
                    const auto message = "Initializer type does not match field '" + structure.fields[i].name + "'.";
                    throw Diagnostic(value.location, "ML3023", message.c_str());
                }
            }
            return expression.type = structure.type;
        }
        const auto found = lookupName(localFunctions_, exportedFunctions_, expression.text, file);
        if (!found && (expression.text == "input" || expression.text == "format")) {
            throw Diagnostic(expression.location, "ML3042", "Use input() or format() directly inside a print block.");
        }
        if (!found || expression.text == "main") {
            NameSuggestion suggestion(expression.text);
            for (const auto& [name, declaration] : localFunctions_[file]) {
                if (name != "main") suggestion.consider(name);
            }
            for (const auto& [name, declaration] : exportedFunctions_) {
                if (name != "main" && reachable_[file].contains(declaration->location.file)) suggestion.consider(name);
            }
            throw Diagnostic(expression.location, "ML3010", "Unknown callable function.",
                {tokenSpan(expression.location, expression.text), "No function named '" + expression.text + "' is visible here.",
                    suggestion.hint("Check the name, imports, and function visibility.")});
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
            std::vector<std::pair<Function*, std::vector<int>>> viable;
            for (auto* candidate : candidates) {
                if (candidate->parameters.size() != actual.size()) continue;
                std::vector<int> ranks;
                for (std::size_t i = 0; i < actual.size(); ++i) {
                    const auto target = candidate->parameters[i].type;
                    const bool listLiteral = target == Type::intList && actual[i] == Type::intSlice &&
                        expression.arguments[i]->kind == ExpressionKind::slice;
                    ranks.push_back(listLiteral ? 1 : conversionRank(target, actual[i]));
                }
                if (std::ranges::none_of(ranks, [](int value) { return value == incompatibleConversion; }))
                    viable.emplace_back(candidate, std::move(ranks));
            }
            selected = bestOverload(viable);
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
        if (left == Type::string && ((op == "+" && right == Type::string) ||
            (op == "*" && right == Type::integer))) {
            return expression.type = Type::string;
        }
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
            if ((left == Type::longInt && realType(right)) || (right == Type::longInt && realType(left))) {
                throw Diagnostic(expression.location, "ML3005", "Mixed long and real values require an explicit conversion.",
                    {tokenSpan(expression.location, op), "This operator combines " + typeName(left) + " and " + typeName(right) + ".",
                        "Use an explicit conversion, such as double(value), if its precision is suitable."});
            }
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

Type Analyzer::checkStandardCall(Expression& expression, const Variables& variables, StandardModule module, bool allowVoid) {
    std::vector<const StandardFunction*> candidates;
    for (const auto& function : standardFunctions) {
        if (function.module == module && function.name == expression.text) candidates.push_back(&function);
    }
    if (candidates.empty()) {
        throw Diagnostic(expression.location, "ML3045", "Unknown function in this standard module.");
    }
    std::vector<Type> actual;
    for (std::size_t i = 0; i < expression.arguments.size(); ++i) {
        auto& argument = *expression.arguments[i];
        const auto& first = *candidates.front();
        actual.push_back(candidates.size() == 1 && first.arity == expression.arguments.size()
            ? checkValue(argument, variables, first.parameters[i]) : checkExpression(argument, variables));
    }
    std::vector<std::pair<const StandardFunction*, std::vector<int>>> viable;
    for (const auto* function : candidates) {
        if (function->arity != actual.size()) continue;
        std::vector<int> ranks;
        for (std::size_t i = 0; i < actual.size(); ++i) ranks.push_back(conversionRank(function->parameters[i], actual[i]));
        if (std::ranges::none_of(ranks, [](int value) { return value == incompatibleConversion; })) {
            viable.emplace_back(function, std::move(ranks));
        }
    }
    const auto* selected = bestOverload(viable);
    if (!selected) {
        throw Diagnostic(expression.location, "ML3011", viable.empty()
            ? "No standard function overload accepts these argument types."
            : "Ambiguous standard function overload; use explicit conversions.");
    }
    if (selected->result == Type::none && !allowVoid) {
        throw Diagnostic(expression.location, "ML3012", "A void standard function has no value.");
    }
    expression.kind = ExpressionKind::standardCall;
    expression.functionIndex = static_cast<std::size_t>(selected - standardFunctions);
    return expression.type = selected->result;
}

Type Analyzer::checkValue(Expression& expression, const Variables& variables, Type expected) {
    if (listType(expected) && expression.kind == ExpressionKind::slice) {
        for (auto& item : expression.arguments) {
            if (checkValue(*item, variables, listElement(expected)) != listElement(expected)) {
                throw Diagnostic(item->location, "ML3018", "Literal element does not match the list type.");
            }
        }
        return expression.type = expected;
    }
    const auto actual = checkExpression(expression, variables);
    if (expected == Type::float32 && expression.kind == ExpressionKind::real) {
        const double limit = (std::numeric_limits<float>::max)();
        if (expression.real >= -limit && expression.real <= limit &&
            static_cast<double>(static_cast<float>(expression.real)) == expression.real) {
            return expression.type = expected;
        }
    }
    if (fixedIntArrayType(expected) && expression.kind == ExpressionKind::slice) {
        if (actual != Type::intSlice) {
            throw Diagnostic(expression.location, "ML3018", "A fixed int array literal requires int elements.");
        }
        if (expression.arguments.size() != fixedIntArrayLength(expected)) {
            throw Diagnostic(expression.location, "ML3032", "Fixed array initializer length does not match the declaration.",
                {expression.span, "Expected " + std::to_string(fixedIntArrayLength(expected)) + " elements, but received "
                    + std::to_string(expression.arguments.size()) + ".",
                    "Adjust the declared length or the initializer elements."});
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
        const auto expected = function.parameters[i + first].type;
        const auto actual = expected == Type::intList ? checkValue(argument, variables, expected)
                                                     : checkExpression(argument, variables);
        if (!canAssign(expected, actual)) {
            throw Diagnostic(argument.location, "ML3011", "Argument type does not match parameter type.");
        }
    }
}

const Variable& Analyzer::lookup(const Variables& variables, const std::string& name, Location location,
    bool fieldCandidates, bool atName) const {
    if (const auto variable = variables.find(name); variable != variables.end()) return variable->second;
    NameSuggestion suggestion(name);
    for (const auto& [candidate, variable] : variables) {
        if (candidate != "this") suggestion.consider(candidate);
    }
    if (fieldCandidates) {
        const auto receiver = receiverType(variables);
        for (auto structure = structures_.find(receiver); structure != structures_.end();
            structure = structures_.find(structure->second->base)) {
            for (const auto& field : structure->second->fields) {
                if (field.isPublic || structure->first == receiver) suggestion.consider(field.name);
            }
        }
    }
    const auto message = "Undeclared variable '" + name + "'.";
    throw Diagnostic(location, "ML3004", message.c_str(),
        {atName ? std::optional(tokenSpan(location, name)) : std::nullopt, {},
            suggestion.hint("Declare the variable in a visible scope or check its spelling.")});
}

std::string Analyzer::typeName(Type type) const {
    if (const auto base = nullableBase(type); base != Type::inferred) return typeName(base) + '?';
    if (structureType(type)) return structures_.at(type)->name;
    if (fixedIntArrayType(type)) return "int[" + std::to_string(fixedIntArrayLength(type)) + ']';
    if (const auto depth = stringListDepth(type)) return depth == 1 ? "str[]" : "str[][]";
    switch (type) {
    case Type::byte: return "char";
    case Type::shortInt: return "short";
    case Type::integer: return "int";
    case Type::longInt: return "long";
    case Type::float32: return "float";
    case Type::float64: return "double";
    case Type::boolean: return "bool";
    case Type::string: return "str";
    case Type::intSlice: return "int slice";
    case Type::intList: return "int[]";
    case Type::intListView: return "int list view";
    case Type::intView:
    case Type::borrowedIntView: return "int view";
    case Type::nullValue: return "null";
    case Type::none: return "void";
    default: return "unknown";
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

int Analyzer::conversionRank(Type destination, Type source) const {
    if (destination == source) return 0;
    if (!canAssign(destination, source)) return incompatibleConversion;
    if (integerBits(destination) && integerBits(source)) return 100 + integerBits(destination);
    if (realType(destination) && realType(source)) return 200 + (destination == Type::float32 ? 32 : 64);
    if (realType(destination) && integerBits(source)) return 300 + (destination == Type::float32 ? 32 : 64);
    return 400;
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
            const auto checkOutput = [&](auto& argument, bool lists) {
                const auto type = checkExpression(*argument, variables);
                if (!numericType(type) && type != Type::boolean && type != Type::string && !(lists && listType(type))) {
                    throw Diagnostic(argument->location, "ML3018", lists ? "Print requires non-null scalar values or lists."
                        : "Format requires non-null scalar values.");
                }
            };
            for (auto& argument : statement.arguments) checkOutput(argument, true);
            if (statement.format) {
                if (statement.arguments.size() != 1 || statement.arguments.front()->type != Type::string) {
                    throw Diagnostic(statement.format->location, "ML3042", "Format requires a single str print template.");
                }
                for (auto& argument : statement.format->arguments) checkOutput(argument, false);
            }
            for (auto& target : statement.inputTargets) {
                target.type = lookup(variables, target.text, target.location).type;
                if (!numericType(target.type) && target.type != Type::boolean && target.type != Type::string) {
                    throw Diagnostic(target.location, "ML3042", "Input requires a non-nullable scalar variable.");
                }
            }
            break;
        }
        case StatementKind::call:
            checkExpression(*statement.value, variables, true);
            if (statement.postMutation) {
                if (statement.value->kind != ExpressionKind::methodCall || !listType(statement.value->left->type)) {
                    throw Diagnostic(location, "ML3043", "A post-mutation block requires a list method.");
                }
                const bool exits = checkBlock(statement.body, variables, loopDepth, returnType, inferReturn);
                terminates = terminates || exits;
                forgetNarrowing(variables);
            }
            break;
        case StatementKind::fieldAssignment:
            statement.type = checkExpression(*statement.target, variables);
            if (statement.target->kind == ExpressionKind::member) {
                if (!writable(*statement.target)) {
                    throw Diagnostic(location, "ML3035", "Field assignment requires writable storage.");
                }
            } else {
                const auto& target = *statement.target;
                if (target.kind != ExpressionKind::subscript) {
                    throw Diagnostic(location, "ML3034", "Indexed assignment requires a fixed int array or list.");
                }
                if (!listType(target.left->type)) {
                    if (!fixedIntArrayType(target.left->type) || target.left->kind != ExpressionKind::variable) {
                        throw Diagnostic(location, "ML3034", "Indexed assignment requires a local fixed int array or list.");
                    }
                    checkArrayMutation(target.left->text, variables, location);
                }
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
                : lookup(variables, statement.name, location, true, false).type;
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
                        throw Diagnostic(location, "ML3019", "Specify a nullable type when initializing with null.",
                            {statement.value->span, "A null initializer does not determine the underlying type.",
                                "Declare a nullable type explicitly, for example int? value = null."});
                    }
                    statement.type = type;
                }
                variables.emplace(statement.name, Variable{statement.type, false, statement.value->borrowOwner});
            }
            if (!canAssign(statement.type, type)) {
                throw Diagnostic(location, "ML3007", "Variable and expression types must match.",
                    {statement.value->span, "'" + statement.name + "' expects " + typeName(statement.type) +
                        ", but the value has type " + typeName(type) + ".",
                        type == Type::boolean ? "Use bool if the variable is meant to hold a truth value." : ""});
            }
            const auto base = nullableBase(statement.type);
            variables.at(statement.name).nonNull = base != Type::inferred && canAssign(base, type);
            break;
        }
        case StatementKind::returnValue: {
            const auto type = statement.value && !inferReturn && returnType && listType(*returnType)
                ? checkValue(*statement.value, variables, *returnType)
                : statement.value ? checkExpression(*statement.value, variables) : Type::none;
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
            const auto actual = checkExpression(*statement.value, variables);
            if (actual != Type::boolean) {
                throw Diagnostic(location, "ML3014", "A condition must have type bool.",
                    {statement.value->span, "The condition has type " + typeName(actual) + ".",
                        numericType(actual) ? "Use an explicit comparison, such as value != 0." : ""});
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
                if (source != Type::string && !stringListDepth(source) && !intCollection(source)) {
                    throw Diagnostic(location, "ML3027", "Foreach requires str, a string list, or an integer collection.");
                }
                statement.type = stringListDepth(source) ? listElement(source)
                    : source == Type::string ? Type::string : Type::integer;
            } else if (source != Type::integer || checkExpression(*statement.bound, variables) != Type::integer) {
                throw Diagnostic(location, "ML3014", "For loop start and limit must have type int.");
            }
            auto scoped = variables;
            forgetNarrowing(scoped);
            if (reserved(statement.name) || !scoped.emplace(statement.name, Variable{statement.type}).second) {
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
    standardModules_.resize(count);
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
    for (const auto& import : program.imports) {
        const auto name = import.path.substr(4);
        const auto module = standardModule(name);
        if (module == StandardModule::none) {
            throw Diagnostic(import.location, "ML3044", "Unknown standard module.");
        }
        const auto& alias = import.alias.empty() ? name : import.alias;
        const auto file = import.location.file;
        if (reserved(alias) || localFunctions_[file].contains(alias) || localStructures_[file].contains(alias) ||
            !standardModules_[file].emplace(alias, module).second) {
            throw Diagnostic(import.location, "ML3044", "Reserved or conflicting standard module alias.");
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
