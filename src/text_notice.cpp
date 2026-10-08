#include "text_notice.h"
#include "windows_support.h"

namespace snip
{
namespace
{
constexpr wchar_t NoticeClass[] = L"TigerSnip.TextNotice.1";
COLORREF nativeColor(Color c)
{
    return static_cast<COLORREF>(c);
}
} // namespace
void TextNotice::close()
{
    if (window_)
        DestroyWindow(std::exchange(window_, nullptr));
    if (font_)
        DeleteObject(std::exchange(font_, nullptr));
    if (titleFont_)
        DeleteObject(std::exchange(titleFont_, nullptr));
}
void TextNotice::show(const std::wstring &title, const std::wstring &text, POINT anchor, bool dark,
                      Color accent, bool persistent)
{
    close();
    title_ = title;
    preview_ = text;
    dark_ = dark;
    accent_ = accent;
    shortened_ = false;
    hovering_ = false;
    persistent_ = persistent;
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = procedure;
    cls.lpszClassName = NoticeClass;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.style = CS_DROPSHADOW;
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throwWindowsError("Cannot register the text preview.");
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &monitor))
        throwWindowsError("Cannot position the text preview.");
    window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, NoticeClass,
                              title.c_str(), WS_POPUP, anchor.x, anchor.y, 1, 1, nullptr, nullptr,
                              cls.hInstance, this);
    if (!window_)
        throwWindowsError("Cannot create the text preview.");
    dpi_ = GetDpiForWindow(window_) / 96.0f;
    const int padding = static_cast<int>(16 * dpi_),
              width = std::min(static_cast<int>(440 * dpi_),
                               static_cast<int>(monitor.rcWork.right - monitor.rcWork.left)),
              contentWidth = std::max(1, width - 2 * padding),
              lineHeight = static_cast<int>(23 * dpi_);
    font_ = CreateFontW(-static_cast<int>(15 * dpi_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH, L"Segoe UI");
    titleFont_ = CreateFontW(-static_cast<int>(15 * dpi_), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                             FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (!font_ || !titleFont_)
        throwWindowsError("Cannot create the text preview font.");
    HDC dc = GetDC(window_);
    if (!dc)
        throwWindowsError("Cannot measure the text preview.");
    auto oldFont = SelectObject(dc, font_);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    const int maxTextHeight = metrics.tmHeight * 4;
    auto measured = [&](const std::wstring &s) {
        RECT r{0, 0, contentWidth, 0};
        DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r,
                  DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        return r;
    };
    // Bound measurement work as well as visible height for long captures.
    const auto extent = preview_.size() > 800 ? RECT{} : measured(preview_);
    if (preview_.size() > 800 || extent.bottom > maxTextHeight || extent.right > contentWidth)
    {
        shortened_ = true;
        size_t low = 0, high = std::min<size_t>(800, preview_.size());
        while (low < high)
        {
            const size_t middle = (low + high + 1) / 2;
            const auto bounds = measured(preview_.substr(0, middle) + L"\u2026");
            if (bounds.bottom <= maxTextHeight && bounds.right <= contentWidth)
                low = middle;
            else
                high = middle - 1;
        }
        if (low && preview_[low - 1] >= 0xd800 && preview_[low - 1] <= 0xdbff)
            --low;
        preview_ = preview_.substr(0, low) + L"\u2026";
    }
    const int textHeight = preview_.empty() ? 0 : measured(preview_).bottom;
    SelectObject(dc, oldFont);
    ReleaseDC(window_, dc);
    const int height = padding * 2 + lineHeight +
                       (textHeight ? textHeight + static_cast<int>(8 * dpi_) : 0) +
                       (shortened_ ? lineHeight + static_cast<int>(8 * dpi_) : 0);
    const int x = std::clamp(anchor.x + static_cast<int>(16 * dpi_), monitor.rcWork.left,
                             std::max(monitor.rcWork.left, monitor.rcWork.right - width)),
              y = std::clamp(anchor.y + static_cast<int>(20 * dpi_), monitor.rcWork.top,
                             std::max(monitor.rcWork.top, monitor.rcWork.bottom - height));
    SetWindowPos(window_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (!persistent && !SetTimer(window_, 1, 3000, nullptr))
        close();
}
void TextNotice::paint()
{
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(window_, &ps);
    draw(dc);
    EndPaint(window_, &ps);
}
void TextNotice::draw(HDC dc)
{
    RECT client{};
    GetClientRect(window_, &client);
    auto background = CreateSolidBrush(dark_ ? RGB(30, 34, 43) : RGB(255, 255, 255));
    FillRect(dc, &client, background);
    DeleteObject(background);
    auto border = CreateSolidBrush(dark_ ? RGB(74, 80, 94) : RGB(211, 216, 224));
    FrameRect(dc, &client, border);
    DeleteObject(border);
    SetBkMode(dc, TRANSPARENT);
    const int padding = static_cast<int>(16 * dpi_), lineHeight = static_cast<int>(23 * dpi_);
    RECT title{padding, padding, client.right - padding, padding + lineHeight};
    auto oldFont = SelectObject(dc, titleFont_);
    SetTextColor(dc, dark_ ? RGB(230, 232, 240) : nativeColor(accent_));
    DrawTextW(dc, title_.c_str(), -1, &title, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, font_);
    SetTextColor(dc, dark_ ? RGB(236, 239, 245) : RGB(32, 38, 46));
    RECT text{padding, padding + lineHeight + static_cast<int>(8 * dpi_), client.right - padding,
              client.bottom - padding - (shortened_ ? lineHeight + static_cast<int>(8 * dpi_) : 0)};
    DrawTextW(dc, preview_.c_str(), -1, &text, DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (shortened_)
    {
        SetTextColor(dc, dark_ ? RGB(178, 187, 202) : RGB(101, 111, 128));
        RECT footer{padding, client.bottom - padding - lineHeight, client.right - padding,
                    client.bottom - padding};
        DrawTextW(dc,
                  title_ == L"Text copied" ? L"Preview shortened \u00B7 All text copied"
                                           : L"Preview shortened",
                  -1, &footer, DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(dc, oldFont);
}
LRESULT CALLBACK TextNotice::procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept
{
    auto self = reinterpret_cast<TextNotice *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        self = static_cast<TextNotice *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(hwnd, message, wp, lp);
    switch (message)
    {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        self->paint();
        return 0;
    case WM_PRINTCLIENT:
        self->draw(reinterpret_cast<HDC>(wp));
        return 0;
    case WM_MOUSEMOVE: {
        self->hovering_ = true;
        KillTimer(hwnd, 1);
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        self->hovering_ = false;
        if (!self->persistent_)
            SetTimer(hwnd, 1, 3000, nullptr);
        return 0;
    case WM_TIMER:
        if (!self->hovering_ && !self->persistent_)
            self->close();
        return 0;
    case WM_LBUTTONUP:
        self->close();
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wp, lp);
    }
}
} // namespace snip
