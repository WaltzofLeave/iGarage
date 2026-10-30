#pragma once

#include "../core.h"
#include "output_buffer.h"

namespace orbit {

// Non-owning adapter for an existing Scintilla output control.
class ConsoleView {
public:
    static constexpr std::size_t displayLimit = 2 * 1024 * 1024;
    void bind(HWND window);
    void clear();
    void append(std::string_view text, unsigned foreground, int style = 0);

private:
    HWND window_ = nullptr;
    OutputLineLimiter lineLimiter_;
};

} // namespace orbit
