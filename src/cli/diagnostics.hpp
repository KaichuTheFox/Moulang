#pragma once

#include "moulang/source.hpp"

#include <ostream>
#include <vector>

namespace moulang {
void renderDiagnosticDetail(std::ostream& out, const Diagnostic& diagnostic,
    const std::vector<std::string>& snapshots);
}
