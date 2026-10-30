#pragma once

#include <string_view>

namespace orbit {

enum class LineEnding { CrLf, Cr, Lf };

// Preserve the first existing newline convention; new single-line files use LF.
inline LineEnding detectLineEnding(std::string_view text) {
    const auto position = text.find_first_of("\r\n");
    if (position == text.npos || text[position] == '\n') return LineEnding::Lf;
    return position + 1 < text.size() && text[position + 1] == '\n' ? LineEnding::CrLf : LineEnding::Cr;
}

} // namespace orbit
