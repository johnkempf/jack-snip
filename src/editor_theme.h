// UI colors never enter the image/export pipeline or replace annotation colors.
// Keep stored indices stable; index 1 belonged to the retired Orange preset.
constexpr std::array<int, 3> PresetThemeIndices = {0, 2, 3};
constexpr std::array<Color, 4> ThemeAccents = {ClassicAccent, OrangeAccent, rgb(37, 99, 235),
                                               rgb(0, 133, 119)};
constexpr std::array<const wchar_t *, 4> ThemeNames = {L"Purple", L"Orange", L"Blue", L"Teal"};
Color mixColor(Color a, Color b, float amount)
{
    auto component = [&](int shift) {
        return static_cast<unsigned>(
            std::lround(((a >> shift) & 255) * (1 - amount) + ((b >> shift) & 255) * amount));
    };
    return rgb(component(0), component(8), component(16));
}
Color uiSurface()
{
    return app.darkTheme ? rgb(30, 35, 43) : rgb(255, 255, 255);
}
Color uiRaised()
{
    return app.darkTheme ? rgb(39, 45, 55) : rgb(247, 248, 250);
}
Color uiCanvas()
{
    return app.darkTheme ? rgb(20, 24, 31) : rgb(246, 247, 251);
}
Color uiBorder()
{
    return app.darkTheme ? rgb(58, 67, 81) : rgb(224, 228, 233);
}
Color uiSolidAccent()
{
    return app.colorTheme == 4 ? app.customUIAccent : ThemeAccents[app.colorTheme];
}
Color uiSelected()
{
    return mixColor(uiSurface(), uiSolidAccent(), app.darkTheme ? .22f : .10f);
}
Color uiSelectedBorder()
{
    return mixColor(uiSurface(), uiSolidAccent(), .46f);
}
double colorLuminance(Color c)
{
    auto linear = [](unsigned value) {
        const double v = value / 255.0;
        return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
    };
    return .2126 * linear(c & 255) + .7152 * linear((c >> 8) & 255) +
           .0722 * linear((c >> 16) & 255);
}
double colorContrast(Color a, Color b)
{
    const auto x = colorLuminance(a), y = colorLuminance(b);
    return (std::max(x, y) + .05) / (std::min(x, y) + .05);
}
Color readableAccent(Color c, Color surface, Color toward, double minimum)
{
    for (int step = 0; step <= 20; ++step)
    {
        const auto shade = mixColor(c, toward, step / 20.0f);
        if (colorContrast(shade, surface) >= minimum)
            return shade;
    }
    return toward;
}
Color uiAccentText()
{
    return readableAccent(uiSolidAccent(), uiSelected(),
                          app.darkTheme ? rgb(255, 255, 255) : rgb(0, 0, 0), 4.5);
}
Color uiCaptureText()
{
    const auto background = uiSolidAccent();
    const Color dark = rgb(32, 38, 46), white = rgb(255, 255, 255);
    if (colorContrast(background, dark) >= 4.5)
        return dark;
    return colorContrast(background, white) >= 4.5 ? white : rgb(0, 0, 0);
}
Color uiCaptureBorder()
{
    const auto background = uiSolidAccent();
    // Pale fills on Light and dark fills on Dark need an edge, not a different fill.
    if (colorContrast(background, uiSurface()) < 3)
        return readableAccent(background, uiSurface(),
                              app.darkTheme ? rgb(233, 237, 244) : rgb(32, 38, 46), 3);
    return mixColor(background, uiCaptureText(), .18f);
}
Color themeSurfaceColor(Color c)
{
    if (c == Accent)
        return c;
    for (Color tint : {rgb(233, 226, 255), rgb(242, 238, 255), rgb(249, 248, 255),
                       rgb(225, 218, 249), rgb(255, 239, 225), rgb(255, 247, 240),
                       rgb(255, 244, 234), rgb(255, 226, 201), rgb(229, 248, 238)})
        if (c == tint)
            return uiSelected();
    if (c == rgb(219, 211, 248) || c == rgb(255, 194, 143))
        return uiSelectedBorder();
    if (c == rgb(93, 57, 216))
        return mixColor(Accent, rgb(0, 0, 0), .12f);
    if (c == rgb(81, 44, 199))
        return mixColor(Accent, rgb(0, 0, 0), .22f);
    if (c == rgb(255, 255, 255))
        return uiSurface();
    for (Color border : {rgb(218, 221, 232), rgb(231, 233, 241), rgb(227, 229, 238),
                         rgb(226, 229, 237), rgb(229, 225, 243)})
        if (c == border)
            return uiBorder();
    if (app.darkTheme)
    {
        const unsigned r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
        if (std::min({r, g, b}) > 242)
            return uiRaised();
        if (std::min({r, g, b}) > 185)
            return uiBorder();
    }
    return c;
}
void updateInterfaceColors()
{
    const Color base = app.colorTheme == 4 ? app.customUIAccent : ThemeAccents[app.colorTheme];
    Accent = base;
    // Lift dark accents for text/icons; keep the same preset hue in both modes.
    if (app.darkTheme)
        Accent = mixColor(Accent, rgb(255, 255, 255), .23f);
    if (app.colorTheme == 4)
    {
        // Adapt small foreground marks independently of the exact accent surfaces.
        if (app.darkTheme)
        {
            const unsigned largest = std::max({Accent & 255, (Accent >> 8) & 255,
                                               (Accent >> 16) & 255});
            if (largest > 230)
                Accent = mixColor(Accent, rgb(0, 0, 0), 1 - 230.0f / largest);
            Accent = readableAccent(Accent, uiSurface(), rgb(230, 230, 230), 7);
        }
        else
            Accent = readableAccent(base, uiSurface(), rgb(0, 0, 0), 3);
    }
    Ink = app.darkTheme ? rgb(233, 237, 244) : rgb(32, 38, 46);
    Muted = app.darkTheme ? rgb(158, 170, 188) : rgb(112, 121, 135);
}

// Keep the native top menu's labels and submenus, while matching its bar to Dark.
constexpr ULONG_PTR RootMenuTag = 0xC0D000;
constexpr std::array<const wchar_t *, 5> RootMenuLabels = {L"File", L"Edit", L"View", L"Settings",
                                                         L"Help"};
bool rootMenuItem(ULONG_PTR data)
{
    return data >= RootMenuTag && data < RootMenuTag + RootMenuLabels.size();
}
HFONT rootMenuFont()
{
    return CreateFontW(-static_cast<int>(16 * app.dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, L"Segoe UI");
}
std::optional<RECT> darkMenuSeparator(HWND window)
{
    if (!app.darkTheme || !GetMenu(window) || IsIconic(window))
        return {};
    MENUBARINFO menu{};
    menu.cbSize = sizeof(menu);
    RECT bounds{}, client{};
    POINT origin{};
    if (!GetMenuBarInfo(window, OBJID_MENU, 0, &menu) || menu.rcBar.bottom <= menu.rcBar.top ||
        !GetWindowRect(window, &bounds) || !GetClientRect(window, &client) ||
        !ClientToScreen(window, &origin))
        return {};
    const int height = std::max(1, GetSystemMetricsForDpi(SM_CYBORDER, GetDpiForWindow(window)));
    return RECT{origin.x - bounds.left, origin.y - bounds.top - height,
                origin.x - bounds.left + client.right, origin.y - bounds.top};
}
void paintDarkMenuSeparator(HWND window)
{
    const auto line = darkMenuSeparator(window);
    if (!line)
        return;
    const HDC dc = GetWindowDC(window);
    if (!dc)
        return;
    // Windows paints this strip outside the client area after the owner-drawn menu.
    const auto previous = SetDCBrushColor(dc, uiSurface());
    FillRect(dc, &*line, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetDCBrushColor(dc, previous);
    ReleaseDC(window, dc);
}
void applyWindowTheme()
{
    const BOOL dark = app.darkTheme;
    DwmSetWindowAttribute(app.window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    HMENU menu = app.windowedMenu ? app.windowedMenu : GetMenu(app.window);
    if (!menu)
        return;
    auto background = app.darkTheme ? CreateSolidBrush(uiSurface()) : nullptr;
    MENUINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = MIM_BACKGROUND;
    info.hbrBack = background;
    SetMenuInfo(menu, &info);
    if (app.menuBackground)
        DeleteObject(app.menuBackground);
    app.menuBackground = background;
    for (UINT i = 0; i < RootMenuLabels.size(); ++i)
    {
        MENUITEMINFOW item{};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_FTYPE | MIIM_DATA;
        item.fType = app.darkTheme ? MFT_OWNERDRAW : MFT_STRING;
        item.dwItemData = app.darkTheme ? RootMenuTag + i : 0;
        SetMenuItemInfoW(menu, i, TRUE, &item);
    }
    DrawMenuBar(app.window);
    paintDarkMenuSeparator(app.window);
}
void measureRootMenu(MEASUREITEMSTRUCT &item)
{
    HDC dc = GetDC(app.window);
    auto font = rootMenuFont();
    auto old = SelectObject(dc, font);
    const auto label = RootMenuLabels[item.itemData - RootMenuTag];
    SIZE size{};
    GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &size);
    item.itemWidth = size.cx + static_cast<UINT>(14 * app.dpi);
    item.itemHeight = size.cy + static_cast<UINT>(4 * app.dpi);
    SelectObject(dc, old);
    DeleteObject(font);
    ReleaseDC(app.window, dc);
}
void drawRootMenu(const DRAWITEMSTRUCT &item)
{
    const bool on = item.itemState & (ODS_SELECTED | ODS_HOTLIGHT);
    SetDCBrushColor(item.hDC, on ? uiSelected() : uiSurface());
    FillRect(item.hDC, &item.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, Ink);
    auto font = rootMenuFont();
    auto old = SelectObject(item.hDC, font);
    auto rect = item.rcItem;
    DrawTextW(item.hDC, RootMenuLabels[item.itemData - RootMenuTag], -1, &rect,
              DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    SelectObject(item.hDC, old);
    DeleteObject(font);
}
