#pragma once

#include <cstdint>

namespace orbit {

// Byte positions are used throughout because Scintilla edits UTF-8 documents.
struct EditorViewState {
    std::int64_t caret = 0;
    std::int64_t anchor = 0;
    int firstVisibleLine = 0;
    int horizontalOffset = 0;

    bool operator==(const EditorViewState&) const = default;
};

} // namespace orbit
