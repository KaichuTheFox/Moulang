#include "moulang/source.hpp"

#include <Windows.h>
#include <fstream>
#include <limits>

namespace moulang {
char32_t takeCodePoint(std::string_view& source) {
    const auto first = static_cast<unsigned char>(source.front());
    source.remove_prefix(1);
    if (first < 0x80) return first;
    const int trailing = first < 0xe0 ? 1 : first < 0xf0 ? 2 : 3;
    char32_t point = first & (0x7f >> (trailing + 1));
    for (int i = 0; i < trailing; ++i) {
        point = (point << 6) | (static_cast<unsigned char>(source.front()) & 0x3f);
        source.remove_prefix(1);
    }
    return point;
}

SourceSpan tokenSpan(Location start, std::string_view spelling) {
    auto end = start;
    while (!spelling.empty()) {
        takeCodePoint(spelling);
        ++end.column;
    }
    return {start, end};
}

std::string pathText(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return {utf8.begin(), utf8.end()};
}

std::string readSource(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("Cannot open source file: " + pathText(path));
    }

    const auto size = input.tellg();
    if (size < 0 || size > (std::numeric_limits<int>::max)()) {
        throw std::runtime_error("Cannot read source file or file is too large: " + pathText(path));
    }
    std::string source(static_cast<std::size_t>(size), '\0');
    input.seekg(0);
    if (!input.read(source.data(), size)) {
        throw std::runtime_error("Cannot read source file: " + pathText(path));
    }
    validateSource(source);
    return source;
}

void validateSource(std::string& source) {
    if (source.size() > (std::numeric_limits<int>::max)()) {
        throw Diagnostic({1, 1}, "ML1000", "Source exceeds the supported size.");
    }
    if (source.starts_with("\xEF\xBB\xBF")) {
        source.erase(0, 3);
    }
    if (!source.empty() && !MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, source.data(), static_cast<int>(source.size()), nullptr, 0)) {
        throw Diagnostic({1, 1}, "ML1000", "Source must be valid UTF-8.");
    }
}
}
