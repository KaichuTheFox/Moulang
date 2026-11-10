#include <Windows.h>

#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

extern "C" [[noreturn]] void moulang_runtime_error(const char* message, int length);

namespace {
template <std::size_t size>
[[noreturn]] void numericError(const char (&message)[size]) {
    moulang_runtime_error(message, static_cast<int>(size - 1));
}

template<class Number>
int renderNumber(Number value, char* buffer) {
    const auto result = std::to_chars(buffer, buffer + 32, value);
    if (result.ec != std::errc{}) numericError("Moulang runtime: cannot format a real value.\r\n");
    return static_cast<int>(result.ptr - buffer);
}

std::string_view trimInput(std::string_view text) {
    const auto start = text.find_first_not_of(" \t");
    if (start == std::string_view::npos) return text.substr(text.size());
    return text.substr(start, text.find_last_not_of(" \t") - start + 1);
}

template<class Number>
Number parseNumber(const char* data, int length) {
    auto text = trimInput({data, static_cast<std::size_t>(length)});
    if (text.starts_with('+')) {
        text.remove_prefix(1);
        if (text.starts_with('-')) text = text.substr(text.size());
    }
    Number value{};
    const auto end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value)) {
        numericError("Moulang runtime: invalid or out-of-range numeric input.\r\n");
    }
    return value;
}
}

extern "C" std::int64_t moulang_input_integer(const char* data, int length, int width) {
    const auto value = parseNumber<std::int64_t>(data, length);
    if ((width == 8 && (value < 0 || value > 255)) ||
        (width == 16 && (value < -32768 || value > 32767)) ||
        (width == 32 && (value < INT32_MIN || value > INT32_MAX))) {
        numericError("Moulang runtime: numeric input is outside the variable's range.\r\n");
    }
    return value;
}

extern "C" float moulang_input_float(const char* data, int length) { return parseNumber<float>(data, length); }
extern "C" double moulang_input_double(const char* data, int length) { return parseNumber<double>(data, length); }

extern "C" int moulang_input_bool(const char* data, int length) {
    const auto value = trimInput({data, static_cast<std::size_t>(length)});
    if (value == "true") return 1;
    if (value == "false") return 0;
    numericError("Moulang runtime: bool input must be true or false.\r\n");
}

extern "C" int moulang_render_long(std::int64_t value, char* buffer) {
    return renderNumber(value, buffer);
}

extern "C" int moulang_render_float(float value, char* buffer) {
    return renderNumber(value, buffer);
}

extern "C" int moulang_render_double(double value, char* buffer) {
    return renderNumber(value, buffer);
}

extern "C" float moulang_narrow_real(double value) {
    constexpr std::uint64_t doubleInfinity = 0x7ff0000000000000;
    constexpr auto floatLimit =
        std::bit_cast<std::uint64_t>(static_cast<double>((std::numeric_limits<float>::max)()));
    const auto magnitude = std::bit_cast<std::uint64_t>(value) & 0x7fffffffffffffff;
    if (magnitude < doubleInfinity && magnitude > floatLimit) {
        numericError("Moulang runtime: real value is outside the float range.\r\n");
    }
    return static_cast<float>(value);
}

template<class UInt>
UInt realToInteger(double value) {
    constexpr int width = sizeof(UInt) * 8;
    constexpr int fractionBits = 52;
    constexpr std::uint64_t leadingBit = std::uint64_t{1} << fractionBits;
    const auto raw = std::bit_cast<std::uint64_t>(value);
    const int biasedExponent = static_cast<int>((raw >> fractionBits) & 0x7ff);
    if (biasedExponent == 0x7ff) {
        numericError("Moulang runtime: cannot convert a non-finite real to an integer.\r\n");
    }
    const int exponent = biasedExponent - 1023;
    if (exponent < -1 || exponent >= fractionBits + width) return 0;

    const auto significand = leadingBit | (raw & (leadingBit - 1));
    UInt lowBits;
    if (exponent >= fractionBits) {
        lowBits = static_cast<UInt>(significand) << (exponent - fractionBits);
    } else {
        const int shift = fractionBits - exponent;
        const auto whole = significand >> shift;
        const auto halfway = std::uint64_t{1} << (shift - 1);
        const auto fraction = significand & ((halfway << 1) - 1);
        const bool roundUp = fraction > halfway || (fraction == halfway && (whole & 1) != 0);
        lowBits = static_cast<UInt>(whole + roundUp);
    }
    return (raw >> 63) ? 0u - lowBits : lowBits;
}

extern "C" std::uint32_t moulang_real_to_int(double value) { return realToInteger<std::uint32_t>(value); }
extern "C" std::uint64_t moulang_real_to_long(double value) { return realToInteger<std::uint64_t>(value); }
