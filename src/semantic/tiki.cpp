#include "moulang/tiki.hpp"
#include "moulang/standard.hpp"

#include <algorithm>
#include <deque>
#include <tuple>
#include <unordered_map>

namespace moulang {
bool TikiOptions::enabled(TikiRule rule) const {
    const auto group = rule == TikiRule::unusedLocal ? TikiMode::basic :
        rule == TikiRule::activeListMutation ? TikiMode::deep : TikiMode::performance;
    return !disabled[static_cast<std::size_t>(rule)] &&
        (mode == TikiMode::all || mode == group);
}

namespace {
struct Binding {
    const Statement* declaration;
    std::size_t loopDepth;
    bool read = false;
    bool written = false;
};

struct Traversal {
    Binding* text;
    Binding* index;
};

struct IndexFinding {
    Traversal traversal;
    Location location;
    std::optional<SourceSpan> span;
};

bool nonAscii(std::string_view text) {
    return std::ranges::any_of(text, [](unsigned char byte) { return byte >= 0x80; });
}

bool exitsLoop(StatementKind kind) {
    return kind == StatementKind::breakLoop || kind == StatementKind::returnValue;
}

bool exitsLoop(const std::vector<Statement>& body) {
    for (const auto& statement : body) {
        if (exitsLoop(statement.kind)) return true;
        if (statement.kind == StatementKind::continueLoop) return false;
        if (statement.kind == StatementKind::branch || statement.postMutation) {
            // A continue hidden in a nested block can bypass a later unconditional exit.
            return false;
        }
    }
    return false;
}

bool repeats(const Statement& statement) {
    const auto& source = *statement.value;
    if (exitsLoop(statement.body)) return false;
    if (statement.kind == StatementKind::whileLoop) {
        return source.kind != ExpressionKind::boolean || source.text == "true";
    }
    if (statement.kind == StatementKind::forLoop) {
        return source.kind != ExpressionKind::integer || statement.bound->kind != ExpressionKind::integer ||
            statement.bound->integer - source.integer > 1;
    }
    if (source.kind == ExpressionKind::slice) return source.arguments.size() > 1;
    if (source.kind == ExpressionKind::string) {
        auto text = std::string_view(source.text);
        if (text.empty()) return false;
        takeCodePoint(text);
        return !text.empty();
    }
    if (fixedIntArrayType(source.type)) return fixedIntArrayLength(source.type) > 1;
    return true;
}

bool canEnterLoop(const Statement& statement) {
    const auto& source = *statement.value;
    if (statement.kind == StatementKind::whileLoop) return source.kind != ExpressionKind::boolean || source.text == "true";
    if (statement.kind == StatementKind::forLoop) {
        return source.kind != ExpressionKind::integer || statement.bound->kind != ExpressionKind::integer ||
            source.integer < statement.bound->integer;
    }
    if (source.kind == ExpressionKind::slice) return !source.arguments.empty();
    return source.kind != ExpressionKind::string || !source.text.empty();
}

class Inspector {
public:
    explicit Inspector(const TikiOptions& options)
        : unused_(options.enabled(TikiRule::unusedLocal)),
          accumulation_(options.enabled(TikiRule::stringAccumulation)),
          indexed_(options.enabled(TikiRule::indexedText)),
          mutations_(options.enabled(TikiRule::activeListMutation)) {}

    std::vector<Diagnostic> run(const Program& program) {
        if (!unused_ && !accumulation_ && !indexed_ && !mutations_) return {};
        for (const auto& function : program.functions) {
            scopes_.emplace_back();
            for (const auto& parameter : function.parameters) {
                auto* binding = bind(parameter.name, nullptr);
                if (mutations_ && listType(parameter.type)) setList(binding);
            }
            visitBlock(function.body, mutations_);
            if (unused_) {
                for (const auto& binding : bindings_) {
                    const auto* declaration = binding.declaration;
                    if (declaration && !binding.read && !declaration->name.starts_with('_')) {
                        warn(TikiRule::unusedLocal, declaration->location,
                            tokenSpan(declaration->location, declaration->name),
                            "Unused local '" + declaration->name + "'.",
                            "This local is never read.",
                            "Prefix an intentional unused name with '_' or review the initializer's side effects.");
                    }
                }
            }
            for (const auto& finding : indexFindings_) {
                if (!finding.traversal.text->written && !finding.traversal.index->written) {
                    warn(TikiRule::indexedText, finding.location, finding.span,
                        "Repeated indexing can rescan non-ASCII text.",
                        "Each code-point lookup can scan the same UTF-8 string again.",
                        "Use foreach when you only need successive code points.");
                }
            }
            indexFindings_.clear();
            lists_.clear();
            nextList_ = 0;
            scopes_.pop_back();
            bindings_.clear();
        }
        const auto key = [](const Diagnostic& diagnostic) {
            const auto& at = diagnostic.location;
            return std::tuple(at.file, at.line, at.column, std::string_view(diagnostic.code));
        };
        std::ranges::sort(warnings_, {}, key);
        const auto duplicates = std::ranges::unique(warnings_, {}, key);
        warnings_.erase(duplicates.begin(), duplicates.end());
        return std::move(warnings_);
    }

private:
    void warn(TikiRule rule, Location at, std::optional<SourceSpan> span,
        const std::string& message, const char* reason, const char* hint) {
        warnings_.emplace_back(at, tikiRuleIds[static_cast<std::size_t>(rule)].data(), message.c_str(),
            DiagnosticDetail{span, reason, hint});
    }

    Binding* bind(const std::string& name, const Statement* declaration) {
        auto& binding = bindings_.emplace_back(declaration, loopDepth_);
        scopes_.back().emplace(name, &binding);
        return &binding;
    }

    Binding* lookup(const std::string& name) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            if (const auto match = scope->find(name); match != scope->end()) return match->second;
        }
        return nullptr;
    }

    Binding* variable(const Expression& expression) const {
        return expression.kind == ExpressionKind::variable ? lookup(expression.text) : nullptr;
    }

    // A shared ID proves equality. Distinct IDs make no claim about aliasing.
    std::size_t listIdentity(const Expression& expression) const {
        const auto found = lists_.find(variable(expression));
        return found == lists_.end() ? 0 : found->second;
    }

    void setList(const Binding* binding, const Expression* source = nullptr) {
        const auto existing = source ? listIdentity(*source) : 0;
        lists_[binding] = existing ? existing : ++nextList_;
    }

    void forgetLoopAssignments(const std::vector<Statement>& body) {
        for (const auto& statement : body) {
            if (statement.kind == StatementKind::assignment && listType(statement.type)) {
                if (const auto binding = lists_.find(lookup(statement.name)); binding != lists_.end()) {
                    binding->second = ++nextList_;
                }
            }
            forgetLoopAssignments(statement.body);
            forgetLoopAssignments(statement.alternative);
        }
    }

    void inspectMutation(const Expression& expression) {
        if (expression.kind != ExpressionKind::methodCall || !listType(expression.left->type)) return;
        const auto& method = expression.text;
        // Sorting an already ordered list is allowed by the runtime.
        if (method != "append" && method != "insert" && method != "remove" && method != "pop") return;
        const auto receiver = listIdentity(*expression.left);
        if (!receiver || std::ranges::find(activeLists_, receiver) == activeLists_.end()) return;
        warn(TikiRule::activeListMutation, expression.location, tokenSpan(expression.location, method),
            "'" + method + "' can change a list during foreach.",
            "The receiver refers to the list being traversed. Structural changes are rejected while the iterator is active.",
            "If you need to traverse the original list, collect the changes and apply them after foreach.");
    }

    void visitExpression(const Expression* expression, bool deep, bool evaluated = true) {
        if (!expression) return;
        if (auto* binding = variable(*expression)) binding->read = true;
        if (evaluated && expression->kind == ExpressionKind::subscript && expression->left->type == Type::string &&
            !traversals_.empty()) {
            const auto* text = variable(*expression->left);
            const auto* index = variable(*expression->right);
            for (const auto& traversal : traversals_) {
                if (traversal.text == text && traversal.index == index) {
                    indexFindings_.push_back({traversal, expression->location, expression->span});
                    break;
                }
            }
        }
        if (deep && evaluated && !activeLists_.empty()) inspectMutation(*expression);
        // A standard call's receiver is a module alias. type() inspects its argument without evaluating it.
        if (expression->kind != ExpressionKind::standardCall) visitExpression(expression->left.get(), deep, evaluated);
        bool rightDeep = deep;
        bool alternateDeep = deep;
        if (deep && expression->left && expression->left->kind == ExpressionKind::boolean) {
            const bool truth = expression->left->text == "true";
            if (expression->kind == ExpressionKind::conditional) {
                rightDeep = truth;
                alternateDeep = !truth;
            } else if (expression->kind == ExpressionKind::binary) {
                rightDeep = (truth || expression->text != "&&") && (!truth || expression->text != "||");
            }
        }
        visitExpression(expression->right.get(), rightDeep, evaluated);
        visitExpression(expression->alternative.get(), alternateDeep, evaluated);
        visitExpression(expression->step.get(), deep, evaluated);
        const bool argumentEvaluated = evaluated &&
            !(expression->kind == ExpressionKind::call && expression->text == "type");
        for (const auto& argument : expression->arguments) visitExpression(argument.get(), deep, argumentEvaluated);
    }

    void assign(const Statement& statement, bool deep) {
        auto* binding = lookup(statement.name);
        if (!binding) return;
        binding->written = true;
        if (deep && listType(statement.type)) setList(binding, statement.value.get());
        if (!accumulation_ || binding->loopDepth >= loopDepth_ || statement.type != Type::string) return;
        const auto& value = *statement.value;
        if (value.kind != ExpressionKind::binary || value.text != "+") return;
        const Expression* other = nullptr;
        if (variable(*value.left) == binding) other = value.right.get();
        else if (variable(*value.right) == binding) other = value.left.get();
        if (!other || (other->kind == ExpressionKind::string && other->text.empty())) return;
        warn(TikiRule::stringAccumulation, statement.location, tokenSpan(statement.location, statement.name),
            "Repeated concatenation can copy a growing string.",
            "Each concatenation can allocate and copy the accumulated text.",
            "If only the final value is used, consider collecting pieces and joining them once.");
    }

    Binding* traversalText(const Statement& statement) const {
        if (statement.kind != StatementKind::forLoop || statement.value->kind != ExpressionKind::integer ||
            statement.value->integer != 0) return nullptr;
        const auto& limit = *statement.bound;
        if (limit.kind != ExpressionKind::standardCall) return nullptr;
        const auto& function = standardFunctions[limit.functionIndex];
        if (function.module != StandardModule::text || function.name != "len") return nullptr;
        auto* text = variable(*limit.arguments.front());
        if (!text || !text->declaration) return nullptr;
        const auto& initial = *text->declaration->value;
        if (initial.kind != ExpressionKind::string || !nonAscii(initial.text)) return nullptr;
        auto bytes = std::string_view(initial.text);
        takeCodePoint(bytes);
        return bytes.empty() ? nullptr : text;
    }

    void popScope(bool deep) {
        if (deep) {
            for (const auto& [name, binding] : scopes_.back()) lists_.erase(binding);
        }
        scopes_.pop_back();
    }

    void visitLoop(const Statement& statement, bool deep) {
        const bool repeated = (accumulation_ || indexed_) && repeats(statement);
        auto* text = indexed_ && repeated ? traversalText(statement) : nullptr;
        const bool collection = statement.kind == StatementKind::foreachLoop;
        const auto active = deep && collection && listType(statement.value->type) ? listIdentity(*statement.value) : 0;
        const bool bodyDeep = deep && canEnterLoop(statement);
        // A counted loop can reassign its index, so literal bounds alone do not bound its iterations.
        if (bodyDeep && (statement.kind == StatementKind::forLoop || repeats(statement))) {
            forgetLoopAssignments(statement.body);
        }
        // While conditions run on every iteration. Counted loop bounds and foreach sources are captured once.
        if (statement.kind == StatementKind::whileLoop) visitExpression(statement.value.get(), deep);
        if (active) activeLists_.push_back(active);
        if (repeated) ++loopDepth_;
        scopes_.emplace_back();
        if (statement.kind != StatementKind::whileLoop) {
            auto* index = bind(statement.name, nullptr);
            if (text) traversals_.push_back({text, index});
            if (bodyDeep && collection && listType(statement.type)) setList(index);
        }
        visitBlock(statement.body, bodyDeep);
        if (text) traversals_.pop_back();
        popScope(bodyDeep);
        if (repeated) --loopDepth_;
        if (active) activeLists_.pop_back();
        if (bodyDeep) forgetLoopAssignments(statement.body);
    }

    bool visitBranch(const Statement& statement, bool deep) {
        if (!deep || statement.value->kind == ExpressionKind::boolean) {
            const bool truth = statement.value->text == "true";
            const bool yes = visitBlock(statement.body, deep && truth);
            const bool no = visitBlock(statement.alternative, deep && !truth);
            return yes || no;
        }
        auto before = lists_;
        const bool yes = visitBlock(statement.body, true);
        auto positive = std::exchange(lists_, std::move(before));
        const bool no = visitBlock(statement.alternative, true);
        if (!no) lists_ = std::move(positive);
        else if (yes) {
            for (auto& [binding, identity] : lists_) {
                if (identity != positive.at(binding)) identity = ++nextList_;
            }
        }
        return yes || no;
    }

    bool visitBlock(const std::vector<Statement>& body, bool deep) {
        const bool track = deep;
        scopes_.emplace_back();
        for (const auto& statement : body) {
            if (statement.kind != StatementKind::whileLoop) visitExpression(statement.value.get(), deep);
            visitExpression(statement.bound.get(), deep);
            visitExpression(statement.target.get(), deep);
            visitExpression(statement.format.get(), deep);
            for (const auto& argument : statement.arguments) visitExpression(argument.get(), deep);
            for (const auto& target : statement.inputTargets) {
                if (auto* binding = variable(target)) binding->written = true;
            }
            switch (statement.kind) {
            case StatementKind::declaration: {
                auto* binding = bind(statement.name, &statement);
                if (deep && listType(statement.type)) setList(binding, statement.value.get());
                break;
            }
            case StatementKind::assignment: assign(statement, deep); break;
            case StatementKind::forLoop:
            case StatementKind::foreachLoop:
            case StatementKind::whileLoop: visitLoop(statement, deep); break;
            case StatementKind::branch:
                deep = visitBranch(statement, deep);
                break;
            case StatementKind::call:
                if (statement.postMutation) deep = visitBlock(statement.body, deep);
                break;
            case StatementKind::returnValue:
            case StatementKind::breakLoop:
            case StatementKind::continueLoop: deep = false; break;
            default: break;
            }
        }
        popScope(track);
        return deep;
    }

    bool unused_;
    bool accumulation_;
    bool indexed_;
    bool mutations_;
    std::size_t loopDepth_ = 0;
    std::size_t nextList_ = 0;
    std::deque<Binding> bindings_;
    std::vector<std::unordered_map<std::string, Binding*>> scopes_;
    std::unordered_map<const Binding*, std::size_t> lists_;
    std::vector<std::size_t> activeLists_;
    std::vector<Traversal> traversals_;
    std::vector<IndexFinding> indexFindings_;
    std::vector<Diagnostic> warnings_;
};
}

std::vector<Diagnostic> inspect(const Program& program, const TikiOptions& options) {
    return Inspector(options).run(program);
}
}
