#pragma once
#include "../core.h"
#include "../language/protocol.h"

namespace orbit {
class CompletionPopup {
public:
    ~CompletionPopup();
    void show(HWND owner, POINT anchor, float dpi, const Theme& theme,
              ID2D1Factory* d2d, IDWriteFactory* dw, IDWriteFontCollection* fonts,
              std::vector<language::Completion> items,
              std::function<void(std::size_t)> choose, std::function<void(std::size_t)> select);
    void close();
    bool isOpen() const { return visible_; }
    bool key(UINT key);
    HWND window() const { return window_; }
    std::size_t selected() const { return static_cast<std::size_t>(selected_); }
    const language::Completion* current() const;
    void resolved(const language::Completion& item);
private:
    void move(int delta);
    void accept();
    void render();
    void paragraph(std::wstring_view text, D2D1_RECT_F bounds, unsigned colour, float size, bool mono = false);
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    HWND window_ = nullptr, owner_ = nullptr;
    float dpi_ = 96, width_ = 690, height_ = 280;
    Theme theme_;
    Paint paint_;
    bool visible_ = false;
    ComPtr<IDWriteTextFormat> detailFormat_, documentationFormat_;
    ComPtr<ID2D1Factory> d2d_;
    ComPtr<IDWriteFactory> dw_;
    ComPtr<IDWriteFontCollection> fonts_;
    std::vector<language::Completion> items_;
    int selected_ = 0, first_ = 0;
    std::function<void(std::size_t)> choose_, select_;
};
} // namespace orbit
