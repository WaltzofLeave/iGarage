#include "console_view.h"

namespace orbit {

void ConsoleView::bind(HWND window) {
    if (window_ == window) return;
    window_ = window;
    lineLimiter_.reset();
    SendMessageW(window_, SCI_SETUNDOCOLLECTION, FALSE, 0);
    SendMessageW(window_, SCI_EMPTYUNDOBUFFER, 0, 0);
}

void ConsoleView::clear() {
    lineLimiter_.reset();
    SendMessageW(window_, SCI_SETREADONLY, FALSE, 0);
    SendMessageW(window_, SCI_CLEARALL, 0, 0);
    SendMessageW(window_, SCI_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(window_, SCI_SETREADONLY, TRUE, 0);
}

void ConsoleView::append(std::string_view raw, unsigned foreground, int style) {
    auto text = lineLimiter_.consume(raw);
    if (!window_ || text.empty()) return;
    if (text.size() > displayLimit) {
        auto start = text.size() - displayLimit;
        while (start < text.size() && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80) ++start;
        text.erase(0, start);
    }
    const auto send = [&](UINT message, uptr_t w = 0, sptr_t l = 0) {
        return SendMessageW(window_, message, w, l);
    };
    auto length = send(SCI_GETLENGTH);
    const auto firstLine = send(SCI_GETFIRSTVISIBLELINE);
    const bool follow = firstLine + send(SCI_LINESONSCREEN) >= send(SCI_GETLINECOUNT) - 2;
    send(SCI_SETREADONLY, FALSE);
    if (length + static_cast<sptr_t>(text.size()) > static_cast<sptr_t>(displayLimit)) {
        const auto excess = length + static_cast<sptr_t>(text.size()) - static_cast<sptr_t>(displayLimit);
        // Trim a useful block rather than moving a large gap buffer for every log line.
        const auto remove = std::max(excess, std::min(length, static_cast<sptr_t>(displayLimit / 8)));
        auto cut = send(SCI_POSITIONFROMLINE, send(SCI_LINEFROMPOSITION, remove) + 1);
        if (cut < remove || cut > length) cut = std::min(length, send(SCI_POSITIONAFTER, remove));
        const auto removedLines = send(SCI_LINEFROMPOSITION, cut);
        send(SCI_DELETERANGE, 0, cut);
        if (!follow) send(SCI_SETFIRSTVISIBLELINE, std::max<sptr_t>(0, firstLine - removedLines));
        length = send(SCI_GETLENGTH);
    }
    send(SCI_ADDTEXT, text.size(), reinterpret_cast<sptr_t>(text.data()));
    send(SCI_STYLESETFORE, style, rgb(foreground));
    send(SCI_STARTSTYLING, length);
    send(SCI_SETSTYLING, text.size(), style);
    send(SCI_SETREADONLY, TRUE);
    // Follow the vertical viewport. Moving the caret to a huge line's end forces
    // expensive horizontal measurement even when automatic wrapping is disabled.
    if (follow) send(SCI_SCROLLTOEND);
}

} // namespace orbit
