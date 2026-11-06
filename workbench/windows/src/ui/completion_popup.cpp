#include "completion_popup.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <utility>

namespace orbit {
namespace {
constexpr wchar_t windowClass[] = L"Orbit.EditorCompletion";
constexpr float rowHeight = 29, listWidth = 295, heading = 28, footer = 26;
Icon completionIcon(int kind) {
    switch (kind) {
    case 2: return Icon::Method;
    case 3: case 4: return Icon::Function;
    case 7: case 8: return Icon::Class;
    case 9: return Icon::Library;
    case 17: return Icon::File;
    case 19: return Icon::Folder;
    default: return Icon::Config;
    }
}
} // namespace
CompletionPopup::~CompletionPopup() {
    if (window_) DestroyWindow(std::exchange(window_, nullptr));
    paint_.release();
}
void CompletionPopup::show(HWND owner, POINT anchor, float dpi, const Theme& theme,
                          ID2D1Factory* d2d, IDWriteFactory* dw, IDWriteFontCollection* fonts,
                          std::vector<language::Completion> items,
                          std::function<void(std::size_t)> choose, std::function<void(std::size_t)> select) {
    const auto previous = current() ? current()->label : std::string{};
    close();
    if (items.empty()) return;
    if (window_ && owner_ != owner) { DestroyWindow(std::exchange(window_, nullptr)); paint_.release(); }
    if (dpi_ != dpi) paint_.release();
    owner_ = owner; dpi_ = dpi; theme_ = theme;
    d2d_ = d2d; dw_ = dw; fonts_ = fonts;
    items_ = std::move(items); choose_ = std::move(choose); select_ = std::move(select);
    selected_ = first_ = 0;
    for (std::size_t i = 0; i < items_.size(); ++i) if (items_[i].label == previous) selected_ = static_cast<int>(i);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    width_ = std::min(690.f, (monitor.rcWork.right - monitor.rcWork.left) * 96.f / dpi - 20);
    height_ = heading + 8 * rowHeight + footer;
    const int width = static_cast<int>(std::lround(width_ * dpi / 96));
    const int height = static_cast<int>(std::lround(height_ * dpi / 96));
    int x = std::clamp(anchor.x, monitor.rcWork.left + 5, monitor.rcWork.right - width - 5);
    int y = anchor.y + height <= monitor.rcWork.bottom ? anchor.y : std::max(monitor.rcWork.top, anchor.y - height - static_cast<int>(24 * dpi / 96));
    WNDCLASSEXW descriptor{sizeof(descriptor)};
    descriptor.hInstance = GetModuleHandleW(nullptr); descriptor.lpfnWndProc = proc;
    descriptor.lpszClassName = windowClass; descriptor.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&descriptor);
    if (!window_) window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, windowClass, L"Python 代码补全",
        WS_POPUP | WS_CLIPCHILDREN, x, y, width, height, owner_, nullptr, descriptor.hInstance, this);
    else {
        RECT before{}; GetClientRect(window_, &before);
        SetWindowPos(window_, HWND_TOP, x, y, width, height, SWP_NOACTIVATE);
        if (before.right != width || before.bottom != height) paint_.resize();
    }
    if (!window_) return;
    const DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    visible_ = true; ShowWindow(window_, SW_SHOWNOACTIVATE);
    move(0);
}
void CompletionPopup::close() {
    visible_ = false;
    if (window_) ShowWindow(window_, SW_HIDE);
    items_.clear(); choose_ = {}; select_ = {};
}
const language::Completion* CompletionPopup::current() const {
    return visible_ && selected_ >= 0 && selected_ < static_cast<int>(items_.size()) ? &items_[selected_] : nullptr;
}
void CompletionPopup::resolved(const language::Completion& item) {
    if (!current() || current()->label != item.label) return;
    items_[selected_] = item;
    InvalidateRect(window_, nullptr, FALSE);
}
void CompletionPopup::move(int delta) {
    if (items_.empty()) return;
    selected_ = std::clamp(selected_ + delta, 0, static_cast<int>(items_.size()) - 1);
    const int count = static_cast<int>((height_ - heading - footer) / rowHeight);
    first_ = std::clamp(first_, std::max(0, selected_ - count + 1), selected_);
    InvalidateRect(window_, nullptr, FALSE);
    if (select_) select_(static_cast<std::size_t>(selected_));
}
void CompletionPopup::accept() { const auto callback = choose_; const auto index = selected(); if (callback) callback(index); }
bool CompletionPopup::key(UINT key) {
    if (!isOpen()) return false;
    switch (key) {
    case VK_UP: move(-1); return true;
    case VK_DOWN: move(1); return true;
    case VK_PRIOR: move(-8); return true;
    case VK_NEXT: move(8); return true;
    case VK_RETURN: case VK_TAB: accept(); return true;
    case VK_ESCAPE: close(); return true;
    default: return false;
    }
}
void CompletionPopup::paragraph(std::wstring_view text, D2D1_RECT_F bounds, unsigned colour, float size, bool mono) {
    auto& format = mono ? detailFormat_ : documentationFormat_;
    if (!format) {
        if (FAILED(dw_->CreateTextFormat(mono ? L"JetBrains Mono" : L"DM Sans", fonts_.Get(), DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &format))) return;
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
    paint_.brush->SetColor(color(colour));
    paint_.rt->DrawText(text.data(), static_cast<UINT32>(text.size()), format.Get(), bounds, paint_.brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void CompletionPopup::render() {
    auto& p = paint_;
    p.fill(rect(0, 0, width_, height_), theme_.panel);
    p.outline(rect(.5f, .5f, width_ - 1, height_ - 1), theme_.line, 4);
    p.text(L"Python · 代码补全", rect(12, 0, listWidth - 24, heading), theme_.muted, 10);
    p.line(listWidth, 0, listWidth, height_ - footer, theme_.line);
    p.line(0, height_ - footer, width_, height_ - footer, theme_.line);
    const int rows = static_cast<int>((height_ - heading - footer) / rowHeight);
    for (int i = first_; i < static_cast<int>(items_.size()) && i < first_ + rows; ++i) {
        const float y = heading + (i - first_) * rowHeight;
        if (i == selected_) p.round(rect(4, y, listWidth - 8, rowHeight), theme_.selected, 3);
        p.icon(completionIcon(items_[i].kind), 12, y + 6.5f, 16, theme_.muted);
        p.text(wide(items_[i].label), rect(37, y, listWidth - 50, rowHeight), theme_.text, 11, 400, 0, true);
    }
    if (const auto* item = current()) {
        p.text(wide(item->label), rect(listWidth + 14, 8, width_ - listWidth - 28, 25), theme_.blue, 12, 500, 0, true);
        std::string detail = item->detail;
        std::replace(detail.begin(), detail.end(), '\n', ' ');
        paragraph(wide(detail), rect(listWidth + 14, 40, width_ - listWidth - 28, 62), theme_.text, 10.5f, true);
        p.line(listWidth + 14, 111, width_ - 14, 111, theme_.line);
        paragraph(wide(item->documentation.empty() ? "选择候选项查看文档" : item->documentation),
            rect(listWidth + 14, 123, width_ - listWidth - 28, height_ - footer - 132), theme_.muted, 10.5f);
    }
    p.text(L"↑ ↓ 选择    Enter / Tab 插入    Esc 关闭", rect(12, height_ - footer, width_ - 24, footer), theme_.muted, 9);
}
LRESULT CALLBACK CompletionPopup::proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<CompletionPopup*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<CompletionPopup*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, w, l);
    switch (message) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; BeginPaint(window, &ps);
        if (!self->paint_.rt) self->paint_.init(window, self->d2d_.Get(), self->dw_.Get(), self->fonts_.Get(), self->dpi_);
        if (self->paint_.rt) { self->paint_.begin(self->theme_.bg == Theme::light().bg); self->render(); self->paint_.end(); }
        EndPaint(window, &ps); return 0;
    }
    case WM_LBUTTONUP: {
        const float y = GET_Y_LPARAM(l) * 96.f / self->dpi_;
        const auto row = static_cast<int>((y - heading) / rowHeight) + self->first_;
        if (y >= heading && y < self->height_ - footer && GET_X_LPARAM(l) * 96.f / self->dpi_ < listWidth && row >= 0 && row < static_cast<int>(self->items_.size())) {
            self->selected_ = row; self->accept();
        }
        return 0;
    }
    case WM_MOUSEWHEEL: self->move(GET_WHEEL_DELTA_WPARAM(w) > 0 ? -1 : 1); return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
} // namespace orbit
