#pragma once

#include "moulang/ast.hpp"

#include <array>

namespace moulang {
enum class TikiMode { off, basic, performance, deep, all };
enum class TikiRule { unusedLocal, stringAccumulation, indexedText, activeListMutation };
inline constexpr std::array<std::string_view, 4> tikiRuleIds = {"TI-B001", "TI-P001", "TI-P002", "TI-D001"};

struct TikiOptions {
    TikiMode mode = TikiMode::off;
    std::array<bool, tikiRuleIds.size()> disabled{};

    bool enabled(TikiRule rule) const;
};

std::vector<Diagnostic> inspect(const Program& program, const TikiOptions& options);
}
