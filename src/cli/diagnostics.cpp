#include "diagnostics.hpp"

#include <charconv>
#include <limits>

namespace moulang {
namespace {
std::optional<std::string_view> sourceLine(std::string_view source, std::size_t number) {
    for (std::size_t line = 1; line < number; ++line) {
        const auto end = source.find_first_of("\r\n");
        if (end == std::string_view::npos) return std::nullopt;
        const auto newline = source.substr(end).starts_with("\r\n") ? 2u : 1u;
        source.remove_prefix(end + newline);
    }
    return source.substr(0, source.find_first_of("\r\n"));
}

struct Glyph {
    std::string text;
    std::size_t cells;
};

Glyph displayGlyph(std::string_view& source, std::size_t column) {
    const auto before = source;
    const auto point = takeCodePoint(source);
    if (point == '\t') {
        const auto spaces = 4 - column % 4;
        return {std::string(spaces, ' '), spaces};
    }
    if (point >= 0x20 && point <= 0x7e) return {std::string(1, static_cast<char>(point)), 1};
    if (point >= 0xac00 && point <= 0xd7a3) {
        return {std::string(before.substr(0, before.size() - source.size())), 2};
    }
    char hex[6];
    const auto end = std::to_chars(hex, hex + sizeof(hex), static_cast<unsigned>(point), 16).ptr;
    auto escaped = "\\u{" + std::string(hex, end) + '}';
    const auto width = escaped.size();
    return {std::move(escaped), width};
}

struct Excerpt {
    std::string text;
    std::size_t caret = 0;
    std::size_t underline = 0;
    bool highlightVisible = false;
};

Excerpt excerpt(std::string_view source, SourceSpan span, std::size_t firstColumn) {
    constexpr std::size_t displayLimit = 160;
    const auto endColumn = span.end.line == span.start.line && span.end.column > span.start.column
        ? span.end.column
        : span.end.line > span.start.line ? (std::numeric_limits<std::size_t>::max)() : span.start.column + 1;
    Excerpt result{firstColumn > 1 ? "..." : ""};
    std::size_t displayWidth = result.text.size();
    std::size_t absoluteWidth = 0;
    std::size_t column = 1;
    for (; !source.empty(); ++column) {
        const auto glyph = displayGlyph(source, absoluteWidth);
        absoluteWidth += glyph.cells;
        if (column < firstColumn) continue;
        if (displayWidth + glyph.cells > displayLimit) {
            result.text += "...";
            return result;
        }
        if (column == span.start.column) {
            result.caret = displayWidth;
            result.highlightVisible = true;
        }
        if (column >= span.start.column && column < endColumn) result.underline += glyph.cells;
        result.text += glyph.text;
        displayWidth += glyph.cells;
    }
    if (span.start.column >= column) {
        result.caret = displayWidth;
        result.highlightVisible = true;
    }
    return result;
}

void renderSource(std::ostream& out, std::string_view source, SourceSpan span) {
    const auto number = std::to_string(span.start.line);
    auto display = excerpt(source, span, 1);
    if (!display.highlightVisible && span.start.column > 12) display = excerpt(source, span, span.start.column - 12);
    out << "  " << number << " | " << display.text << '\n'
        << std::string(number.size() + 2, ' ') << " | " << std::string(display.caret, ' ') << '^';
    if (display.underline > 1) out << std::string(display.underline - 1, '~');
    out << '\n';
}

void renderNote(std::ostream& out, const char* label, std::string_view text) {
    if (text.empty()) return;
    out << label;
    std::size_t column = 0;
    while (!text.empty()) {
        const auto glyph = displayGlyph(text, column);
        out << glyph.text;
        column += glyph.cells;
    }
    out << '\n';
}
}

void renderDiagnosticDetail(std::ostream& out, const Diagnostic& diagnostic,
    const std::vector<std::string>& snapshots) {
    const auto location = diagnostic.location;
    const auto span = diagnostic.detail.span.value_or(SourceSpan{location, location});
    if (span.start.file < snapshots.size()) {
        if (const auto line = sourceLine(snapshots[span.start.file], span.start.line)) renderSource(out, *line, span);
    }
    renderNote(out, "  note: ", diagnostic.detail.explanation);
    renderNote(out, "  hint: ", diagnostic.detail.hint);
}
}
