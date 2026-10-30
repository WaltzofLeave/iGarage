#pragma once

#include "../core.h"

#include <functional>

namespace orbit {

struct QuickPickItem {
    std::string id;
    std::wstring label;
    std::wstring detail;
    int line = -1;
    char kind = 'F';
};

// Reusable native chooser. It owns its windows, edit font and background brush.
// The host supplies immutable rows and receives a selected value after closure.
class QuickPick {
public:
    using SelectionHandler = std::function<void(const QuickPickItem&)>;

    QuickPick() = default;
    ~QuickPick();
    QuickPick(const QuickPick&) = delete;
    QuickPick& operator=(const QuickPick&) = delete;

    void show(HWND owner, float dpi, const Theme& theme, ID2D1Factory* d2d,
              IDWriteFactory* dwrite, IDWriteFontCollection* fonts,
              std::wstring title, std::wstring placeholder,
              std::vector<QuickPickItem> items, SelectionHandler onSelect,
              int initialSelection = 0);
    void close();
    bool isOpen() const { return window_ != nullptr; }
    HWND window() const { return window_; }
    HWND input() const { return input_; }
    std::size_t resultCount() const { return filtered_.size(); }

private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK overlayProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK inputProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT handleMessage(HWND, UINT, WPARAM, LPARAM);
    bool handleKey(unsigned key);
    void filter();
    void choose();
    void moveSelection(int offset);
    void render();
    int pixel(float value) const;
    float logical(int value) const;

    HWND owner_ = nullptr;
    HWND previousFocus_ = nullptr;
    HWND window_ = nullptr;
    HWND overlay_ = nullptr;
    HWND input_ = nullptr;
    HFONT inputFont_ = nullptr;
    HBRUSH inputBrush_ = nullptr;
    float dpi_ = 96;
    Theme theme_;
    Paint paint_;
    ComPtr<ID2D1Factory> d2d_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<IDWriteFontCollection> fonts_;
    std::wstring title_;
    std::vector<QuickPickItem> items_;
    std::vector<std::size_t> filtered_;
    SelectionHandler onSelect_;
    int selected_ = 0;
    int firstVisible_ = 0;
};

} // namespace orbit
