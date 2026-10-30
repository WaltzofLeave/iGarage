#include "quick_pick.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <cwctype>
#include <utility>

namespace orbit {
namespace {

constexpr wchar_t pickerClass[] = L"Orbit.QuickPick";
constexpr wchar_t overlayClass[] = L"Orbit.QuickPick.Overlay";
constexpr float rowHeight = 36;
constexpr float listTop = 99;

std::wstring folded(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return text;
}

} // namespace

QuickPick::~QuickPick() { close(); }

int QuickPick::pixel(float value) const { return static_cast<int>(std::lround(value * dpi_ / 96)); }
float QuickPick::logical(int value) const { return value * 96 / dpi_; }

void QuickPick::show(HWND owner, float dpi, const Theme& theme, ID2D1Factory* d2d,
                     IDWriteFactory* dwrite, IDWriteFontCollection* fonts,
                     std::wstring title, std::wstring placeholder,
                     std::vector<QuickPickItem> items, SelectionHandler onSelect,
                     int initialSelection) {
    close();
    owner_ = owner;
    previousFocus_ = GetFocus();
    dpi_ = dpi;
    theme_ = theme;
    d2d_ = d2d;
    dwrite_ = dwrite;
    fonts_ = fonts;
    title_ = std::move(title);
    items_ = std::move(items);
    onSelect_ = std::move(onSelect);
    selected_ = firstVisible_ = 0;

    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW descriptor{sizeof(descriptor)};
    descriptor.hInstance = instance;
    descriptor.hCursor = LoadCursor(nullptr, IDC_ARROW);
    descriptor.lpfnWndProc = windowProc;
    descriptor.lpszClassName = pickerClass;
    RegisterClassExW(&descriptor);
    descriptor.lpfnWndProc = overlayProc;
    descriptor.lpszClassName = overlayClass;
    descriptor.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassExW(&descriptor);

    RECT bounds{};
    GetWindowRect(owner_, &bounds);
    const float width = std::min(620.f, logical(bounds.right - bounds.left) - 30);
    const float height = std::min(490.f, logical(bounds.bottom - bounds.top) - 50);
    const int left = bounds.left + (bounds.right - bounds.left - pixel(width)) / 2;
    const int top = bounds.top + pixel(std::max(20.f, logical(bounds.bottom - bounds.top) * .16f));

    overlay_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        overlayClass, L"", WS_POPUP, bounds.left, bounds.top,
        bounds.right - bounds.left, bounds.bottom - bounds.top, owner_, nullptr, instance, this);
    SetLayeredWindowAttributes(overlay_, 0, 115, LWA_ALPHA);
    ShowWindow(overlay_, SW_SHOWNOACTIVATE);
    UpdateWindow(overlay_);

    window_ = CreateWindowExW(WS_EX_TOOLWINDOW, pickerClass, title_.c_str(),
        WS_POPUP | WS_CLIPCHILDREN, left, top, pixel(width), pixel(height),
        owner_, nullptr, instance, this);
    if (!window_) { close(); return; }

    const DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    inputBrush_ = CreateSolidBrush(rgb(theme_.bg));
    inputFont_ = CreateFontW(-pixel(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    input_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        pixel(45), pixel(57), pixel(width - 65), pixel(26), window_, nullptr, instance, nullptr);
    SendMessageW(input_, WM_SETFONT, reinterpret_cast<WPARAM>(inputFont_), TRUE);
    SendMessageW(input_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(placeholder.c_str()));
    SetWindowSubclass(input_, inputProc, 1, reinterpret_cast<DWORD_PTR>(this));
    filter();
    if (!filtered_.empty()) {
        selected_ = std::clamp(initialSelection, 0, static_cast<int>(filtered_.size()) - 1);
        moveSelection(0);
    }
    ShowWindow(window_, SW_SHOWNORMAL);
    SetForegroundWindow(window_);
    SetFocus(input_);
}

void QuickPick::close() {
    // Clear the handle first: DestroyWindow can synchronously deliver activation messages.
    const auto window = std::exchange(window_, nullptr);
    if (window) DestroyWindow(window);
    input_ = nullptr;
    if (overlay_) DestroyWindow(std::exchange(overlay_, nullptr));
    paint_.release();
    if (inputFont_) DeleteObject(std::exchange(inputFont_, nullptr));
    if (inputBrush_) DeleteObject(std::exchange(inputBrush_, nullptr));
    items_.clear();
    filtered_.clear();
    onSelect_ = {};
    if (window && IsWindow(owner_)) {
        SetForegroundWindow(owner_);
        if (IsWindow(previousFocus_)) SetFocus(previousFocus_);
    }
}

void QuickPick::filter() {
    const int length = GetWindowTextLengthW(input_);
    std::wstring query(static_cast<std::size_t>(length) + 1, 0);
    GetWindowTextW(input_, query.data(), length + 1);
    query.resize(length);
    query = folded(std::move(query));
    filtered_.clear();
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (query.empty() || folded(items_[i].label + L" " + items_[i].detail).find(query) != query.npos) {
            filtered_.push_back(i);
        }
    }
    selected_ = firstVisible_ = 0;
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void QuickPick::moveSelection(int offset) {
    if (filtered_.empty()) return;
    const int count = static_cast<int>(filtered_.size());
    selected_ = (selected_ + offset + count) % count;
    RECT bounds{};
    GetClientRect(window_, &bounds);
    const int visible = std::max(1, static_cast<int>((logical(bounds.bottom) - listTop - 39) / rowHeight));
    firstVisible_ = std::clamp(firstVisible_, std::max(0, selected_ - visible + 1), selected_);
    InvalidateRect(window_, nullptr, FALSE);
}

void QuickPick::choose() {
    if (filtered_.empty()) return;
    const auto selected = items_[filtered_[selected_]];
    const auto callback = onSelect_;
    close();
    if (callback) callback(selected);
}

bool QuickPick::handleKey(unsigned key) {
    switch (key) {
    case VK_ESCAPE: close(); return true;
    case VK_RETURN: choose(); return true;
    case VK_UP: moveSelection(-1); return true;
    case VK_DOWN: moveSelection(1); return true;
    case VK_PRIOR: moveSelection(-std::min(8, static_cast<int>(filtered_.size()))); return true;
    case VK_NEXT: moveSelection(std::min(8, static_cast<int>(filtered_.size()))); return true;
    default: return false;
    }
}

void QuickPick::render() {
    RECT bounds{};
    GetClientRect(window_, &bounds);
    const float width = logical(bounds.right);
    const float height = logical(bounds.bottom);
    paint_.fill(rect(0, 0, width, height), theme_.panel);
    paint_.outline(rect(.5f, .5f, width - 1, height - 1), theme_.line, 7);
    paint_.text(title_, rect(18, 9, width - 75, 29), theme_.text, 13, 500);
    paint_.text(L"Esc", rect(width - 49, 10, 32, 27), theme_.muted, 9);
    paint_.round(rect(12, 46, width - 24, 43), theme_.bg);
    paint_.icon(Icon::Search, 22, 59, 16, theme_.accent);
    paint_.line(12, 94, width - 12, 94, theme_.line);

    const int visible = std::max(1, static_cast<int>((height - listTop - 39) / rowHeight));
    for (int i = firstVisible_; i < static_cast<int>(filtered_.size()) && i < firstVisible_ + visible; ++i) {
        const auto& item = items_[filtered_[i]];
        const float indent = static_cast<float>(std::clamp(item.depth, 0, 2) * 12);
        const float y = listTop + (i - firstVisible_) * rowHeight;
        if (i == selected_) paint_.round(rect(7, y, width - 14, rowHeight), theme_.selected, 3);
        const std::wstring glyph = item.kind == 'C' ? L"C" : item.kind == 'f' ? L"ƒ" :
            item.kind == '#' ? L"#" : item.kind == 'P' ? L"Py" :
            item.kind == 'M' ? L"M↓" : item.kind == 'J' ? L"JS" : L"";
        if (glyph.empty()) paint_.icon(Icon::File, 20 + indent, y + 10, 15, theme_.muted);
        else paint_.text(glyph, rect(19 + indent, y, 24, rowHeight), theme_.blue, 10, 500, 0, true);
        paint_.text(item.label, rect(51 + indent, y, width * .52f - 51 - indent, rowHeight), theme_.text, 11);
        paint_.text(item.detail, rect(width * .52f, y, width * .48f - 20, rowHeight), theme_.muted, 9, 400, 1);
    }
    if (filtered_.empty()) paint_.text(L"没有匹配的结果", rect(20, listTop + 18, width - 40, 35), theme_.muted, 11);
    paint_.line(0, height - 34, width, height - 34, theme_.line);
    paint_.text(L"↑ ↓ 选择    Enter 打开", rect(16, height - 32, 240, 30), theme_.muted, 9);
    paint_.text(std::to_wstring(filtered_.size()) + L" 个结果", rect(width - 120, height - 32, 100, 30), theme_.muted, 9, 400, 1);
}

LRESULT CALLBACK QuickPick::windowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* picker = reinterpret_cast<QuickPick*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        picker = static_cast<QuickPick*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(picker));
    }
    return picker ? picker->handleMessage(window, message, w, l) : DefWindowProcW(window, message, w, l);
}

LRESULT QuickPick::handleMessage(HWND window, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT state{};
        BeginPaint(window, &state);
        if (!paint_.rt) paint_.init(window, d2d_.Get(), dwrite_.Get(), fonts_.Get(), dpi_);
        if (paint_.rt) { paint_.begin(); render(); paint_.end(); }
        EndPaint(window, &state);
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(w) == EN_CHANGE && reinterpret_cast<HWND>(l) == input_) filter();
        return 0;
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(w), rgb(theme_.text));
        SetBkColor(reinterpret_cast<HDC>(w), rgb(theme_.bg));
        return reinterpret_cast<LRESULT>(inputBrush_);
    case WM_KEYDOWN:
        if (handleKey(static_cast<unsigned>(w))) return 0;
        break;
    case WM_LBUTTONUP: {
        const float y = logical(GET_Y_LPARAM(l));
        RECT bounds{};
        GetClientRect(window, &bounds);
        if (y >= listTop && y < logical(bounds.bottom) - 39) {
            const int row = firstVisible_ + static_cast<int>((y - listTop) / rowHeight);
            if (row < static_cast<int>(filtered_.size())) { selected_ = row; choose(); }
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        moveSelection(GET_WHEEL_DELTA_WPARAM(w) > 0 ? -1 : 1);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE && window == window_) close();
        return 0;
    case WM_CLOSE: close(); return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

LRESULT CALLBACK QuickPick::overlayProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* picker = reinterpret_cast<QuickPick*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        picker = static_cast<QuickPick*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(picker));
    }
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_LBUTTONUP && picker) { picker->close(); return 0; }
    return DefWindowProcW(window, message, w, l);
}

LRESULT CALLBACK QuickPick::inputProc(HWND window, UINT message, WPARAM w, LPARAM l,
                                    UINT_PTR, DWORD_PTR data) {
    auto* picker = reinterpret_cast<QuickPick*>(data);
    if (message == WM_KEYDOWN && picker->handleKey(static_cast<unsigned>(w))) return 0;
    if (message == WM_CHAR && (w == '\r' || w == VK_ESCAPE)) return 0;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, inputProc, 1);
    return DefSubclassProc(window, message, w, l);
}

} // namespace orbit
