#pragma once

#include "outline.h"
#include "view_state.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace orbit {

struct EditorAppearance {
    int font = 13;
    int tab = 4;
    int dpi = 96;
    bool wrap = false;
    bool light = false;
    std::string language;

    bool operator==(const EditorAppearance&) const = default;
};

} // namespace orbit

struct Document {
    std::string path;
    std::string cache;
    std::intptr_t pointer = 0;
    std::uint64_t revision = 0;
    std::uint64_t savedRevision = 0;
    bool loaded = false;
    bool dirty = false;
    bool pending = false;
    orbit::EditorViewState view;
    std::optional<orbit::EditorAppearance> appearance;
    std::uint64_t outlineRevision = std::numeric_limits<std::uint64_t>::max();
    orbit::DocumentOutline outline;
};
