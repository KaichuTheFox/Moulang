#include "moulang/object.hpp"
#include "moulang/standard.hpp"

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>

#include <fstream>
#include <algorithm>
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
        slice_ = LLVMStructTypeInContext(context_, fields, 2, false);
        LLVMTypeRef viewFields[] = {fields[0], integer_, fields[0]};
        ownedView_ = LLVMStructTypeInContext(context_, viewFields, 3, false);
        LLVMTypeRef textFields[] = {fields[0], integer_, integer_, fields[0]};
        string_ = LLVMStructTypeInContext(context_, textFields, 4, false);
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
                declare(parameter.name, parameter.type, copy(LLVMGetParam(currentFunction_, i), parameter.type));
            }
            emitStatements(function.body);
            if (!terminated()) {
                releaseLocals(0);
                if (function.result == Type::none) LLVMBuildRetVoid(builder_);
                else LLVMBuildUnreachable(builder_);
            }
            ownedLocals_.clear();
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
                const auto value = take(*field.defaultValue, field.type);
                LLVMBuildStore(builder_, value, memberAddress(structure.type, receiver, i));
                releaseTemporaries(0);
            }
            LLVMBuildRetVoid(builder_);
        }
#ifdef MOULANG_ARC_DEBUG
        for (const auto& structure : program.structures) {
            if (rootCleanups_.contains(structure.type)) emitRootCleanup(structure);
        }
#endif
    }

private:
    using Locals = std::unordered_map<std::string, LLVMValueRef>;
    struct Owned {
        LLVMValueRef value;
        Type type;
        bool listIterator = false;
    };
    struct Loop {
        LLVMBasicBlockRef exit;
        LLVMBasicBlockRef step;
        std::size_t localDepth;
    };
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

    bool managed(Type valueType) const {
        if (valueType == Type::string || listType(valueType) || valueType == Type::intListView) return true;
        if (const auto base = nullableBase(valueType); base != Type::inferred) return managed(base);
        if (!structureType(valueType) || referenceType(valueType)) return false;
        for (const auto& member : structures_.at(valueType).declaration.fields) {
            if (managed(member.type)) return true;
        }
        return false;
    }

    void reference(LLVMValueRef value, Type valueType, bool retain) {
        if (LLVMIsNull(value)) return;
        if (valueType == Type::string || listType(valueType) || valueType == Type::intListView) {
            auto handle = value;
            const char* retainName = "moulang_list_retain";
            const char* releaseName = "moulang_list_release";
            if (valueType == Type::string) {
                handle = field(value, 3);
                retainName = "moulang_text_retain";
                releaseName = "moulang_text_release";
            } else if (valueType == Type::intList) {
                retainName = "moulang_int_list_retain";
                releaseName = "moulang_int_list_release";
            } else if (valueType == Type::intListView) {
                handle = field(value, 2);
                retainName = "moulang_int_buffer_retain";
                releaseName = "moulang_int_buffer_release";
            }
            if (LLVMIsNull(handle)) return;
            call(retain ? retainName : releaseName, type(Type::none), {handle});
            return;
        }
        // Absent nullable values have a zero payload, including every managed field.
        if (const auto base = nullableBase(valueType); base != Type::inferred && managed(base)) {
            reference(field(value, 1), base, retain);
        } else if (structureType(valueType) && !referenceType(valueType)) {
            const auto& members = structures_.at(valueType).declaration.fields;
            for (unsigned i = 0; i < members.size(); ++i) {
                if (managed(members[i].type)) reference(field(value, i), members[i].type, retain);
            }
        }
    }

    LLVMValueRef copy(LLVMValueRef value, Type valueType) {
        if (managed(valueType)) reference(value, valueType, true);
        return value;
    }

    void releaseTemporaries(std::size_t depth) {
        while (temporaries_.size() > depth) {
            const auto temporary = temporaries_.back();
            temporaries_.pop_back();
            reference(temporary.value, temporary.type, false);
        }
    }

    void releaseSlot(LLVMValueRef slot, Type valueType) {
        if (managed(valueType)) {
            reference(LLVMBuildLoad2(builder_, type(valueType), slot, ""), valueType, false);
        }
    }

    void releaseLocals(std::size_t depth) {
        for (auto i = ownedLocals_.size(); i > depth; --i) {
            const auto local = ownedLocals_[i - 1];
            if (local.listIterator) {
                const auto handle = LLVMBuildLoad2(builder_, type(local.type), local.value, "");
                call(local.type == Type::intList ? "moulang_int_list_end" : "moulang_list_end",
                    type(Type::none), {handle});
            }
            releaseSlot(local.value, local.type);
        }
    }

    LLVMTypeRef type(Type valueType) const {
        if (listType(valueType)) return LLVMPointerTypeInContext(context_, 0);
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
        case Type::string: return string_;
        case Type::intListView: return ownedView_;
        case Type::intView:
        case Type::borrowedIntView:
        case Type::intSlice: return slice_;
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
        const bool signedSource = source != Type::byte;
        if (realType(destination)) {
            return signedSource ? LLVMBuildSIToFP(builder_, value, target, "")
                : LLVMBuildUIToFP(builder_, value, target, "");
        }
        return LLVMBuildIntCast2(builder_, value, target, signedSource, "");
    }

    LLVMValueRef explicitNumericCast(const Expression& cast) {
        const auto& argument = *cast.arguments.front();
        const auto source = argument.type;
        const auto destination = cast.type;
        const auto value = expression(argument);
        if (source == Type::longInt && destination == Type::integer) {
            errorSite(cast.location);
            return call("moulang_checked_int", integer_, {value});
        }
        if (realType(source) && source != destination && destination != Type::float64) {
            errorSite(cast.location);
        }
        const auto converted = numericCast(value, source, destination);
        const int destinationWidth = integerBits(destination);
        if (destinationWidth && integerBits(source) > destinationWidth) {
            // Re-extending the converted value checks both bounds, including unsigned char.
            const auto restored = numericCast(converted, destination, source);
            const auto inRange = LLVMBuildICmp(builder_, LLVMIntEQ, value, restored, "");
            const auto valid = block("cast.valid");
            const auto invalid = block("cast.invalid");
            LLVMBuildCondBr(builder_, inRange, valid, invalid);

            LLVMPositionBuilderAtEnd(builder_, invalid);
            errorSite(cast.location);
            const auto message = stringLiteral("Moulang runtime: " + typeLabel(source) +
                " value is outside the " + typeLabel(destination) + " range.\r\n");
            call("moulang_runtime_error", type(Type::none), {field(message, 0), field(message, 1)});
            LLVMBuildUnreachable(builder_);

            LLVMPositionBuilderAtEnd(builder_, valid);
        }
        return converted;
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

    LLVMValueRef take(const Expression& expr, Type destination) {
        const auto value = convert(expr, destination);
        if (!temporaries_.empty() && temporaries_.back().value == value &&
            temporaries_.back().type == destination) {
            temporaries_.pop_back();
            return value;
        }
        return copy(value, destination);
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
        return allocate(type(valueType), name);
    }

    LLVMValueRef allocate(LLVMTypeRef valueType, const char* name) {
        // Allocate once per call even when the declaration executes in a loop.
        if (const auto first = LLVMGetFirstInstruction(entry_)) {
            LLVMPositionBuilderBefore(slotBuilder_.get(), first);
        } else {
            LLVMPositionBuilderAtEnd(slotBuilder_.get(), entry_);
        }
        return LLVMBuildAlloca(slotBuilder_.get(), valueType, name);
    }

    LLVMValueRef declare(const std::string& name, Type valueType, LLVMValueRef value) {
        const auto slot = allocate(valueType, name.c_str());
        locals_.emplace(name, slot);
        LLVMBuildStore(builder_, value, slot);
        if (managed(valueType)) ownedLocals_.push_back({slot, valueType});
        return slot;
    }

    void emitScoped(const std::vector<Statement>& statements) {
        auto saved = locals_;
        const auto depth = ownedLocals_.size();
        emitStatements(statements);
        if (!terminated()) releaseLocals(depth);
        ownedLocals_.resize(depth);
        locals_ = std::move(saved);
    }

    void emitBranch(const Statement& statement) {
        const auto condition = expression(*statement.value);
        releaseTemporaries(0);
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

    void emitManagedForeach(const Statement& statement) {
        auto saved = locals_;
        const auto depth = ownedLocals_.size();
        const auto sourceType = statement.value->type;
        const bool list = listType(sourceType);
        const bool integers = sourceType == Type::intList;
        const auto snapshot = take(*statement.value, sourceType);
        const auto sourceSlot = allocate(sourceType, "foreach.source");
        LLVMBuildStore(builder_, snapshot, sourceSlot);
        ownedLocals_.push_back({sourceSlot, sourceType, list});
        releaseTemporaries(0);

        const auto cursor = allocate(Type::integer, "foreach.cursor");
        LLVMBuildStore(builder_, LLVMConstNull(integer_), cursor);
        const auto length = list
            ? call(integers ? "moulang_int_list_len" : "moulang_list_len", integer_, {snapshot}) : field(snapshot, 1);
        const auto version = list
            ? call(integers ? "moulang_int_list_begin" : "moulang_list_begin", type(Type::longInt), {snapshot}) : nullptr;
        const auto test = block("foreach.test");
        const auto body = block("foreach.body");
        const auto exit = block("foreach.end");
        LLVMBuildBr(builder_, test);
        LLVMPositionBuilderAtEnd(builder_, test);
        const auto offset = LLVMBuildLoad2(builder_, integer_, cursor, "");
        LLVMBuildCondBr(builder_, LLVMBuildICmp(builder_, LLVMIntSLT, offset, length, ""), body, exit);

        LLVMPositionBuilderAtEnd(builder_, body);
        // Break and continue drop the current item, while the loop keeps its source.
        loops_.push_back({exit, test, ownedLocals_.size()});
        const auto item = allocate(statement.type, statement.name.c_str());
        locals_.emplace(statement.name, item);
        if (managed(statement.type)) ownedLocals_.push_back({item, statement.type});
        if (sourceType == Type::string) {
            call("moulang_text_next", type(Type::none),
                {field(snapshot, 0), length, field(snapshot, 3), cursor, item});
        } else if (sourceType == Type::intListView) {
            LLVMBuildStore(builder_, collectionGet(sourceType, snapshot, offset), item);
            LLVMBuildStore(builder_, LLVMBuildAdd(builder_, offset, LLVMConstInt(integer_, 1, false), ""), cursor);
        } else if (integers) {
            LLVMBuildStore(builder_, call("moulang_int_list_next", integer_, {snapshot, version, cursor}), item);
        } else if (statement.type == Type::string) {
            call("moulang_list_next", type(Type::none), {snapshot, version, cursor, item});
        } else {
            const auto row = listResult("moulang_list_next", {snapshot, version, cursor}, statement.type);
            LLVMBuildStore(builder_, row, item);
        }
        emitScoped(statement.body);
        loops_.pop_back();
        if (!terminated()) {
            releaseSlot(item, statement.type);
            LLVMBuildBr(builder_, test);
        }

        LLVMPositionBuilderAtEnd(builder_, exit);
        ownedLocals_.resize(depth + 1);
        releaseLocals(depth);
        ownedLocals_.resize(depth);
        locals_ = std::move(saved);
    }

    void emitLoop(const Statement& statement) {
        if (statement.kind == StatementKind::foreachLoop && managed(statement.value->type)) {
            emitManagedForeach(statement);
            return;
        }
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
        releaseTemporaries(0);
        const auto test = block("loop.test");
        const auto body = block("loop.body");
        const auto step = counted ? block("loop.step") : test;
        const auto exit = block("loop.end");
        LLVMBuildBr(builder_, test);
        LLVMPositionBuilderAtEnd(builder_, test);
        const auto condition = counted
            ? LLVMBuildICmp(builder_, LLVMIntSLT, LLVMBuildLoad2(builder_, integer_, slot, ""), limit, "")
            : expression(*statement.value);
        releaseTemporaries(0);
        LLVMBuildCondBr(builder_, condition, body, exit);
        LLVMPositionBuilderAtEnd(builder_, body);
        if (collection) {
            const auto index = LLVMBuildLoad2(builder_, integer_, slot, "");
            const auto item = collectionGet(statement.value->type, slice, index);
            LLVMBuildStore(builder_, item, itemSlot);
        }
        loops_.push_back({exit, step, ownedLocals_.size()});
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
                std::vector<LLVMValueRef> values;
                for (const auto& argument : statement.arguments) values.push_back(expression(*argument));
                if (statement.format) {
                    formattedOutput(values.front(), *statement.format);
                } else {
                    errorSite(statement.location);
                    for (std::size_t i = 0; i < values.size(); ++i) {
                        if (i) output(stringLiteral(" "));
                        const auto outputType = statement.arguments[i]->type;
                        if (listType(outputType)) {
                            call(outputType == Type::intList ? "moulang_int_list_print" : "moulang_list_print",
                                type(Type::none), {values[i]});
                        } else {
                            output(scalarText(outputType, values[i]));
                        }
                    }
                }
                call("moulang_newline", type(Type::none), {});
                for (const auto& target : statement.inputTargets) emitInput(target);
                break;
            }
            case StatementKind::declaration:
                declare(statement.name, statement.type, take(*statement.value, statement.type));
                break;
            case StatementKind::assignment: {
                const auto slot = locals_.at(statement.name);
                const auto value = take(*statement.value, statement.type);
                releaseSlot(slot, statement.type);
                LLVMBuildStore(builder_, value, slot);
                break;
            }
            case StatementKind::fieldAssignment: {
                const auto& target = *statement.target;
                if (target.kind == ExpressionKind::subscript && listType(target.left->type)) {
                    const auto list = expression(*target.left);
                    const auto index = expression(*target.right);
                    const auto value = convert(*statement.value, statement.type);
                    errorSite(target.location);
                    if (target.left->type == Type::intList) {
                        call("moulang_int_list_set", type(Type::none), {list, index, value});
                    } else {
                        const auto item = listItem(value, statement.type);
                        call("moulang_list_set", type(Type::none),
                            {list, index, field(item, 0), field(item, 1), field(item, 2), field(item, 3)});
                    }
                    break;
                }
                const auto destination = address(*statement.target);
                const auto value = take(*statement.value, statement.type);
                releaseSlot(destination, statement.type);
                LLVMBuildStore(builder_, value, destination);
                break;
            }
            case StatementKind::call:
                expression(*statement.value);
                releaseTemporaries(0);
                if (statement.postMutation) emitScoped(statement.body);
                break;
            case StatementKind::returnValue: {
                const auto value = statement.value ? take(*statement.value, statement.type) : nullptr;
                releaseTemporaries(0);
                releaseLocals(0);
                if (value) LLVMBuildRet(builder_, value);
                else LLVMBuildRetVoid(builder_);
                break;
            }
            case StatementKind::branch: emitBranch(statement); break;
            case StatementKind::whileLoop:
            case StatementKind::forLoop:
            case StatementKind::foreachLoop: emitLoop(statement); break;
            case StatementKind::breakLoop:
            case StatementKind::continueLoop: {
                const auto loop = loops_.back();
                releaseLocals(loop.localDepth);
                LLVMBuildBr(builder_, statement.kind == StatementKind::breakLoop ? loop.exit : loop.step);
                break;
            }
            }
            if (!terminated()) releaseTemporaries(0);
        }
    }

    LLVMValueRef call(const char* name, LLVMTypeRef result, std::vector<LLVMValueRef> args) {
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
        const bool ascii = std::ranges::all_of(value, [](unsigned char byte) { return byte < 0x80; });
        LLVMValueRef fields[] = {global, LLVMConstInt(integer_, value.size(), false), LLVMConstInt(integer_, ascii, false),
            LLVMConstNull(LLVMPointerTypeInContext(context_, 0))};
        return LLVMConstStructInContext(context_, fields, 4, false);
    }

    LLVMValueRef field(LLVMValueRef aggregate, unsigned index) {
        return LLVMBuildExtractValue(builder_, aggregate, index, "");
    }

    LLVMValueRef textPair(LLVMValueRef data, LLVMValueRef length) {
        const auto pair = LLVMBuildInsertValue(builder_, LLVMConstNull(string_), data, 0, "");
        const auto text = LLVMBuildInsertValue(builder_, pair, length, 1, "");
        return LLVMBuildInsertValue(builder_, text, LLVMConstInt(integer_, 1, false), 2, "");
    }

    LLVMValueRef textResult(const char* name, std::vector<LLVMValueRef> arguments) {
        const auto slot = allocate(Type::string, "text.result");
        arguments.push_back(slot);
        call(name, type(Type::none), std::move(arguments));
        return LLVMBuildLoad2(builder_, string_, slot, "");
    }

    LLVMValueRef checkedTextData(LLVMValueRef text) {
        const auto data = field(text, 0);
#ifdef MOULANG_ARC_DEBUG
        const auto owner = field(text, 3);
        if (!LLVMIsNull(owner)) call("moulang_text_validate", type(Type::none), {data, field(text, 1), owner});
#endif
        return data;
    }

    void errorSite(Location location) {
        const auto& path = sourceNames_->at(location.file);
        const auto text = stringLiteral(path + ':' + std::to_string(location.line) + ':' +
            std::to_string(location.column) + ": ");
        call("moulang_set_error_site", type(Type::none), {field(text, 0), field(text, 1)});
    }

    LLVMValueRef collectionGet(Type sourceType, LLVMValueRef collection, LLVMValueRef index) {
        if (sourceType == Type::intList) return call("moulang_int_list_get", integer_, {collection, index});
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
        const auto tagged = LLVMBuildInsertValue(builder_, LLVMGetUndef(slice_), data, 0, "");
        return LLVMBuildInsertValue(builder_, tagged,
            LLVMConstInt(integer_, fixedIntArrayLength(expr.type), false), 1, "");
    }

    LLVMValueRef textFile(const Expression& expr) {
        const auto path = expression(*expr.arguments[0]);
        const auto none = type(Type::none);
        if (expr.type == Type::none) {
            const auto text = expression(*expr.arguments[1]);
            errorSite(expr.location);
            return call(expr.text == "AText" ? "moulang_append_text" : "moulang_write_text", none,
                {checkedTextData(path), field(path, 1), checkedTextData(text), field(text, 1)});
        }
        errorSite(expr.location);
        return textResult("moulang_read_text", {checkedTextData(path), field(path, 1)});
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

#ifdef MOULANG_ARC_DEBUG
    bool hasManagedClassFields(Type owner) const {
        const auto& declaration = structures_.at(owner).declaration;
        if (declaration.base != Type::none && hasManagedClassFields(declaration.base)) return true;
        for (const auto& member : declaration.fields) {
            if (managed(member.type)) return true;
        }
        return false;
    }

    void registerRoot(Type owner, LLVMValueRef object) {
        if (!hasManagedClassFields(owner)) return;
        auto& cleanup = rootCleanups_[owner];
        if (!cleanup) {
            const auto name = "moulang.arc.root." + std::to_string(static_cast<unsigned>(owner));
            LLVMTypeRef parameter[] = {type(owner)};
            cleanup = LLVMAddFunction(module_, name.c_str(), LLVMFunctionType(type(Type::none), parameter, 1, false));
            LLVMSetLinkage(cleanup, LLVMInternalLinkage);
        }
        call("moulang_arc_register_root", type(Type::none), {object, cleanup});
    }

    void releaseClassFields(Type owner, LLVMValueRef object) {
        const auto& declaration = structures_.at(owner).declaration;
        for (unsigned i = 0; i < declaration.fields.size(); ++i) {
            const auto fieldType = declaration.fields[i].type;
            if (managed(fieldType)) releaseSlot(memberAddress(owner, object, i), fieldType);
        }
        if (declaration.base != Type::none) releaseClassFields(declaration.base, object);
    }

    void emitRootCleanup(const Structure& structure) {
        currentFunction_ = rootCleanups_.at(structure.type);
        entry_ = block("entry");
        LLVMPositionBuilderAtEnd(builder_, entry_);
        releaseClassFields(structure.type, LLVMGetParam(currentFunction_, 0));
        LLVMBuildRetVoid(builder_);
    }
#endif

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
        const auto depth = temporaries_.size();
        const auto positive = block("conditional.then");
        const auto negative = block("conditional.else");
        const auto join = block("conditional.end");
        LLVMBuildCondBr(builder_, condition, positive, negative);
        LLVMPositionBuilderAtEnd(builder_, positive);
        const auto yes = emitYes();
        releaseTemporaries(depth);
        const auto yesEnd = LLVMGetInsertBlock(builder_);
        LLVMBuildBr(builder_, join);
        LLVMPositionBuilderAtEnd(builder_, negative);
        const auto no = emitNo();
        releaseTemporaries(depth);
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

    void output(LLVMValueRef text) {
        call("moulang_output", type(Type::none), {checkedTextData(text), field(text, 1)});
    }

    LLVMValueRef scalarText(Type valueType, LLVMValueRef value) {
        if (valueType == Type::string) return value;
        if (valueType == Type::boolean) {
            const auto yes = stringLiteral("true");
            const auto no = stringLiteral("false");
            return textPair(LLVMBuildSelect(builder_, value, field(yes, 0), field(no, 0), ""),
                LLVMBuildSelect(builder_, value, field(yes, 1), field(no, 1), ""));
        }
        const auto buffer = allocate(LLVMArrayType2(LLVMInt8TypeInContext(context_), 32), "scalar.text");
        const auto render = valueType == Type::float32 ? "moulang_render_float" :
            valueType == Type::float64 ? "moulang_render_double" : "moulang_render_long";
        if (!realType(valueType)) value = numericCast(value, valueType, Type::longInt);
        const auto length = call(render, integer_, {value, buffer});
        return textPair(buffer, length);
    }

    LLVMValueRef listItem(LLVMValueRef value, Type itemType) {
        return itemType == Type::string ? value : textPair(value, LLVMConstNull(integer_));
    }

    LLVMValueRef listResult(const char* symbol, std::vector<LLVMValueRef> args, Type resultType) {
        const auto item = textResult(symbol, std::move(args));
        return resultType == Type::string ? item : field(item, 0);
    }

    LLVMValueRef listLiteral(const Expression& expr) {
        errorSite(expr.location);
        if (expr.type == Type::intList) {
            const auto list = call("moulang_int_list_new", type(expr.type),
                {LLVMConstInt(integer_, expr.arguments.size(), false)});
            for (const auto& argument : expr.arguments) {
                const auto item = expression(*argument);
                errorSite(argument->location);
                call("moulang_int_list_append", type(Type::none), {list, item});
            }
            return list;
        }
        const auto list = call("moulang_list_new", type(expr.type),
            {LLVMConstInt(integer_, expr.arguments.size(), false),
             LLVMConstInt(integer_, stringListDepth(expr.type), false)});
        for (const auto& argument : expr.arguments) {
            const auto item = listItem(expression(*argument), argument->type);
            errorSite(argument->location);
            call("moulang_list_append", type(Type::none),
                {list, field(item, 0), field(item, 1), field(item, 2), field(item, 3)});
        }
        return list;
    }

    LLVMValueRef listMethod(const Expression& expr) {
        std::vector<LLVMValueRef> args{expression(*expr.left)};
        if (expr.left->type == Type::intList) {
            for (const auto& argument : expr.arguments) args.push_back(expression(*argument));
            errorSite(expr.location);
            const auto symbol = "moulang_int_list_" + expr.text;
            return call(symbol.c_str(), type(expr.type), std::move(args));
        }
        if (expr.text == "insert") args.push_back(expression(*expr.arguments.front()));
        if (!expr.arguments.empty()) {
            const auto& argument = *expr.arguments.back();
            const auto item = listItem(expression(argument), argument.type);
            args.push_back(field(item, 0));
            args.push_back(field(item, 1));
            args.push_back(field(item, 2));
            args.push_back(field(item, 3));
        }
        errorSite(expr.location);
        if (expr.text == "pop") return listResult("moulang_list_pop", std::move(args), expr.type);
        const auto symbol = "moulang_list_" + expr.text;
        return call(symbol.c_str(), type(Type::none), std::move(args));
    }

    void formattedOutput(LLVMValueRef pattern, const Expression& format) {
        const auto count = format.arguments.size();
        auto values = allocate(LLVMArrayType2(type(Type::string), count), "format.values");
        for (std::size_t i = 0; i < count; ++i) {
            const auto& argument = *format.arguments[i];
            const auto rendered = scalarText(argument.type, expression(argument));
#ifdef MOULANG_ARC_DEBUG
            checkedTextData(rendered);
#endif
            auto index = LLVMConstInt(integer_, i, false);
            const auto slot = LLVMBuildGEP2(builder_, type(Type::string), values, &index, 1, "");
            LLVMBuildStore(builder_, rendered, slot);
        }
        errorSite(format.location);
        call("moulang_format_output", type(Type::none), {checkedTextData(pattern), field(pattern, 1), values,
            LLVMConstInt(integer_, count, false)});
    }

    void emitInput(const Expression& target) {
        errorSite(target.location);
        const auto valueType = target.type;
        const auto line = textResult("moulang_read_line", {LLVMConstInt(integer_, valueType == Type::string, false)});
        const auto data = field(line, 0);
        const auto length = field(line, 1);
        LLVMValueRef value = line;
        if (realType(valueType)) {
            value = call(valueType == Type::float32 ? "moulang_input_float" : "moulang_input_double",
                type(valueType), {data, length});
        } else if (valueType == Type::boolean) {
            value = call("moulang_input_bool", integer_, {data, length});
            value = LLVMBuildTrunc(builder_, value, type(valueType), "");
        } else if (numericType(valueType)) {
            const auto width = LLVMConstInt(integer_, LLVMGetIntTypeWidth(type(valueType)), false);
            value = call("moulang_input_integer", type(Type::longInt), {data, length, width});
            value = LLVMBuildIntCast2(builder_, value, type(valueType), true, "");
        }
        const auto slot = locals_.at(target.text);
        releaseSlot(slot, valueType);
        LLVMBuildStore(builder_, value, slot);
    }

    std::string typeLabel(Type valueType) const {
        if (const auto depth = stringListDepth(valueType)) {
            std::string name = "str";
            for (unsigned i = 0; i < depth; ++i) name += "[]";
            return name;
        }
        if (const auto base = nullableBase(valueType); base != Type::inferred) return typeLabel(base) + "?";
        if (structureType(valueType)) return structures_.at(valueType).declaration.name;
        if (fixedIntArrayType(valueType)) return "int[" + std::to_string(fixedIntArrayLength(valueType)) + "]";
        switch (valueType) {
        case Type::byte: return "char";
        case Type::shortInt: return "short";
        case Type::integer: return "int";
        case Type::longInt: return "long";
        case Type::float32: return "float";
        case Type::float64: return "double";
        case Type::boolean: return "bool";
        case Type::string: return "str";
        case Type::nullValue: return "null";
        case Type::intSlice:
        case Type::intList: return "int[]";
        case Type::intListView: return "int list view";
        case Type::intView:
        case Type::borrowedIntView: return "int view";
        default: throw std::logic_error("Unresolved type query.");
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
            return choose(present, type(expr.type), [&] { return copy(field(left, 1), expr.type); },
                [&] { return take(*expr.right, expr.type); });
        }
        if (op == "&&" || op == "||") {
            const bool conjunction = op == "&&";
            const auto leftBlock = LLVMGetInsertBlock(builder_);
            const auto rightBlock = block("logical.rhs");
            const auto merge = block("logical.end");
            LLVMBuildCondBr(builder_, left, conjunction ? rightBlock : merge, conjunction ? merge : rightBlock);
            LLVMPositionBuilderAtEnd(builder_, rightBlock);
            const auto depth = temporaries_.size();
            const auto right = expression(*expr.right);
            releaseTemporaries(depth);
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
            if (op == "+" || op == "*") {
                errorSite(expr.location);
                if (op == "+") return textResult("moulang_text_concat",
                    {field(left, 0), field(left, 1), field(left, 2), field(left, 3),
                     field(right, 0), field(right, 1), field(right, 2), field(right, 3)});
                return textResult("moulang_text_repeat", {field(left, 0), field(left, 1), field(left, 2), field(left, 3), right});
            }
            const auto equal = call("moulang_string_equal", integer_,
                {checkedTextData(left), field(left, 1), checkedTextData(right), field(right, 1)});
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
        const auto value = emitExpression(expr);
        if (managed(expr.type)) temporaries_.push_back({value, expr.type});
        return value;
    }

    LLVMValueRef emitExpression(const Expression& expr) {
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
            return copy(stored == type(expr.type) ? value : field(value, 1), expr.type);
        }
        case ExpressionKind::slice: {
            if (listType(expr.type)) return listLiteral(expr);
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
            if (expr.left->type == Type::string) {
                return textResult("moulang_text_index",
                    {field(slice, 0), field(slice, 1), field(slice, 2), field(slice, 3), index});
            }
            if (stringListDepth(expr.left->type)) {
                return listResult("moulang_list_get", {slice, index}, expr.type);
            }
            return collectionGet(expr.left->type, slice, index);
        }
        case ExpressionKind::view: {
            const auto source = asCollection(*expr.left);
            if (expr.left->type == Type::string) {
                const auto begin = expr.right ? expression(*expr.right) : LLVMConstNull(integer_);
                const auto end = expr.alternative ? expression(*expr.alternative) : LLVMConstNull(integer_);
                const auto step = expr.step ? expression(*expr.step) : LLVMConstInt(integer_, 1, false);
                const auto flags = LLVMConstInt(integer_, (expr.right ? 1 : 0) | (expr.alternative ? 2 : 0), false);
                errorSite(expr.location);
                return textResult("moulang_text_slice",
                    {field(source, 0), field(source, 1), field(source, 2), field(source, 3), begin, end, step, flags});
            }
            const auto begin = expression(*expr.right);
            const auto end = expression(*expr.alternative);
            errorSite(expr.location);
            if (expr.type == Type::intListView) {
                const auto slot = allocate(Type::intListView, "int.list.view");
                if (expr.left->type == Type::intList) {
                    call("moulang_int_list_view", type(Type::none), {source, begin, end, slot});
                } else {
                    call("moulang_int_view_slice", type(Type::none),
                        {field(source, 0), field(source, 1), field(source, 2), begin, end, slot});
                }
                return LLVMBuildLoad2(builder_, ownedView_, slot, "");
            }
            const auto pointer = LLVMPointerTypeInContext(context_, 0);
            auto data = field(source, 0);
            if (expr.left->type == Type::intSlice) data = call("moulang_slice_data", pointer, {data});
            const auto start = call("moulang_view_start", pointer, {data, field(source, 1), begin, end});
            const auto length = LLVMBuildSub(builder_, end, begin, "");
            const auto tagged = LLVMBuildInsertValue(builder_, LLVMGetUndef(type(Type::intView)), start, 0, "");
            return LLVMBuildInsertValue(builder_, tagged, length, 1, "");
        }
        case ExpressionKind::member:
            return copy(memberValue(expr.memberOwner, expression(*expr.left), expr.fieldIndex), expr.type);
        case ExpressionKind::safeMember: {
            const auto receiver = expression(*expr.left);
            const auto resultType = type(expr.type);
            return choose(field(receiver, 0), resultType, [&] {
                const auto value = memberValue(expr.memberOwner, field(receiver, 1), expr.fieldIndex);
                return copy(LLVMTypeOf(value) == resultType ? value : tagged(value, resultType), expr.type);
            }, [&] { return LLVMConstNull(resultType); });
        }
        case ExpressionKind::methodCall: {
            if (listType(expr.left->type)) return listMethod(expr);
            const auto& function = functions_.at(expr.functionIndex);
            std::vector<LLVMValueRef> args{expression(*expr.left)};
            appendArguments(expr, function.declaration, args);
            return invoke(function.symbol, args);
        }
        case ExpressionKind::standardCall: {
            const auto& function = standardFunctions[expr.functionIndex];
            std::vector<LLVMValueRef> args;
            for (std::size_t i = 0; i < function.arity; ++i) {
                const auto parameter = function.parameters[i];
                auto value = convert(*expr.arguments[i], parameter);
                if (parameter == Type::string) {
                    const auto slot = allocate(Type::string, "standard.text");
                    LLVMBuildStore(builder_, value, slot);
                    value = slot;
                }
                args.push_back(value);
            }
            errorSite(expr.location);
            if (function.result == Type::string) return textResult(function.symbol, std::move(args));
            return call(function.symbol, type(function.result), std::move(args));
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
#ifdef MOULANG_ARC_DEBUG
            registerRoot(expr.type, object);
#endif
            return object;
        }
        case ExpressionKind::call: {
            if (expr.text == "type") return stringLiteral(typeLabel(expr.operandType));
            if (expr.text == "len") {
                const auto& list = *expr.arguments.front();
                return call(list.type == Type::intList ? "moulang_int_list_len" : "moulang_list_len",
                    integer_, {expression(list)});
            }
            if (expr.text == "readText" || expr.text == "writeText" || expr.text == "AText") return textFile(expr);
            if (numericType(namedType(expr.text))) return explicitNumericCast(expr);
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
                    const auto initializer = take(*expr.arguments[i], structure.declaration.fields[i].type);
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
                [&] { return take(*expr.right, expr.type); },
                [&] { return take(*expr.alternative, expr.type); });
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
    LLVMTypeRef slice_;
    LLVMTypeRef ownedView_;
    LLVMValueRef currentFunction_ = nullptr;
    LLVMBasicBlockRef entry_ = nullptr;
    Locals locals_;
    std::vector<Owned> temporaries_;
    std::vector<Owned> ownedLocals_;
    std::vector<FunctionSymbol> functions_;
    std::unordered_map<Type, StructureSymbol> structures_;
    std::unordered_map<Type, LLVMValueRef> initializers_;
#ifdef MOULANG_ARC_DEBUG
    std::unordered_map<Type, LLVMValueRef> rootCleanups_;
#endif
    const std::vector<std::string>* sourceNames_ = nullptr;
    std::vector<Loop> loops_;
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
