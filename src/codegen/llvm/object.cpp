#include "moulang/object.hpp"

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>

#include <fstream>
#include <memory>
#include <type_traits>
#include <unordered_map>

namespace moulang {
namespace {
template<class Ref, auto Dispose>
using LLVMOwner = std::unique_ptr<std::remove_pointer_t<Ref>, decltype([](Ref ref) { Dispose(ref); })>;

constexpr char triple[] = "x86_64-w64-windows-gnu";

class Emitter {
public:
    Emitter(LLVMContextRef context, LLVMModuleRef module, LLVMBuilderRef builder)
        : context_(context), module_(module), builder_(builder), integer_(LLVMInt32TypeInContext(context)),
          slotBuilder_(LLVMCreateBuilderInContext(context)) {
        LLVMTypeRef fields[] = {LLVMPointerTypeInContext(context_, 0), integer_};
        string_ = LLVMStructTypeInContext(context_, fields, 2, false);
    }

    void emit(const Program& program) {
        sourceNames_ = &program.sourceNames;
        for (const auto& structure : program.structures) {
            const auto name = "moulang.struct." + structure.name;
            structures_.emplace(structure.type, StructureSymbol{
                LLVMStructCreateNamed(context_, name.c_str()), structure
            });
        }
        for (const auto& structure : program.structures) {
            std::vector<LLVMTypeRef> fields;
            if (structure.base != Type::none) fields.push_back(layout(structure.base));
            for (const auto& field : structure.fields) fields.push_back(type(field.type));
            LLVMStructSetBody(layout(structure.type), fields.data(), static_cast<unsigned>(fields.size()), false);
            if (structure.reference) {
                LLVMTypeRef parameters[] = {type(structure.type)};
                const auto signature = LLVMFunctionType(type(Type::none), parameters, 1, false);
                const auto name = "moulang.init." + std::to_string(static_cast<std::uint32_t>(structure.type));
                const auto initializer = LLVMAddFunction(module_, name.c_str(), signature);
                LLVMSetLinkage(initializer, LLVMInternalLinkage);
                initializers_.emplace(structure.type, initializer);
            }
        }
        for (const auto& function : program.functions) {
            std::vector<LLVMTypeRef> parameters;
            for (const auto& parameter : function.parameters) parameters.push_back(type(parameter.type));
            const auto signature = LLVMFunctionType(type(function.result), parameters.data(),
                static_cast<unsigned>(parameters.size()), false);
            const bool entry = function.name == "main";
            const auto name = entry ? "moulang_main" : "moulang.fn." + std::to_string(function.location.file) +
                "." + std::to_string(static_cast<std::uint32_t>(function.owner)) +
                (function.constructor ? ".ctor." : ".") + function.name + "." + std::to_string(functions_.size());
            const auto symbol = LLVMAddFunction(module_, name.c_str(), signature);
            if (!entry) LLVMSetLinkage(symbol, LLVMInternalLinkage);
            functions_.push_back({symbol, function});
        }
        for (const auto& symbol : functions_) {
            const auto& function = symbol.declaration;
            currentFunction_ = symbol.symbol;
            locals_.clear();
            entry_ = block("entry");
            LLVMPositionBuilderAtEnd(builder_, entry_);
            for (unsigned i = 0; i < function.parameters.size(); ++i) {
                const auto& parameter = function.parameters[i];
                declare(parameter.name, parameter.type, LLVMGetParam(currentFunction_, i));
            }
            emitStatements(function.body);
            if (!terminated()) {
                if (function.result == Type::none) LLVMBuildRetVoid(builder_);
                else LLVMBuildUnreachable(builder_);
            }
        }
        for (const auto& structure : program.structures) {
            if (!structure.reference) continue;
            currentFunction_ = initializers_.at(structure.type);
            entry_ = block("entry");
            locals_.clear();
            LLVMPositionBuilderAtEnd(builder_, entry_);
            const auto receiver = LLVMGetParam(currentFunction_, 0);
            if (structure.base != Type::none) {
                std::vector<LLVMValueRef> arguments{receiver};
                invoke(initializers_.at(structure.base), arguments);
                if (structure.baseConstructor != Expression::noFunction) {
                    invoke(functions_.at(structure.baseConstructor).symbol, arguments);
                }
            }
            for (unsigned i = 0; i < structure.fields.size(); ++i) {
                const auto& field = structure.fields[i];
                if (!field.defaultValue) continue;
                const auto value = convert(*field.defaultValue, field.type);
                LLVMBuildStore(builder_, value, memberAddress(structure.type, receiver, i));
            }
            LLVMBuildRetVoid(builder_);
        }
    }

private:
    using Locals = std::unordered_map<std::string, LLVMValueRef>;
    struct FunctionSymbol {
        LLVMValueRef symbol;
        const Function& declaration;
    };
    struct StructureSymbol {
        LLVMTypeRef symbol;
        const Structure& declaration;
    };

    LLVMTypeRef layout(Type valueType) const {
        return structures_.at(valueType).symbol;
    }

    bool referenceType(Type valueType) const {
        return structureType(valueType) && structures_.at(valueType).declaration.reference;
    }

    LLVMTypeRef type(Type valueType) const {
        if (structureType(valueType)) {
            return referenceType(valueType) ? LLVMPointerTypeInContext(context_, 0) : layout(valueType);
        }
        if (fixedIntArrayType(valueType)) return LLVMArrayType2(integer_, fixedIntArrayLength(valueType));
        if (const auto base = nullableBase(valueType); base != Type::inferred) {
            LLVMTypeRef fields[] = {type(Type::boolean), type(base)};
            return LLVMStructTypeInContext(context_, fields, 2, false);
        }
        if (const int width = integerBits(valueType)) return LLVMIntTypeInContext(context_, width);
        switch (valueType) {
        case Type::float32: return LLVMFloatTypeInContext(context_);
        case Type::float64: return LLVMDoubleTypeInContext(context_);
        case Type::boolean: return LLVMInt1TypeInContext(context_);
        case Type::string:
        case Type::intView:
        case Type::borrowedIntView:
        case Type::intSlice: return string_;
        case Type::none: return LLVMVoidTypeInContext(context_);
        default: throw std::logic_error("Unresolved Moulang type.");
        }
    }

    LLVMValueRef numericCast(LLVMValueRef value, Type source, Type destination) {
        const auto target = type(destination);
        if (LLVMTypeOf(value) == target) return value;
        if (realType(source)) {
            if (realType(destination)) {
                if (destination == Type::float32) return call("moulang_narrow_real", target, {value});
                return LLVMBuildFPExt(builder_, value, target, "");
            }
            const auto widened = LLVMBuildFPCast(builder_, value, type(Type::float64), "");
              const bool wide = destination == Type::longInt;
              const auto bits = call(wide ? "moulang_real_to_long" : "moulang_real_to_int",
                  wide ? target : integer_, {widened});
            return LLVMBuildIntCast2(builder_, bits, target, false, "");
        }
        if (source == Type::longInt && destination == Type::integer)
            return call("moulang_checked_int", integer_, {value});
        const bool signedSource = source != Type::byte;
        if (realType(destination)) {
            return signedSource ? LLVMBuildSIToFP(builder_, value, target, "")
                : LLVMBuildUIToFP(builder_, value, target, "");
        }
        return LLVMBuildIntCast2(builder_, value, target, signedSource, "");
    }

    LLVMValueRef convert(const Expression& expr, Type destination) {
        const auto target = type(destination);
        if (expr.type == Type::nullValue) return LLVMConstNull(target);
        auto value = expression(expr);
        if (LLVMTypeOf(value) == target) return value;
        if (numericType(destination)) return numericCast(value, expr.type, destination);
        const auto base = nullableBase(destination);
        if (numericType(base)) value = numericCast(value, expr.type, base);
        return tagged(value, target);
    }

    LLVMValueRef tagged(LLVMValueRef value, LLVMTypeRef target) {
        const auto present = LLVMConstInt(type(Type::boolean), true, false);
        const auto header = LLVMBuildInsertValue(builder_, LLVMGetUndef(target), present, 0, "");
        return LLVMBuildInsertValue(builder_, header, value, 1, "");
    }

    LLVMBasicBlockRef block(const char* name) {
        return LLVMAppendBasicBlockInContext(context_, currentFunction_, name);
    }

    bool terminated() const {
        return LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder_)) != nullptr;
    }

    LLVMValueRef allocate(Type valueType, const char* name) {
        // Allocate once per call even when the declaration executes in a loop.
        if (const auto first = LLVMGetFirstInstruction(entry_)) {
            LLVMPositionBuilderBefore(slotBuilder_.get(), first);
        } else {
            LLVMPositionBuilderAtEnd(slotBuilder_.get(), entry_);
        }
        return LLVMBuildAlloca(slotBuilder_.get(), type(valueType), name);
    }

    LLVMValueRef declare(const std::string& name, Type valueType, LLVMValueRef value) {
        const auto slot = allocate(valueType, name.c_str());
        locals_.emplace(name, slot);
        LLVMBuildStore(builder_, value, slot);
        return slot;
    }

    void emitScoped(const std::vector<Statement>& statements) {
        auto saved = locals_;
        emitStatements(statements);
        locals_ = std::move(saved);
    }

    void emitBranch(const Statement& statement) {
        const auto condition = expression(*statement.value);
        const auto yes = block("if.then");
        const auto no = block("if.else");
        const auto join = block("if.end");
        LLVMBuildCondBr(builder_, condition, yes, no);
        LLVMPositionBuilderAtEnd(builder_, yes);
        emitScoped(statement.body);
        const bool yesEnds = terminated();
        if (!yesEnds) LLVMBuildBr(builder_, join);
        LLVMPositionBuilderAtEnd(builder_, no);
        emitScoped(statement.alternative);
        const bool noEnds = terminated();
        if (!noEnds) LLVMBuildBr(builder_, join);
        LLVMPositionBuilderAtEnd(builder_, join);
        if (yesEnds && noEnds) LLVMBuildUnreachable(builder_);
    }

    void emitLoop(const Statement& statement) {
        auto saved = locals_;
        const bool collection = statement.kind == StatementKind::foreachLoop;
        const bool counted = collection || statement.kind == StatementKind::forLoop;
        LLVMValueRef slot = nullptr;
        LLVMValueRef limit = nullptr;
        LLVMValueRef slice = nullptr;
        LLVMValueRef itemSlot = nullptr;
        if (collection) {
            slice = asCollection(*statement.value, true);
            limit = field(slice, 1);
            slot = allocate(Type::integer, "foreach.index");
            const auto zero = LLVMConstNull(integer_);
            LLVMBuildStore(builder_, zero, slot);
            itemSlot = declare(statement.name, Type::integer, zero);
        } else if (counted) {
            const auto start = expression(*statement.value);
            limit = expression(*statement.bound);
            slot = declare(statement.name, Type::integer, start);
        }
        const auto test = block("loop.test");
        const auto body = block("loop.body");
        const auto step = counted ? block("loop.step") : test;
        const auto exit = block("loop.end");
        LLVMBuildBr(builder_, test);
        LLVMPositionBuilderAtEnd(builder_, test);
        const auto condition = counted
            ? LLVMBuildICmp(builder_, LLVMIntSLT, LLVMBuildLoad2(builder_, integer_, slot, ""), limit, "")
            : expression(*statement.value);
        LLVMBuildCondBr(builder_, condition, body, exit);
        LLVMPositionBuilderAtEnd(builder_, body);
        if (collection) {
            const auto index = LLVMBuildLoad2(builder_, integer_, slot, "");
            const auto item = collectionGet(statement.value->type, slice, index);
            LLVMBuildStore(builder_, item, itemSlot);
        }
        loops_.emplace_back(exit, step);
        emitScoped(statement.body);
        loops_.pop_back();
        if (!terminated()) LLVMBuildBr(builder_, step);
        if (counted) {
            LLVMPositionBuilderAtEnd(builder_, step);
            const auto counter = LLVMBuildLoad2(builder_, integer_, slot, "");
            const auto next = LLVMBuildAdd(builder_, counter, LLVMConstInt(integer_, 1, false), "");
            LLVMBuildStore(builder_, next, slot);
            LLVMBuildBr(builder_, test);
        }
        LLVMPositionBuilderAtEnd(builder_, exit);
        locals_ = std::move(saved);
    }

    void emitStatements(const std::vector<Statement>& statements) {
        for (const auto& statement : statements) {
            if (terminated()) break;
            switch (statement.kind) {
            case StatementKind::print: {
                const auto value = expression(*statement.value);
                errorSite(statement.location);
                print(statement.value->type, value);
                break;
            }
            case StatementKind::declaration:
                declare(statement.name, statement.type, convert(*statement.value, statement.type));
                break;
            case StatementKind::assignment:
                LLVMBuildStore(builder_, convert(*statement.value, statement.type), locals_.at(statement.name));
                break;
            case StatementKind::fieldAssignment: {
                const auto destination = address(*statement.target);
                LLVMBuildStore(builder_, convert(*statement.value, statement.type), destination);
                break;
            }
            case StatementKind::call: expression(*statement.value); break;
            case StatementKind::returnValue:
                if (statement.value) LLVMBuildRet(builder_, convert(*statement.value, statement.type));
                else LLVMBuildRetVoid(builder_);
                break;
            case StatementKind::branch: emitBranch(statement); break;
            case StatementKind::whileLoop:
            case StatementKind::forLoop:
            case StatementKind::foreachLoop: emitLoop(statement); break;
            case StatementKind::breakLoop: LLVMBuildBr(builder_, loops_.back().first); break;
            case StatementKind::continueLoop: LLVMBuildBr(builder_, loops_.back().second); break;
            }
        }
    }

    LLVMValueRef call(const char* name, LLVMTypeRef result, std::initializer_list<LLVMValueRef> arguments) {
        std::vector<LLVMValueRef> args(arguments);
        std::vector<LLVMTypeRef> types;
        for (auto arg : args) types.push_back(LLVMTypeOf(arg));
        const auto count = static_cast<unsigned>(args.size());
        const auto type = LLVMFunctionType(result, types.data(), count, false);
        auto function = LLVMGetNamedFunction(module_, name);
        if (!function) function = LLVMAddFunction(module_, name, type);
        return LLVMBuildCall2(builder_, type, function, args.data(), count, "");
    }

    LLVMValueRef stringLiteral(const std::string& value) {
        // Strings carry a byte length. Embedded NUL is data, not a terminator.
        const auto bytes = LLVMConstStringInContext2(context_, value.data(), value.size(), true);
        const auto global = LLVMAddGlobal(module_, LLVMTypeOf(bytes), ".literal");
        LLVMSetInitializer(global, bytes);
        LLVMSetGlobalConstant(global, true);
        LLVMSetLinkage(global, LLVMPrivateLinkage);
        LLVMValueRef fields[] = {global, LLVMConstInt(integer_, value.size(), false)};
        return LLVMConstStructInContext(context_, fields, 2, false);
    }

    LLVMValueRef field(LLVMValueRef aggregate, unsigned index) {
        return LLVMBuildExtractValue(builder_, aggregate, index, "");
    }

    void errorSite(Location location) {
        const auto& path = sourceNames_->at(location.file);
        const auto text = stringLiteral(path + ':' + std::to_string(location.line) + ':' +
            std::to_string(location.column) + ": ");
        call("moulang_set_error_site", type(Type::none), {field(text, 0), field(text, 1)});
    }

    LLVMValueRef collectionGet(Type sourceType, LLVMValueRef collection, LLVMValueRef index) {
        if (fixedIntArrayType(sourceType)) {
            auto checked = call("moulang_checked_array_index", integer_, {index, field(collection, 1)});
            const auto element = LLVMBuildGEP2(builder_, integer_, field(collection, 0), &checked, 1, "");
            return LLVMBuildLoad2(builder_, integer_, element, "");
        }
        const auto name = sourceType == Type::intSlice ? "moulang_slice_get" : "moulang_view_get";
        return call(name, integer_, {field(collection, 0), field(collection, 1), index});
    }

    LLVMValueRef asCollection(const Expression& expr, bool snapshot = false) {
        if (!fixedIntArrayType(expr.type)) return expression(expr);
        LLVMValueRef data;
        if (!snapshot && expr.kind == ExpressionKind::variable) {
            data = address(expr);
        } else {
            data = allocate(expr.type, "array.value");
            LLVMBuildStore(builder_, expression(expr), data);
        }
        const auto tagged = LLVMBuildInsertValue(builder_, LLVMGetUndef(string_), data, 0, "");
        return LLVMBuildInsertValue(builder_, tagged,
            LLVMConstInt(integer_, fixedIntArrayLength(expr.type), false), 1, "");
    }

    LLVMValueRef textFile(const Expression& expr) {
        const auto path = expression(*expr.arguments[0]);
        const auto none = type(Type::none);
        if (expr.type == Type::none) {
            const auto text = expression(*expr.arguments[1]);
            errorSite(expr.location);
            return call("moulang_write_text", none, {field(path, 0), field(path, 1), field(text, 0), field(text, 1)});
        }
        const auto result = allocate(Type::string, "read.text");
        const auto data = LLVMBuildStructGEP2(builder_, string_, result, 0, "");
        const auto length = LLVMBuildStructGEP2(builder_, string_, result, 1, "");
        errorSite(expr.location);
        call("moulang_read_text", none, {field(path, 0), field(path, 1), data, length});
        return LLVMBuildLoad2(builder_, string_, result, "");
    }

    LLVMValueRef address(const Expression& target) {
        if (target.kind == ExpressionKind::variable) {
            const auto slot = locals_.at(target.text);
            const auto stored = LLVMGetAllocatedType(slot);
            return stored == type(target.type) ? slot : LLVMBuildStructGEP2(builder_, stored, slot, 1, "");
        }
        if (target.kind == ExpressionKind::subscript) {
            const auto base = address(*target.left);
            const auto index = expression(*target.right);
            const auto length = LLVMConstInt(integer_, fixedIntArrayLength(target.left->type), false);
            errorSite(target.location);
            const auto checked = call("moulang_checked_array_index", integer_, {index, length});
            LLVMValueRef indices[] = {LLVMConstNull(integer_), checked};
            return LLVMBuildGEP2(builder_, type(target.left->type), base, indices, 2, "");
        }
        const auto receiver = target.left->type;
        const auto pointer = referenceType(receiver) ? expression(*target.left) : address(*target.left);
        return memberAddress(target.memberOwner, pointer, target.fieldIndex);
    }

    LLVMValueRef memberAddress(Type receiver, LLVMValueRef pointer, unsigned index) {
        const auto offset = structures_.at(receiver).declaration.base != Type::none ? 1u : 0u;
        return LLVMBuildStructGEP2(builder_, layout(receiver), pointer, index + offset, "");
    }

    LLVMValueRef memberValue(Type receiver, LLVMValueRef value, unsigned index) {
        if (!referenceType(receiver)) return field(value, index);
        const auto memberType = structures_.at(receiver).declaration.fields[index].type;
        return LLVMBuildLoad2(builder_, type(memberType), memberAddress(receiver, value, index), "");
    }

    void appendArguments(const Expression& expr, const Function& function, std::vector<LLVMValueRef>& values) {
        const auto offset = values.size();
        for (std::size_t i = 0; i < expr.arguments.size(); ++i) {
            values.push_back(convert(*expr.arguments[i], function.parameters[i + offset].type));
        }
    }

    LLVMValueRef invoke(LLVMValueRef function, std::vector<LLVMValueRef>& values) {
        return LLVMBuildCall2(builder_, LLVMGlobalGetValueType(function), function, values.data(),
            static_cast<unsigned>(values.size()), "");
    }

    LLVMValueRef choose(LLVMValueRef condition, LLVMTypeRef resultType, auto emitYes, auto emitNo) {
        const auto positive = block("conditional.then");
        const auto negative = block("conditional.else");
        const auto join = block("conditional.end");
        LLVMBuildCondBr(builder_, condition, positive, negative);
        LLVMPositionBuilderAtEnd(builder_, positive);
        const auto yes = emitYes();
        const auto yesEnd = LLVMGetInsertBlock(builder_);
        LLVMBuildBr(builder_, join);
        LLVMPositionBuilderAtEnd(builder_, negative);
        const auto no = emitNo();
        const auto noEnd = LLVMGetInsertBlock(builder_);
        LLVMBuildBr(builder_, join);
        LLVMPositionBuilderAtEnd(builder_, join);
        if (LLVMGetTypeKind(resultType) == LLVMVoidTypeKind) return nullptr;
        const auto result = LLVMBuildPhi(builder_, resultType, "");
        LLVMValueRef values[] = {yes, no};
        LLVMBasicBlockRef blocks[] = {yesEnd, noEnd};
        LLVMAddIncoming(result, values, blocks, 2);
        return result;
    }

    LLVMValueRef append(LLVMValueRef slice, LLVMValueRef item) {
        const auto size = field(slice, 1);
        const auto data = call("moulang_slice_append", LLVMPointerTypeInContext(context_, 0),
            {field(slice, 0), size, item});
        const auto extended = LLVMBuildInsertValue(builder_, slice, data, 0, "");
        const auto length = LLVMBuildAdd(builder_, size, LLVMConstInt(integer_, 1, false), "");
        return LLVMBuildInsertValue(builder_, extended, length, 1, "");
    }

    void print(Type type, LLVMValueRef value) {
        const auto none = LLVMVoidTypeInContext(context_);
        if (type == Type::string) {
            call("moulang_print", none, {LLVMBuildExtractValue(builder_, value, 0, ""),
                LLVMBuildExtractValue(builder_, value, 1, "")});
        } else if (type == Type::boolean) {
            call("moulang_print_bool", none, {LLVMBuildZExt(builder_, value, integer_, "")});
        } else if (realType(type)) {
            const auto name = type == Type::float32 ? "moulang_print_float" : "moulang_print_double";
            call(name, none, {value});
        } else {
            if (type == Type::longInt) call("moulang_print_long", none, {value});
            else call("moulang_print_int", none, {numericCast(value, type, Type::integer)});
        }
    }

    LLVMValueRef binary(const Expression& expr) {
        const auto& op = expr.text;
        if (expr.left->type == Type::nullValue || expr.right->type == Type::nullValue) {
            const auto& nonLiteral = expr.left->type == Type::nullValue ? *expr.right : *expr.left;
            const auto present = field(expression(nonLiteral), 0);
            return op == "!=" ? present : LLVMBuildNot(builder_, present, "");
        }
        const auto left = expression(*expr.left);
        if (op == "??") {
            const auto present = field(left, 0);
            const auto value = field(left, 1);
            const auto leftBlock = LLVMGetInsertBlock(builder_);
            const auto fallback = block("coalesce.default");
            const auto merge = block("coalesce.end");
            LLVMBuildCondBr(builder_, present, merge, fallback);
            LLVMPositionBuilderAtEnd(builder_, fallback);
            const auto defaultValue = convert(*expr.right, expr.type);
            const auto fallbackEnd = LLVMGetInsertBlock(builder_);
            LLVMBuildBr(builder_, merge);
            LLVMPositionBuilderAtEnd(builder_, merge);
            const auto result = LLVMBuildPhi(builder_, type(expr.type), "");
            LLVMValueRef incoming[] = {value, defaultValue};
            LLVMBasicBlockRef blocks[] = {leftBlock, fallbackEnd};
            LLVMAddIncoming(result, incoming, blocks, 2);
            return result;
        }
        if (op == "&&" || op == "||") {
            const bool conjunction = op == "&&";
            const auto leftBlock = LLVMGetInsertBlock(builder_);
            const auto rightBlock = block("logical.rhs");
            const auto merge = block("logical.end");
            LLVMBuildCondBr(builder_, left, conjunction ? rightBlock : merge, conjunction ? merge : rightBlock);
            LLVMPositionBuilderAtEnd(builder_, rightBlock);
            const auto right = expression(*expr.right);
            const auto rightEnd = LLVMGetInsertBlock(builder_);
            LLVMBuildBr(builder_, merge);
            LLVMPositionBuilderAtEnd(builder_, merge);
            const auto boolean = LLVMInt1TypeInContext(context_);
            const auto value = LLVMBuildPhi(builder_, boolean, "");
            LLVMValueRef incoming[] = {LLVMConstInt(boolean, !conjunction, false), right};
            LLVMBasicBlockRef blocks[] = {leftBlock, rightEnd};
            LLVMAddIncoming(value, incoming, blocks, 2);
            return value;
        }
        const auto right = expression(*expr.right);
        if (op == "<<" || op == ">>") {
            const auto width = LLVMConstInt(integer_, integerBits(expr.type), false);
            const auto count = numericCast(right, expr.right->type, Type::longInt);
            errorSite(expr.location);
            const auto checked = call("moulang_shift_count64", integer_, {count, width});
            const auto offset = numericCast(checked, Type::integer, expr.type);
            if (op == "<<") return LLVMBuildShl(builder_, left, offset, "");
            if (expr.type == Type::byte) return LLVMBuildLShr(builder_, left, offset, "");
            return LLVMBuildAShr(builder_, left, offset, "");
        }
        if (expr.left->type == Type::string) {
            const auto equal = call("moulang_string_equal", integer_,
                {field(left, 0), field(left, 1), field(right, 0), field(right, 1)});
            const auto predicate = op == "==" ? LLVMIntNE : LLVMIntEQ;
            return LLVMBuildICmp(builder_, predicate, equal, LLVMConstNull(integer_), "");
        }
        if (expr.operandType != Type::inferred) {
            if (op == "/" && integerBits(expr.operandType)) errorSite(expr.location);
            return numericBinary(expr.operandType, op, numericCast(left, expr.left->type, expr.operandType),
                numericCast(right, expr.right->type, expr.operandType));
        }
        return LLVMBuildICmp(builder_, op == "==" ? LLVMIntEQ : LLVMIntNE, left, right, "");
    }

    LLVMValueRef numericBinary(Type operand, std::string_view op, LLVMValueRef left, LLVMValueRef right) {
        const bool addition = op == "+" || op == "++";
        const bool subtraction = op == "-" || op == "--";
        if (realType(operand)) {
            if (addition) return LLVMBuildFAdd(builder_, left, right, "");
            if (subtraction) return LLVMBuildFSub(builder_, left, right, "");
            if (op == "*") return LLVMBuildFMul(builder_, left, right, "");
            if (op == "/") return LLVMBuildFDiv(builder_, left, right, "");
            const std::pair<std::string_view, LLVMRealPredicate> comparisons[] = {
                {"==", LLVMRealOEQ}, {"!=", LLVMRealUNE}, {"<", LLVMRealOLT}, {">", LLVMRealOGT},
                {"<=", LLVMRealOLE}, {">=", LLVMRealOGE}
            };
            for (const auto& [symbol, predicate] : comparisons) {
                if (op == symbol) return LLVMBuildFCmp(builder_, predicate, left, right, "");
            }
            throw std::logic_error("Unknown real operator.");
        }
        if (addition) return LLVMBuildAdd(builder_, left, right, "");
        if (subtraction) return LLVMBuildSub(builder_, left, right, "");
        if (op == "&") return LLVMBuildAnd(builder_, left, right, "");
        if (op == "|") return LLVMBuildOr(builder_, left, right, "");
        if (op == "^") return LLVMBuildXor(builder_, left, right, "");
        if (op == "*") return LLVMBuildMul(builder_, left, right, "");
        if (op == "/") {
            if (operand == Type::longInt) return call("moulang_divide_long", type(operand), {left, right});
            const auto quotient = call("moulang_divide", integer_,
                {numericCast(left, operand, Type::integer), numericCast(right, operand, Type::integer)});
            return numericCast(quotient, Type::integer, operand);
        }
        const bool unsignedByte = operand == Type::byte;
        const std::pair<std::string_view, LLVMIntPredicate> comparisons[] = {
            {"==", LLVMIntEQ}, {"!=", LLVMIntNE},
            {"<", unsignedByte ? LLVMIntULT : LLVMIntSLT}, {">", unsignedByte ? LLVMIntUGT : LLVMIntSGT},
            {"<=", unsignedByte ? LLVMIntULE : LLVMIntSLE}, {">=", unsignedByte ? LLVMIntUGE : LLVMIntSGE}
        };
        for (const auto& [symbol, predicate] : comparisons) {
            if (op == symbol) return LLVMBuildICmp(builder_, predicate, left, right, "");
        }
        throw std::logic_error("Unknown binary operator.");
    }

    LLVMValueRef expression(const Expression& expr) {
        switch (expr.kind) {
        case ExpressionKind::integer:
            if (realType(expr.type)) return LLVMConstReal(type(expr.type), static_cast<double>(expr.integer));
            return LLVMConstInt(type(expr.type), static_cast<std::uint64_t>(expr.integer), false);
        case ExpressionKind::real: return LLVMConstReal(type(expr.type), expr.real);
        case ExpressionKind::boolean:
            return LLVMConstInt(LLVMInt1TypeInContext(context_), expr.text == "true", false);
        case ExpressionKind::string: return stringLiteral(expr.text);
        case ExpressionKind::variable: {
            const auto slot = locals_.at(expr.text);
            const auto stored = LLVMGetAllocatedType(slot);
            const auto value = LLVMBuildLoad2(builder_, stored, slot, "");
            return stored == type(expr.type) ? value : field(value, 1);
        }
        case ExpressionKind::slice: {
            if (fixedIntArrayType(expr.type)) {
                auto value = LLVMGetUndef(type(expr.type));
                for (unsigned i = 0; i < expr.arguments.size(); ++i) {
                    const auto item = expression(*expr.arguments[i]);
                    value = LLVMBuildInsertValue(builder_, value, item, i, "");
                }
                return value;
            }
            auto value = LLVMConstNull(type(Type::intSlice));
            for (const auto& item : expr.arguments) {
                const auto element = expression(*item);
                errorSite(item->location);
                value = append(value, element);
            }
            return value;
        }
        case ExpressionKind::subscript: {
            const auto slice = asCollection(*expr.left);
            const auto index = expression(*expr.right);
            errorSite(expr.location);
            return collectionGet(expr.left->type, slice, index);
        }
        case ExpressionKind::view: {
            const auto source = asCollection(*expr.left);
            const auto begin = expression(*expr.right);
            const auto end = expression(*expr.alternative);
            errorSite(expr.location);
            const auto pointer = LLVMPointerTypeInContext(context_, 0);
            auto data = field(source, 0);
            if (expr.left->type == Type::intSlice) data = call("moulang_slice_data", pointer, {data});
            const auto start = call("moulang_view_start", pointer, {data, field(source, 1), begin, end});
            const auto length = LLVMBuildSub(builder_, end, begin, "");
            const auto tagged = LLVMBuildInsertValue(builder_, LLVMGetUndef(type(Type::intView)), start, 0, "");
            return LLVMBuildInsertValue(builder_, tagged, length, 1, "");
        }
        case ExpressionKind::member:
            return memberValue(expr.memberOwner, expression(*expr.left), expr.fieldIndex);
        case ExpressionKind::safeMember: {
            const auto receiver = expression(*expr.left);
            const auto resultType = type(expr.type);
            return choose(field(receiver, 0), resultType, [&] {
                const auto value = memberValue(expr.memberOwner, field(receiver, 1), expr.fieldIndex);
                return LLVMTypeOf(value) == resultType ? value : tagged(value, resultType);
            }, [&] { return LLVMConstNull(resultType); });
        }
        case ExpressionKind::methodCall: {
            const auto& function = functions_.at(expr.functionIndex);
            std::vector<LLVMValueRef> args{expression(*expr.left)};
            appendArguments(expr, function.declaration, args);
            return invoke(function.symbol, args);
        }
        case ExpressionKind::safeMethodCall: {
            const auto& function = functions_.at(expr.functionIndex);
            const auto receiver = expression(*expr.left);
            const auto resultType = type(expr.type);
            return choose(field(receiver, 0), resultType, [&] {
                std::vector<LLVMValueRef> args{field(receiver, 1)};
                appendArguments(expr, function.declaration, args);
                const auto value = invoke(function.symbol, args);
                return LLVMTypeOf(value) == resultType ? value : tagged(value, resultType);
            }, [&] { return expr.type == Type::none ? nullptr : LLVMConstNull(resultType); });
        }
        case ExpressionKind::constructObject: {
            std::vector<LLVMValueRef> args{nullptr};
            const auto constructor = expr.functionIndex == Expression::noFunction
                ? nullptr : &functions_.at(expr.functionIndex);
            if (constructor) appendArguments(expr, constructor->declaration, args);
            errorSite(expr.location);
            const auto object = call("moulang_allocate_object", type(expr.type), {LLVMSizeOf(layout(expr.type))});
            std::vector<LLVMValueRef> receiver{object};
            invoke(initializers_.at(expr.type), receiver);
            if (constructor) {
                args[0] = object;
                invoke(constructor->symbol, args);
            }
            return object;
        }
        case ExpressionKind::call: {
            if (expr.text == "readText" || expr.text == "writeText") return textFile(expr);
            if (numericType(namedType(expr.text))) {
                const auto& argument = *expr.arguments[0];
                const auto value = expression(argument);
                if ((realType(argument.type) && argument.type != expr.type && expr.type != Type::float64) ||
                    (argument.type == Type::longInt && expr.type == Type::integer)) {
                    errorSite(expr.location);
                }
                return numericCast(value, argument.type, expr.type);
            }
            if (expr.text == "append") {
                const auto slice = expression(*expr.arguments[0]);
                const auto item = expression(*expr.arguments[1]);
                errorSite(expr.location);
                return append(slice, item);
            }
            if (expr.functionIndex == Expression::noFunction) {
                const auto& structure = structures_.at(expr.type);
                auto value = LLVMGetUndef(structure.symbol);
                for (unsigned i = 0; i < expr.arguments.size(); ++i) {
                    const auto initializer = convert(*expr.arguments[i], structure.declaration.fields[i].type);
                    value = LLVMBuildInsertValue(builder_, value, initializer, i, "");
                }
                return value;
            }
            const auto& function = functions_.at(expr.functionIndex);
            std::vector<LLVMValueRef> args;
            appendArguments(expr, function.declaration, args);
            return invoke(function.symbol, args);
        }
        case ExpressionKind::unary: {
            const auto value = expression(*expr.left);
            if (expr.text == "!" || expr.text == "~") return LLVMBuildNot(builder_, value, "");
            return realType(expr.type) ? LLVMBuildFNeg(builder_, value, "") : LLVMBuildNeg(builder_, value, "");
        }
        case ExpressionKind::binary: return binary(expr);
        case ExpressionKind::conditional:
            return choose(expression(*expr.left), type(expr.type),
                [&] { return convert(*expr.right, expr.type); },
                [&] { return convert(*expr.alternative, expr.type); });
        case ExpressionKind::nullLiteral: break;
        }
        throw std::logic_error("Unknown expression.");
    }

    LLVMContextRef context_;
    LLVMModuleRef module_;
    LLVMBuilderRef builder_;
    LLVMTypeRef integer_;
    LLVMOwner<LLVMBuilderRef, LLVMDisposeBuilder> slotBuilder_;
    LLVMTypeRef string_;
    LLVMValueRef currentFunction_ = nullptr;
    LLVMBasicBlockRef entry_ = nullptr;
    Locals locals_;
    std::vector<FunctionSymbol> functions_;
    std::unordered_map<Type, StructureSymbol> structures_;
    std::unordered_map<Type, LLVMValueRef> initializers_;
    const std::vector<std::string>* sourceNames_ = nullptr;
    std::vector<std::pair<LLVMBasicBlockRef, LLVMBasicBlockRef>> loops_;
};
}

void emitObject(const Program& program, const std::filesystem::path& path, Optimization optimization) {
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();

    LLVMOwner<LLVMContextRef, LLVMContextDispose> context(LLVMContextCreate());
    LLVMOwner<LLVMModuleRef, LLVMDisposeModule> module(
        LLVMModuleCreateWithNameInContext("moulang", context.get()));
    LLVMSetTarget(module.get(), triple);

    LLVMTargetRef target = nullptr;
    char* message = nullptr;
    if (LLVMGetTargetFromTriple(triple, &target, &message)) {
        LLVMOwner<char*, LLVMDisposeMessage> error(message);
        throw std::runtime_error(error.get());
    }
    const auto codeLevel = optimization == Optimization::none ? LLVMCodeGenLevelNone :
        optimization == Optimization::speed ? LLVMCodeGenLevelDefault : LLVMCodeGenLevelAggressive;
    LLVMOwner<LLVMTargetMachineRef, LLVMDisposeTargetMachine> machine(
        LLVMCreateTargetMachine(target, triple, "generic", "", codeLevel,
            LLVMRelocDefault, LLVMCodeModelDefault));
    if (!machine) {
        throw std::runtime_error("Cannot create the LLVM x64 target machine.");
    }
    LLVMOwner<LLVMTargetDataRef, LLVMDisposeTargetData> layout(LLVMCreateTargetDataLayout(machine.get()));
    LLVMSetModuleDataLayout(module.get(), layout.get());

    LLVMOwner<LLVMBuilderRef, LLVMDisposeBuilder> builder(LLVMCreateBuilderInContext(context.get()));
    Emitter(context.get(), module.get(), builder.get()).emit(program);

    const bool invalid = LLVMVerifyModule(module.get(), LLVMReturnStatusAction, &message);
    {
        LLVMOwner<char*, LLVMDisposeMessage> verification(message);
        if (invalid) {
            throw std::runtime_error(verification.get());
        }
    }

    if (optimization != Optimization::none) {
        const auto pipeline = optimization == Optimization::speed ? "default<O2>,verify" : "default<O3>,verify";
        LLVMOwner<LLVMPassBuilderOptionsRef, LLVMDisposePassBuilderOptions> options(LLVMCreatePassBuilderOptions());
        if (auto error = LLVMRunPasses(module.get(), pipeline, machine.get(), options.get())) {
            LLVMOwner<char*, LLVMDisposeErrorMessage> explanation(LLVMGetErrorMessage(error));
            throw std::runtime_error(explanation.get());
        }
    }

    LLVMMemoryBufferRef buffer = nullptr;
    message = nullptr;
    if (LLVMTargetMachineEmitToMemoryBuffer(machine.get(), module.get(), LLVMObjectFile, &message, &buffer)) {
        LLVMOwner<char*, LLVMDisposeMessage> error(message);
        throw std::runtime_error(error.get());
    }
    LLVMOwner<LLVMMemoryBufferRef, LLVMDisposeMemoryBuffer> object(buffer);
    std::ofstream output(path, std::ios::binary);
    output.write(LLVMGetBufferStart(object.get()), static_cast<std::streamsize>(LLVMGetBufferSize(object.get())));
    output.close();
    if (!output) {
        throw std::runtime_error("Cannot write object file: " + pathText(path));
    }
}
}
