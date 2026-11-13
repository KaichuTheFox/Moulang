#pragma once

#include "moulang/types.hpp"

#include <array>
#include <initializer_list>

namespace moulang {
enum class StandardModule { none, math, text, time, random };

inline bool standardImport(std::string_view path) {
    return path.starts_with("std:") && path.substr(4).find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == std::string_view::npos;
}

inline StandardModule standardModule(std::string_view name) {
    if (name == "math") return StandardModule::math;
    if (name == "text") return StandardModule::text;
    if (name == "time") return StandardModule::time;
    if (name == "random") return StandardModule::random;
    return StandardModule::none;
}

struct StandardFunction {
    StandardModule module;
    std::string_view name;
    const char* symbol;
    Type result;
    std::array<Type, 3> parameters{};
    std::size_t arity;

    constexpr StandardFunction(StandardModule module, std::string_view name, const char* symbol,
        Type result, std::initializer_list<Type> arguments)
        : module(module), name(name), symbol(symbol), result(result), arity(arguments.size()) {
        std::size_t index = 0;
        for (const auto argument : arguments) parameters[index++] = argument;
    }
};

inline constexpr StandardFunction standardFunctions[] = {
    {StandardModule::math, "abs", "moulang_std_abs_int", Type::integer, {Type::integer}},
    {StandardModule::math, "abs", "moulang_std_abs_long", Type::longInt, {Type::longInt}},
    {StandardModule::math, "abs", "moulang_std_abs_double", Type::float64, {Type::float64}},
    {StandardModule::math, "min", "moulang_std_min_int", Type::integer, {Type::integer, Type::integer}},
    {StandardModule::math, "min", "moulang_std_min_long", Type::longInt, {Type::longInt, Type::longInt}},
    {StandardModule::math, "min", "moulang_std_min_double", Type::float64, {Type::float64, Type::float64}},
    {StandardModule::math, "max", "moulang_std_max_int", Type::integer, {Type::integer, Type::integer}},
    {StandardModule::math, "max", "moulang_std_max_long", Type::longInt, {Type::longInt, Type::longInt}},
    {StandardModule::math, "max", "moulang_std_max_double", Type::float64, {Type::float64, Type::float64}},
    {StandardModule::math, "sqrt", "moulang_std_sqrt", Type::float64, {Type::float64}},
    {StandardModule::math, "pow", "moulang_std_pow", Type::float64, {Type::float64, Type::float64}},
    {StandardModule::text, "len", "moulang_std_text_len", Type::integer, {Type::string}},
    {StandardModule::text, "find", "moulang_std_text_find", Type::integer, {Type::string, Type::string}},
    {StandardModule::text, "substring", "moulang_std_text_substring", Type::string, {Type::string, Type::integer, Type::integer}},
    {StandardModule::text, "replace", "moulang_std_text_replace", Type::string, {Type::string, Type::string, Type::string}},
    {StandardModule::text, "split", "moulang_std_text_split", stringList(1), {Type::string, Type::string}},
    {StandardModule::text, "join", "moulang_std_text_join", Type::string, {stringList(1), Type::string}},
    {StandardModule::text, "lowerAscii", "moulang_std_text_lower_ascii", Type::string, {Type::string}},
    {StandardModule::text, "upperAscii", "moulang_std_text_upper_ascii", Type::string, {Type::string}},
    {StandardModule::time, "unixMillis", "moulang_std_unix_millis", Type::longInt, {}},
    {StandardModule::time, "monotonicNanos", "moulang_std_monotonic_nanos", Type::longInt, {}},
    {StandardModule::time, "formatUtcDate", "moulang_std_format_utc_date", Type::string, {Type::longInt}},
    {StandardModule::random, "seed", "moulang_std_random_seed", Type::none, {Type::longInt}},
    {StandardModule::random, "nextInt", "moulang_std_random_int", Type::integer, {Type::integer, Type::integer}},
    {StandardModule::random, "nextDouble", "moulang_std_random_double", Type::float64, {}},
};
}
