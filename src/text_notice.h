#pragma once
#include "model.h"
#include <windows.h>

namespace snip
{
class TextNotice
{
    HWND window_ = nullptr;
    HFONT font_ = nullptr, titleFont_ = nullptr;
    std::wstring title_, preview_;
    bool shortened_ = false, dark_ = false, hovering_ = false, persistent_ = false;
    Color accent_ = 0;
    float dpi_ = 1;
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM) noexcept;
    void paint();
    void draw(HDC dc);

  public:
    TextNotice() = default;
    TextNotice(const TextNotice &) = delete;
    TextNotice &operator=(const TextNotice &) = delete;
    ~TextNotice() { close(); }
    void close();
    void show(const std::wstring &title, const std::wstring &text, POINT anchor, bool dark,
              Color accent, bool persistent = false);
    HWND window() const { return window_; }
};
} // namespace snip
