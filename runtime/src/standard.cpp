#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

extern "C" [[noreturn]] void moulang_runtime_error(const char* message, int length);

namespace {
template <std::size_t size>
[[noreturn]] void standardError(const char (&message)[size]) {
    moulang_runtime_error(message, static_cast<int>(size - 1));
}

template<class Integer>
Integer checkedAbs(Integer value) {
    if (value == (std::numeric_limits<Integer>::min)()) {
        standardError("Moulang runtime: math.abs result is outside the integer range.\r\n");
    }
    return value < 0 ? -value : value;
}

void requireFinite(double value) {
    if (!std::isfinite(value)) standardError("Moulang runtime: math requires finite arguments.\r\n");
}

void writeDigits(char* output, int width, unsigned value) {
    while (width > 0) {
        output[--width] = static_cast<char>('0' + value % 10);
        value /= 10;
    }
}

std::uint64_t randomState = 0;

std::uint64_t nextBits() {
    auto value = (randomState += UINT64_C(0x9e3779b97f4a7c15));
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}
}

extern "C" int moulang_std_abs_int(int value) { return checkedAbs(value); }
extern "C" std::int64_t moulang_std_abs_long(std::int64_t value) { return checkedAbs(value); }
extern "C" double moulang_std_abs_double(double value) {
    requireFinite(value);
    return std::fabs(value);
}

extern "C" int moulang_std_min_int(int left, int right) { return std::min(left, right); }
extern "C" std::int64_t moulang_std_min_long(std::int64_t left, std::int64_t right) { return std::min(left, right); }
extern "C" double moulang_std_min_double(double left, double right) {
    requireFinite(left);
    requireFinite(right);
    return std::min(left, right);
}

extern "C" int moulang_std_max_int(int left, int right) { return std::max(left, right); }
extern "C" std::int64_t moulang_std_max_long(std::int64_t left, std::int64_t right) { return std::max(left, right); }
extern "C" double moulang_std_max_double(double left, double right) {
    requireFinite(left);
    requireFinite(right);
    return std::max(left, right);
}

extern "C" double moulang_std_sqrt(double value) {
    requireFinite(value);
    if (value < 0.0) standardError("Moulang runtime: math.sqrt requires a nonnegative argument.\r\n");
    return std::sqrt(value);
}

extern "C" double moulang_std_pow(double base, double exponent) {
    requireFinite(base);
    requireFinite(exponent);
    const auto result = std::pow(base, exponent);
    if (!std::isfinite(result)) standardError("Moulang runtime: math.pow result is outside its finite domain.\r\n");
    return result;
}

extern "C" std::int64_t moulang_std_unix_millis() {
    using namespace std::chrono;
    return floor<milliseconds>(system_clock::now()).time_since_epoch().count();
}

extern "C" std::int64_t moulang_std_monotonic_nanos() {
    using namespace std::chrono;
    static const auto origin = steady_clock::now();
    return duration_cast<nanoseconds>(steady_clock::now() - origin).count();
}

extern "C" void moulang_std_write_utc_date(std::int64_t value, char* output) {
    using namespace std::chrono;
    constexpr auto first = sys_days{year{1}/1/1}.time_since_epoch().count();
    constexpr auto last = sys_days{year{9999}/12/31}.time_since_epoch().count();
    constexpr std::int64_t millisPerDay = 86400000;
    const auto dayIndex = value / millisPerDay - (value % millisPerDay < 0);
    if (dayIndex < first || dayIndex > last) {
        standardError("Moulang runtime: time.formatUtcDate requires a UTC year from 0001 through 9999.\r\n");
    }
    const year_month_day date{sys_days{days{dayIndex}}};
    writeDigits(output, 4, static_cast<unsigned>(static_cast<int>(date.year())));
    output[4] = '-';
    writeDigits(output + 5, 2, static_cast<unsigned>(date.month()));
    output[7] = '-';
    writeDigits(output + 8, 2, static_cast<unsigned>(date.day()));
}

extern "C" void moulang_std_random_seed(std::int64_t value) {
    randomState = static_cast<std::uint64_t>(value);
}

extern "C" int moulang_std_random_int(int minimum, int maximum) {
    if (minimum > maximum) standardError("Moulang runtime: random.nextInt has inverted bounds.\r\n");
    if (minimum == maximum) return minimum;
    const auto range = static_cast<std::uint64_t>(static_cast<std::int64_t>(maximum) - minimum + 1);
    const auto threshold = (std::uint64_t{0} - range) % range;
    auto bits = nextBits();
    while (bits < threshold) bits = nextBits();
    return static_cast<int>(static_cast<std::int64_t>(minimum) + static_cast<std::int64_t>(bits % range));
}

extern "C" double moulang_std_random_double() {
    return static_cast<double>(nextBits() >> 11) * 0x1.0p-53;
}
