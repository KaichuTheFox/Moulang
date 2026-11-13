#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace moulang {
inline constexpr std::array<std::string_view, 7> sourceExtensions = {
    ".mcx", ".m", ".mxx", ".mpp", ".mcp", ".m++", ".mc+"
};

struct Location {
    std::size_t line = 1;
    std::size_t column = 1;
    std::size_t file = 0;
};

struct SourceSpan {
    Location start;
    Location end;
};

struct DiagnosticDetail {
    std::optional<SourceSpan> span;
    std::string explanation;
    std::string hint;
};

class Diagnostic : public std::runtime_error {
public:
    Diagnostic(Location location, const char* code, const char* message, DiagnosticDetail detail = {})
        : std::runtime_error(message), location(location), code(code), detail(std::move(detail)) {}

    Location location;
    const char* code;
    DiagnosticDetail detail;
};

// Consume one code point from validated UTF-8.
char32_t takeCodePoint(std::string_view& source);
SourceSpan tokenSpan(Location start, std::string_view spelling);

std::string pathText(const std::filesystem::path& path);
std::string readSource(const std::filesystem::path& path);
void validateSource(std::string& source);
}
