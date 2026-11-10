#pragma once

#include <string_view>
#include <cstdint>

namespace moulang {
enum class Type : std::uint32_t {
    inferred, byte, shortInt, integer, longInt, float32, float64, boolean, string, intSlice, intView,
    borrowedIntView, none,
    nullValue, nullableByte, nullableShort, nullableInt, nullableLong, nullableFloat, nullableDouble,
    nullableBool, nullableString, firstStructure
};

inline constexpr std::uint32_t nullableStructureFlag = std::uint32_t{1} << 31;
inline constexpr std::uint32_t fixedIntArrayFlag = std::uint32_t{1} << 30;
inline constexpr std::uint32_t stringListFlag = std::uint32_t{1} << 29;
inline constexpr unsigned maxStringListDepth = 2;

inline bool structureType(Type type) {
    return type >= Type::firstStructure && static_cast<std::uint32_t>(type) < stringListFlag;
}

inline Type stringList(unsigned depth) {
    return static_cast<Type>(stringListFlag | depth);
}

inline unsigned stringListDepth(Type type) {
    const auto bits = static_cast<std::uint32_t>(type);
    return (bits >> 29) == 1 ? bits & (stringListFlag - 1) : 0;
}

inline Type listElement(Type list) {
    const auto depth = stringListDepth(list);
    return depth == 1 ? Type::string : stringList(depth - 1);
}

inline Type fixedIntArray(unsigned length) {
    return static_cast<Type>(fixedIntArrayFlag | length);
}

inline bool fixedIntArrayType(Type type) {
    return (static_cast<std::uint32_t>(type) >> 30) == 1;
}

inline unsigned fixedIntArrayLength(Type type) {
    return static_cast<std::uint32_t>(type) & (fixedIntArrayFlag - 1);
}

inline bool intCollection(Type type) {
    return type == Type::intSlice || type == Type::intView || type == Type::borrowedIntView || fixedIntArrayType(type);
}

inline Type namedType(std::string_view name) {
    if (name == "char") return Type::byte;
    if (name == "short") return Type::shortInt;
    if (name == "int") return Type::integer;
    if (name == "long") return Type::longInt;
    if (name == "float") return Type::float32;
    if (name == "double") return Type::float64;
    if (name == "bool") return Type::boolean;
    if (name == "str") return Type::string;
    if (name == "void") return Type::none;
    return Type::inferred;
}

inline int integerBits(Type type) {
    switch (type) {
    case Type::byte: return 8;
    case Type::shortInt: return 16;
    case Type::integer: return 32;
    case Type::longInt: return 64;
    default: return 0;
    }
}

inline bool realType(Type type) {
    return type == Type::float32 || type == Type::float64;
}

inline bool numericType(Type type) {
    return integerBits(type) != 0 || realType(type);
}

inline bool losslessNumeric(Type target, Type source) {
    if (target == source) return numericType(target);
    const int sourceBits = integerBits(source);
    if (target == Type::float64) return (sourceBits != 0 && sourceBits <= 53) || source == Type::float32;
    if (target == Type::float32) return sourceBits != 0 && sourceBits <= 24;
    return sourceBits != 0 && target != Type::byte && integerBits(target) >= sourceBits;
}

inline Type commonNumeric(Type left, Type right) {
    if (left == right) return left;
    if (left == Type::longInt && right == Type::integer) return left;
    if (losslessNumeric(right, left)) return right;
    if (losslessNumeric(left, right)) return left;
    return Type::float64;
}

inline Type nullableType(Type base) {
    if (structureType(base)) {
        return static_cast<Type>(static_cast<std::uint32_t>(base) | nullableStructureFlag);
    }
    switch (base) {
    case Type::byte: return Type::nullableByte;
    case Type::shortInt: return Type::nullableShort;
    case Type::integer: return Type::nullableInt;
    case Type::longInt: return Type::nullableLong;
    case Type::float32: return Type::nullableFloat;
    case Type::float64: return Type::nullableDouble;
    case Type::boolean: return Type::nullableBool;
    case Type::string: return Type::nullableString;
    default: return Type::inferred;
    }
}

inline Type nullableBase(Type type) {
    const auto bits = static_cast<std::uint32_t>(type);
    if (bits & nullableStructureFlag) return static_cast<Type>(bits & ~nullableStructureFlag);
    switch (type) {
    case Type::nullableByte: return Type::byte;
    case Type::nullableShort: return Type::shortInt;
    case Type::nullableInt: return Type::integer;
    case Type::nullableLong: return Type::longInt;
    case Type::nullableFloat: return Type::float32;
    case Type::nullableDouble: return Type::float64;
    case Type::nullableBool: return Type::boolean;
    case Type::nullableString: return Type::string;
    default: return Type::inferred;
    }
}
}
