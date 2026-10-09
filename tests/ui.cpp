// Exercise the real editor layout and pointer handlers on a private desktop.
#include "../src/main.cpp"
#include <iostream>

struct CustomThemePickerTest
{
    Color initial, chosen;
    bool cancel = false;
    std::string error;
} customThemePickerTest;
void driveCustomThemePicker(HWND window)
{
    try
    {
        auto require = [](bool ok, const char *message) {
            if (!ok)
                throw std::runtime_error(message);
        };
        wchar_t title[64]{}, action[32]{}, initial[16]{};
        GetWindowTextW(window, title, 64);
        GetDlgItemTextW(window, IDOK, action, 32);
        GetDlgItemTextW(window, 11, initial, 16);
        require(std::wstring(title) == L"Custom UI color" && std::wstring(action) == L"Apply color" &&
                    std::wstring(initial) == colorHex(customThemePickerTest.initial),
                "The UI color picker has the wrong labels or initial color.");
        require(app.themePickerOpen && !IsWindowEnabled(app.window),
                "The UI color picker did not protect its owner.");
        SendMessageW(app.window, WM_HOTKEY, app.hotkeyId, 0);
        require(!app.capturePending && !app.overlay,
                "A capture shortcut interrupted the UI color picker.");
        SetDlgItemTextW(window, 11, L"#GGGGGG");
        require(!IsWindowEnabled(GetDlgItem(window, IDOK)), "Invalid custom UI hex was accepted.");
        SetDlgItemTextW(window, 11, colorHex(customThemePickerTest.chosen).c_str());
        require(IsWindowEnabled(GetDlgItem(window, IDOK)), "Valid custom UI hex was rejected.");
        SendMessageW(window, WM_COMMAND, customThemePickerTest.cancel ? IDCANCEL : IDOK, 0);
    }
    catch (const std::exception &failure)
    {
        customThemePickerTest.error = failure.what();
        SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
    }
}

int wmain()
{
    const auto originalStation = GetProcessWindowStation();
    const auto originalDesktop = GetThreadDesktop(GetCurrentThreadId());
    HWINSTA station = nullptr;
    HDESK desktop = nullptr;
    bool com = false;
    int result = 0;
    try
    {
        auto require = [](bool ok, const char *message) {
            if (!ok)
                throw std::runtime_error(message);
        };
        station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
        require(station && SetProcessWindowStation(station), "Cannot isolate UI test station.");
        desktop = CreateDesktopW(L"TigerSnipUITest", nullptr, nullptr, 0,
                                 DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU | DESKTOP_READOBJECTS |
                                     DESKTOP_WRITEOBJECTS,
                                 nullptr);
        require(desktop && SetThreadDesktop(desktop), "Cannot isolate UI test desktop.");
        check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Cannot initialize COM.");
        com = true;
        app.instance = GetModuleHandleW(nullptr);
        app.smoke = true;
        app.softwareRendering = true;
        app.iniPath = (std::filesystem::current_path() / L"ui-settings.ini").wstring();
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&controls);
        registerClasses();
        require(CreateWindowExW(0, MainClass, L"UI test", WS_OVERLAPPEDWINDOW, 0, 0, 1280, 840,
                                nullptr, createMenu(), app.instance, nullptr),
                "Cannot create UI test editor.");
        // The initial fixture uses pixel coordinates; do not inherit the host's display scaling.
        // Later cases explicitly exercise 100/150/200% DPI with matching window sizes.
        app.dpi = 1;
        app.windowedMenu = GetMenu(app.window);
        app.menuHidden = true;
        SetMenu(app.window, nullptr);
        ShowWindow(app.window, SW_SHOWNOACTIVATE);
        // Both interfaces preserve the complete existing menu tree.
        const HMENU menu = app.windowedMenu;
        const std::array<int, 23> settings = {Settings, Preferences, AutoCopy,
                                              RenderingSettings, SaveLocation,
                                              Startup,           ProfessionalBorder,
                                              ProfessionalBlur,  ProfessionalRounded,
                                              SamtecLogo,        ToggleActions,
                                              ToggleTools,       ToggleFormatting,
                                              FullScreen,        About,
                                              InterfaceClassic,  InterfaceOrange,
                                              ThemePurple, ThemeBlue, ThemeTeal,
                                              AppearanceLight, AppearanceDark, ThemeCustom};
        for (int id : settings)
            require(GetMenuState(menu, id, MF_BYCOMMAND) != static_cast<UINT>(-1),
                    "An existing setting or view option is missing.");
        require(GetMenuState(app.colorThemeMenu, ThemeOrange, MF_BYCOMMAND) == static_cast<UINT>(-1) &&
                    GetMenuItemCount(app.colorThemeMenu) == 4,
                "The native menu still offers the retired Orange preset.");
        for (int style = 0; style < 6; ++style)
            require(GetMenuState(app.logoMenu, LogoStyleFirst + style, MF_BYCOMMAND) !=
                        static_cast<UINT>(-1),
                    "A Samtec logo setting is missing.");
        std::array<HMENU, 5> submenus{};
        for (int i = 0; i < 5; ++i)
            submenus[i] = GetSubMenu(menu, i);
        for (int repeat = 0; repeat < 2; ++repeat)
        {
            command(AppMenu);
            require(app.settingsPanelOpen && !app.settingsWindow && !GetMenu(app.window),
                    "Side Settings did not open its in-editor panel.");
            processKey(VK_ESCAPE);
            require(!app.settingsPanelOpen, "Escape did not dismiss Settings.");
            require(IsMenu(menu) && GetMenuItemCount(menu) == 5,
                    "Closing the gear popup destroyed the persistent menus.");
            for (int i = 0; i < 5; ++i)
                require(IsMenu(submenus[i]) && GetSubMenu(menu, i) == submenus[i],
                        "The gear popup lost an existing submenu.");
        }
        app.graphics.initialize();
        auto fixture = Bitmap::create(1120, 720);
        for (size_t i = 0; i < fixture.pixels.size(); i += 4)
            fixture.pixels[i] = fixture.pixels[i + 1] = fixture.pixels[i + 2] =
                fixture.pixels[i + 3] = 255;
        std::vector<Annotation> page;
        auto label = [&](Point p, const wchar_t *s, float size, Color c = Ink) {
            Annotation a;
            a.kind = Tool::Text;
            a.a = p;
            a.text = s;
            a.fontSize = size;
            a.color = c;
            a.textWidth = 1000;
            app.graphics.measureText(a);
            page.push_back(a);
        };
        auto box = [&](Rect r, Color c) {
            Annotation a;
            a.kind = Tool::Rectangle;
            a.style = 3;
            a.color = c;
            a.a = {r.left, r.top};
            a.b = {r.right, r.bottom};
            a.thickness = 1;
            page.push_back(a);
        };
        label({36, 28}, L"Engineering  /  Design review", 15, Muted);
        label({36, 72}, L"Connector assembly review", 32);
        label({36, 132}, L"Check contact alignment before releasing the drawing.", 19, Muted);
        box({36, 192, 714, 636}, rgb(245, 247, 249));
        for (int i = 0; i < 12; ++i)
        {
            box({100.0f + i * 43, 277, 119.0f + i * 43, 301}, rgb(194, 159, 81));
            box({100.0f + i * 43, 462, 119.0f + i * 43, 502}, rgb(194, 159, 81));
        }
        box({86, 300, 636, 462}, rgb(43, 48, 55));
        box({103, 325, 619, 437}, rgb(65, 70, 78));
        for (int i = 0; i < 12; ++i)
        {
            box({108.0f + i * 42, 347, 124.0f + i * 42, 415}, rgb(16, 20, 26));
            box({112.0f + i * 42, 378, 120.0f + i * 42, 405}, rgb(205, 174, 99));
        }
        label({747, 202}, L"Review checklist", 23);
        label({747, 260}, L"Contact position", 18);
        label({747, 294}, L"Housing clearance", 18);
        label({747, 328}, L"Drawing dimensions", 18);
        label({747, 420}, L"Notes", 23);
        label({747, 468}, L"Confirm the highlighted", 18, Muted);
        label({747, 497}, L"contact in the next review.", 18, Muted);
        app.image = app.graphics.flatten(fixture, page);
        app.savePath = L"Connector review.png";
        app.recentSequence = 1;
        Annotation arrow;
        arrow.kind = Tool::Arrow;
        arrow.color = Accent;
        arrow.thickness = 4;
        arrow.a = {155, 594};
        arrow.b = {329, 398};
        app.document.items = {arrow};
        app.document.selected = 0;
        updateView();
        buildButtons();
        const auto originalPixels = app.image.pixels;
        const auto originalView = app.view;
        auto button = [&](int id) {
            buildButtons();
            require(app.fullScreen || (app.collapsedRows & 1) ||
                        (std::count_if(app.buttons.begin(), app.buttons.end(), [](const Button &b) {
                             return b.command == SaveAs && b.label == L"Save as";
                         }) == 1 &&
                         std::none_of(app.buttons.begin(), app.buttons.end(),
                                      [](const Button &b) { return b.command == Save; })),
                    "The toolbar must offer Save As instead of quick Save.");
            auto b = std::find_if(app.buttons.begin(), app.buttons.end(),
                                  [&](const Button &b) { return b.command == id; });
            if (b == app.buttons.end())
                throw std::runtime_error("Required UI control is missing: " + std::to_string(id));
            return b->rect;
        };
        auto mouse = [&](Point p) {
            return MAKELPARAM(static_cast<int>(p.x * app.dpi), static_cast<int>(p.y * app.dpi));
        };
        auto verifyClassicChrome = [&] {
            if (!app.classicUI)
                return;
            const auto savedRows = app.collapsedRows;
            const auto savedTool = app.tool;
            const auto savedStatus = app.status;
            const auto savedFit = app.fit;
            app.collapsedRows = 0;
            app.tool = Tool::Select;
            app.fit = true;
            app.hover = app.pressed = 0;
            app.status = L"Copied automatically - ready to paste";
            buildButtons();
            auto pixel = [&](const Bitmap &image, Point p) {
                p = p * app.dpi;
                const auto i = (static_cast<size_t>(p.y) * image.width + static_cast<int>(p.x)) * 4;
                return rgb(image.pixels[i + 2], image.pixels[i + 1], image.pixels[i]);
            };
            auto background = [&](const Bitmap &image, int id) {
                const auto r = button(id);
                return pixel(image, {r.left + 12, r.top + 4});
            };
            const auto neutral = renderEditorPreview();
            for (int id : {PenTool, HighlightTool, TextTool, EraserTool, CircleTool,
                           ArrowTool, CheckTool, LineTool, Copy, SaveAs})
                require(background(neutral, id) == uiSurface(),
                        "An inactive Classic control has an inconsistent or accent-tinted surface.");
            for (int id : {PenTool, HighlightTool, TextTool})
            {
                const auto r = button(id);
                require(pixel(neutral, {r.left + 18, r.top}) == uiBorder(),
                        "Classic drawing tools do not share the same neutral outline.");
            }
            const auto groups = toolbarLayout();
            for (auto r : {groups.draw, groups.shapes})
                require(pixel(neutral, {(r.left + r.right) / 2, r.top + 2}) == uiRaised(),
                        "Classic tool groups do not share the same neutral surface.");
            require(background(neutral, Fit) == uiRaised(),
                    "The active Fit mode uses an accent tint instead of a neutral selection.");
            saveBytes(std::wstring(L"ui-classic-clean-") +
                          (app.colorTheme == 4 ? L"custom" : ThemeNames[app.colorTheme]) +
                          (app.darkTheme ? L"-dark.png" : L"-light.png"),
                      app.graphics.png(neutral));
            app.tool = Tool::Highlight;
            const auto selectedHighlight = renderEditorPreview();
            require(background(selectedHighlight, HighlightTool) == uiSelected() &&
                        background(selectedHighlight, SelectTool) == uiSurface(),
                    "Highlight does not use the shared selected-tool appearance.");
            app.tool = Tool::Select;
            app.hover = ArrowStyleMenu;
            const auto hoveredSplit = renderEditorPreview();
            require(background(hoveredSplit, ArrowTool) == background(hoveredSplit, ArrowStyleMenu) &&
                        background(hoveredSplit, ArrowTool) != uiSurface(),
                    "The split tool's button and chevron do not share a hover surface.");
            app.hover = ArrowTool;
            app.pressed = static_cast<int>(std::find_if(app.buttons.begin(), app.buttons.end(),
                                                       [](const Button &b) { return b.command == ArrowTool; }) -
                                           app.buttons.begin()) + 1;
            const auto pressedSplit = renderEditorPreview();
            require(background(pressedSplit, ArrowTool) == background(pressedSplit, ArrowStyleMenu),
                    "The split tool's button and chevron do not share a pressed surface.");
            app.hover = app.pressed = 0;
            app.tool = Tool::Arrow;
            app.erasing = true;
            const auto erasing = renderEditorPreview();
            require(background(erasing, EraserTool) == uiSelected() &&
                        background(erasing, ArrowTool) == uiSurface() &&
                        background(erasing, ArrowStyleMenu) == uiSurface(),
                    "A shape chevron remains selected while erasing.");
            app.erasing = false;
            app.cropping = true;
            const auto cropping = renderEditorPreview();
            require(background(cropping, CropTool) == uiSelected() &&
                        background(cropping, ArrowTool) == uiSurface() &&
                        background(cropping, ArrowStyleMenu) == uiSurface(),
                    "A shape chevron remains selected while cropping.");
            app.cropping = false;
            app.collapsedRows = savedRows;
            app.tool = savedTool;
            app.status = savedStatus;
            app.fit = savedFit;
            app.hover = app.pressed = 0;
            buildButtons();
        };
        auto verifyCaptureChrome = [&] {
            const auto savedHover = app.hover, savedPressed = app.pressed;
            app.hover = app.pressed = 0;
            buildButtons();
            auto pixel = [&](const Bitmap &image, Point p) {
                p = p * app.dpi;
                const auto i = (static_cast<size_t>(p.y) * image.width + static_cast<int>(p.x)) * 4;
                return rgb(image.pixels[i + 2], image.pixels[i + 1], image.pixels[i]);
            };
            auto exactFill = [&](const Bitmap &image, Rect r) {
                require(pixel(image, {(r.left + r.right) / 2, r.top + 4}) == uiSolidAccent(),
                        "A capture button changed the chosen accent fill.");
                int foregroundPixels = 0;
                for (int y = static_cast<int>((r.top + 8) * app.dpi);
                     y < static_cast<int>((r.bottom - 8) * app.dpi); ++y)
                    for (int x = static_cast<int>((r.left + 5) * app.dpi);
                         x < static_cast<int>((r.right - 5) * app.dpi); ++x)
                    {
                        const auto i = (static_cast<size_t>(y) * image.width + x) * 4;
                        if (rgb(image.pixels[i + 2], image.pixels[i + 1], image.pixels[i]) ==
                            uiCaptureText())
                            ++foregroundPixels;
                    }
                require(foregroundPixels >= 5,
                        "Capture labels/icons did not use the readable foreground color.");
            };
            require(colorContrast(uiSolidAccent(), uiCaptureText()) >= 4.5,
                    "The capture label/icon has insufficient contrast against its fill.");
            const auto idle = renderEditorPreview();
            for (size_t i = 0; i < app.buttons.size(); ++i)
            {
                const auto &b = app.buttons[i];
                if (b.command != NewSnip && b.command != CaptureMenu)
                    continue;
                exactFill(idle, b.rect);
                app.hover = b.command;
                const auto hover = renderEditorPreview();
                exactFill(hover, b.rect);
                require(hover.pixels != idle.pixels, "Capture hover has no visible feedback.");
                app.pressed = static_cast<int>(i + 1);
                const auto down = renderEditorPreview();
                exactFill(down, b.rect);
                require(down.pixels != hover.pixels, "Capture press has no visible feedback.");
                app.hover = app.pressed = 0;
            }
            if (!app.classicUI && hasImage())
            {
                const auto r = button(Copy);
                require(pixel(idle, {r.left + 8, r.top + 4}) == uiSurface(),
                        "The secondary Copy action still uses a dark primary fill.");
            }
            app.hover = savedHover;
            app.pressed = savedPressed;
        };
        auto slide = [&](int id, float from, float to, bool cancel = false) {
            const auto r = button(id);
            const float cy = (r.top + r.bottom) / 2;
            mouseDown(mouse({r.left + r.width() * from, cy}));
            require(app.sliderDrag == id && GetCapture() == app.window,
                    "Slider did not capture pointer.");
            mouseMove(mouse({r.left + r.width() * to, cy}));
            if (cancel)
                processKey(VK_ESCAPE);
            else
                mouseUp(mouse({r.left + r.width() * to, cy}));
            require(!app.sliderDrag && GetCapture() != app.window && !app.document.editing(),
                    "Slider leaked capture or undo transaction.");
        };
        slide(StrokeSlider, .05f, .18f);
        const float width = app.document.items[0].thickness;
        require(width >= 7 && width <= 9, "Pixel slider did not provide fine control below 40px.");
        require(app.document.undo() && app.document.items[0].thickness == 4 &&
                    !app.document.canUndo(),
                "Slider drag was not a single undo step.");
        require(app.document.redo() && app.document.items[0].thickness == width,
                "Slider redo failed.");
        app.document.selected = 0;
        slide(StrokeSlider, .8f, 1);
        require(app.document.items[0].thickness == 40, "Stroke slider did not stop at 40px.");
        const auto increase = button(SizeUp);
        const Point increasePoint{(increase.left + increase.right) / 2,
                                  (increase.top + increase.bottom) / 2};
        mouseDown(mouse(increasePoint));
        mouseUp(mouse(increasePoint));
        require(app.document.items[0].thickness == 41 && app.thickness == 41,
                "The + button could not increase the stroke beyond the slider's 40px limit.");
        slide(StrokeSlider, .8f, .5f, true);
        require(app.document.items[0].thickness == 41 && app.thickness == 41,
                "Canceling a slider drag lost a stroke above the slider's range.");
        command(StrokePresetSecond);
        require(app.document.items[0].thickness == 4, "Stroke preset failed.");
        slide(StrokeSlider, .03f, .74f, true);
        require(app.document.selected == 0 && app.document.items[0].thickness == 4 &&
                    app.thickness == 4,
                "Cancel did not restore stroke and preference.");
        slide(OpacitySlider, 1, .50f);
        require(std::abs(app.document.items[0].opacity - .5f) < .02f, "Opacity slider failed.");
        const auto translucent = app.graphics.flatten(app.image, app.document.items);
        auto opaqueItems = app.document.items;
        opaqueItems[0].opacity = 1;
        require(translucent.pixels != app.graphics.flatten(app.image, opaqueItems).pixels,
                "Opacity was omitted from export.");
        auto invisibleItems = opaqueItems;
        invisibleItems[0].opacity = 0;
        require(app.graphics.flatten(app.image, invisibleItems).pixels == app.image.pixels,
                "Zero opacity changed the image.");
        require(app.graphics.decode(app.graphics.png(translucent)).pixels == translucent.pixels,
                "Opacity PNG round trip failed.");
        require(app.document.undo() && app.document.items[0].opacity == 1, "Opacity undo failed.");
        app.document.selected = 0;
        slide(OpacitySlider, .2f, .3f, true);
        require(app.document.items[0].opacity == 1, "Opacity cancellation failed.");
        command(styleCommand(Tool::Arrow, 2), true);
        require(app.document.selected == 0 && app.document.items[0].style == 2,
                "Selected arrow style did not update in place.");
        require(app.document.undo() && app.document.items[0].style == 0, "Style undo failed.");
        app.document.selected = 0;
        require(app.image.pixels == originalPixels && app.view.origin == originalView.origin &&
                    app.view.scale == originalView.scale,
                "Editing properties changed capture or viewport.");
        require(saveToolPreferences(), "Cannot save opacity preferences.");
        app.opacities.fill(0);
        loadToolPreferences();
        require(std::abs(app.opacities[static_cast<size_t>(Tool::Arrow)] - .5f) < .02f,
                "Opacity preferences did not restore independently of annotation undo.");
        const auto layoutExport = renderedExport();
        const auto layoutItems = app.document.items;
        const auto layoutSelection = app.document.selected;
        const auto layoutUndo = app.document.canUndo();
        command(ToggleFormatting);
        command(InterfaceClassic);
        require(app.classicUI && GetMenu(app.window) == app.windowedMenu &&
                    canvasRect().left == 0 && canvasRect().right == clientDips().right &&
                    app.collapsedRows == 0 &&
                    (GetMenuState(app.interfaceMenu, InterfaceClassic, MF_BYCOMMAND) & MF_CHECKED),
                "Top toolbars did not activate with their native menu.");
        require(preferenceUInt(app.iniPath, L"Settings", L"ToolbarLayout", 9) == 0,
                "The toolbar layout did not save immediately.");
        command(ToggleTools);
        command(InterfaceOrange);
        // Every previous setting/action is available through the new panel.
        command(AppMenu);
        std::vector<int> panelCommands;
        for (int page = SettingsPageFirst; page <= SettingsPageLast; ++page)
        {
            command(page);
            for (const auto &control : settingsPanelLayout().controls)
                panelCommands.push_back(control.command);
        }
        require(std::find(panelCommands.begin(), panelCommands.end(), ThemeOrange) == panelCommands.end(),
                "The Settings panel still offers the retired Orange preset.");
        for (int id : {InterfaceClassic, InterfaceOrange, ThemePurple, ThemeBlue,
                       ThemeTeal, ThemeCustom, AppearanceLight, AppearanceDark, SettingsRenderer, Startup,
                       SaveLocation, SaveFormatPng, SaveFormatJpg, SettingsAreaKey, SettingsAllKey, AutoCopy, ProfessionalBorder,
                       ProfessionalBlur, ProfessionalRounded, SamtecLogo, ToggleActions,
                       ToggleTools, ToggleFormatting, FullScreen, Fit, Actual, NewSnip, InstantSnip,
                       RecentSnips, Copy, Save, SaveAs, Undo, Redo, DeleteSelected, Clear,
                       CropTool, EraserTool, Exit})
            require(std::find(panelCommands.begin(), panelCommands.end(), id) != panelCommands.end(),
                    "The modern Settings panel lost an existing option or action.");
        for (int style = 0; style < 6; ++style)
            require(std::find(panelCommands.begin(), panelCommands.end(), LogoStyleFirst + style) !=
                        panelCommands.end(),
                    "The modern Settings panel lost a logo style.");
        command(SettingsPageFirst);
        auto settingsClick = [&](int id) {
            const auto layout = settingsPanelLayout();
            const auto target = std::find_if(layout.controls.begin(), layout.controls.end(),
                                             [&](const SettingsControl &control) { return control.command == id; });
            require(target != layout.controls.end(), "Settings control is missing from its page.");
            if (target->content)
            {
                if (target->rect.top < layout.body.top)
                    app.settingsScroll -= layout.body.top - target->rect.top;
                else if (target->rect.bottom > layout.body.bottom)
                    app.settingsScroll += target->rect.bottom - layout.body.bottom;
                app.settingsScroll = std::clamp(app.settingsScroll, 0.0f, layout.maxScroll);
            }
            buildButtons();
            auto control = std::find_if(app.buttons.begin() + app.settingsButtonsStart,
                                        app.buttons.end(), [&](const Button &b) { return b.command == id; });
            require(control != app.buttons.end(), "Settings pointer control is missing.");
            Point point{(control->rect.left + control->rect.right) / 2,
                        (control->rect.top + control->rect.bottom) / 2};
            mouseDown(mouse(point));
            mouseUp(mouse(point));
        };
        settingsClick(ThemeBlue);
        require(app.colorTheme == 2 && app.settingsPanelOpen && !app.pressed &&
                    GetCapture() != app.window,
                "Settings theme cards did not release pointer capture and apply the theme.");
        settingsClick(AppearanceDark);
        require(app.darkTheme, "Settings appearance cards did not apply Dark.");
        settingsClick(InterfaceClassic);
        require(app.classicUI && app.settingsPanelOpen && GetMenu(app.window) == menu,
                "Switching to Classic UI closed Settings or lost the native menu.");
        saveBytes(L"ui-classic-settings-dark.png", app.graphics.png(renderEditorPreview()));
        processKey(VK_ESCAPE);
        processKey(VK_F10);
        require(app.classicUI && app.settingsPanelOpen && !app.settingsWindow,
                "F10 did not open full Settings from Classic UI.");
        processKey(VK_ESCAPE);
        SendMessageW(app.window, WM_COMMAND, Preferences, 0);
        require(app.settingsPanelOpen && !app.settingsWindow,
                "The classic Settings menu did not open the full panel.");
        settingsClick(ThemeTeal);
        settingsClick(AppearanceLight);
        require(app.colorTheme == 3 && !app.darkTheme,
                "Classic Settings could not change the UI color and appearance.");
        const auto classicAppearance = settingsPanelLayout();
        require(std::any_of(classicAppearance.controls.begin(), classicAppearance.controls.end(),
                            [](const SettingsControl &c) { return c.command == ThemeCustom; }),
                "Classic Settings did not offer custom UI colors.");
        saveBytes(L"ui-classic-settings-light.png", app.graphics.png(renderEditorPreview()));
        settingsClick(InterfaceOrange);
        require(!app.classicUI && app.settingsPanelOpen && !GetMenu(app.window) &&
                    preferenceUInt(app.iniPath, L"Settings", L"ToolbarLayout", 9) == 1,
                "Classic Settings could not switch to the new UI and save the choice.");
        settingsClick(ThemePurple);
        settingsClick(AppearanceLight);
        const auto themeExport = renderedExport();
        const auto themeItems = app.document.items;
        const auto themeSelection = app.document.selected;
        const auto themeUndo = app.document.canUndo();
        const float themeZoom = app.view.scale;
        // Dimmed background pointer/key events must not edit or zoom the image.
        const auto padding = settingsPanelLayout().panel;
        Point paddedPoint{padding.left + 5, padding.top + 5};
        mouseDown(mouse(paddedPoint));
        mouseMove(mouse(paddedPoint + Point{50, 30}));
        mouseUp(mouse(paddedPoint));
        SendMessageW(app.window, WM_LBUTTONDBLCLK, MK_LBUTTON, mouse(paddedPoint));
        mouseUp(mouse(paddedPoint));
        SendMessageW(app.window, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(app.window),
                     MAKELPARAM(30, 30));
        processKey(VK_DELETE);
        require(!app.textEdit && app.drag == Drag::None && app.settingsPanelOpen &&
                    app.document.items == themeItems && app.document.selected == themeSelection &&
                    app.document.canUndo() == themeUndo && app.view.scale == themeZoom,
                "Settings allowed editing the screenshot underneath.");
        processKey(VK_TAB);
        require(app.settingsFocus != SettingsPageFirst, "Settings keyboard focus did not move.");
        command(SettingsPageFirst + 3);
        const auto savedExportOptions = app.exportOptions;
        settingsClick(ProfessionalBorder);
        settingsClick(ProfessionalRounded);
        settingsClick(ProfessionalBorder);
        for (int layout : {InterfaceClassic, InterfaceOrange})
        {
            command(layout);
            require(!settingsControlSelected(ProfessionalBlur) &&
                        !settingsControlSelected(ProfessionalRounded) &&
                        !enabled(ProfessionalBlur) && !enabled(ProfessionalRounded) &&
                        !(GetMenuState(menu, ProfessionalBlur, MF_BYCOMMAND) & MF_CHECKED) &&
                        !(GetMenuState(menu, ProfessionalRounded, MF_BYCOMMAND) & MF_CHECKED),
                    "Professional components appear on while their parent is off.");
            settingsClick(ProfessionalBlur);
            settingsClick(ProfessionalRounded);
            require(app.exportOptions.professionalBlur && !app.exportOptions.professionalRounded &&
                        renderedExport().pixels == themeExport.pixels,
                    "A disabled component changed saved choices or exported pixels.");
        }
        saveBytes(L"ui-export-components-off.png", app.graphics.png(renderEditorPreview()));
        loadToolPreferences();
        settingsClick(ProfessionalBorder);
        require(settingsControlSelected(ProfessionalBlur) &&
                    !settingsControlSelected(ProfessionalRounded) &&
                    enabled(ProfessionalBlur) && enabled(ProfessionalRounded) &&
                    renderedExport().width == themeExport.width + 40,
                "Enabling Professional Border did not restore saved choices and their effects.");
        saveBytes(L"ui-export-components-on.png", app.graphics.png(renderEditorPreview()));
        const auto pngChoices = app.exportOptions;
        for (int layout : {InterfaceClassic, InterfaceOrange})
        {
            command(layout);
            settingsClick(SaveFormatJpg);
            require(app.exportOptions.jpg && settingsControlSelected(SaveFormatJpg) &&
                        !enabled(ProfessionalBorder) && !enabled(ProfessionalBlur) &&
                        !enabled(ProfessionalRounded) && !settingsControlSelected(ProfessionalBorder) &&
                        !settingsControlSelected(ProfessionalBlur) && !settingsControlSelected(ProfessionalRounded) &&
                        (GetMenuState(menu, ProfessionalBorder, MF_BYCOMMAND) & MF_GRAYED) &&
                        renderedExport().width == app.image.width && previewImage().width == app.image.width &&
                        previewPadding() == 0,
                    "JPG did not disable Professional Border in Settings, menus, preview and export.");
            const auto jpgLayout = settingsPanelLayout();
            require(std::any_of(jpgLayout.controls.begin(), jpgLayout.controls.end(),
                               [](const SettingsControl &control) {
                                   return control.title == L"Professional Border is disabled for JPG.";
                               }), "JPG Settings did not explain why Professional Border is disabled.");
            saveBytes(app.classicUI ? L"ui-jpg-settings-top.png" : L"ui-jpg-settings-side.png",
                      app.graphics.png(renderEditorPreview()));
            command(ProfessionalBorder);
            command(ProfessionalBlur);
            command(ProfessionalRounded);
            loadToolPreferences();
            require(app.exportOptions.jpg && app.exportOptions.professionalBorder == pngChoices.professionalBorder &&
                        app.exportOptions.professionalBlur == pngChoices.professionalBlur &&
                        app.exportOptions.professionalRounded == pngChoices.professionalRounded,
                    "JPG did not persist or changed the remembered PNG border choices.");
            settingsClick(SaveFormatPng);
            require(app.exportOptions == pngChoices && enabled(ProfessionalBlur) &&
                        settingsControlSelected(ProfessionalBorder) && renderedExport().width == themeExport.width + 40,
                    "Switching back to PNG did not restore Professional Border.");
        }
        app.exportOptions = savedExportOptions;
        app.exportPreferencesDirty = true;
        require(saveToolPreferences(), "Cannot restore export preferences after component checks.");
        updateMenus();
        processKey(VK_END);
        require(app.settingsScroll == settingsPanelLayout().maxScroll && app.settingsScroll > 0,
                "Settings cannot scroll to all export options.");
        saveBytes(L"ui-settings-export.png", app.graphics.png(renderEditorPreview()));
        // Shortcut recording uses the same transactional registration as the original dialog.
        command(SettingsPageFirst + 2);
        const auto savedArea = app.hotkey;
        BYTE previousKeyboard[256]{}, emptyKeyboard[256]{};
        GetKeyboardState(previousKeyboard);
        SetKeyboardState(emptyKeyboard);
        command(SettingsAreaKey);
        processKey(VK_F12);
        require(app.settingsRecording && !app.settingsError.empty() && app.hotkey == savedArea,
                "Settings accepted the reserved F12 shortcut.");
        processKey(VK_ESCAPE);
        require(app.settingsPanelOpen && !app.settingsRecording && app.hotkey == savedArea,
                "Cancelling shortcut recording changed the shortcut or closed Settings.");
        SetKeyboardState(previousKeyboard);
        saveBytes(L"ui-settings-capture.png", app.graphics.png(renderEditorPreview()));
        command(SettingsPageFirst);
        // Themes apply independently of layout and never change the rendered export.
        for (int appearance : {AppearanceLight, AppearanceDark})
            for (int theme : {ThemePurple, ThemeBlue, ThemeTeal})
            {
                command(appearance);
                command(theme);
                saveBytes(std::wstring(L"ui-settings-") + ThemeNames[theme - ThemePurple] +
                              (app.darkTheme ? L"-dark.png" : L"-light.png"),
                          app.graphics.png(renderEditorPreview()));
                processKey(VK_ESCAPE);
                for (int layout : {InterfaceClassic, InterfaceOrange})
                {
                    command(layout);
                    if (app.classicUI)
                    {
                        MENUITEMINFOW root{};
                        root.cbSize = sizeof(root);
                        root.fMask = MIIM_FTYPE | MIIM_DATA;
                        require(GetMenuItemInfoW(app.windowedMenu, 0, TRUE, &root) &&
                                    !!(root.fType & MFT_OWNERDRAW) == app.darkTheme &&
                                    rootMenuItem(root.dwItemData) == app.darkTheme,
                                "The native top menu did not follow Light/Dark appearance.");
                    }
                    require(app.colorTheme == static_cast<unsigned>(theme - ThemePurple) &&
                                app.darkTheme == (appearance == AppearanceDark),
                            "Changing layout reset the color theme or appearance.");
                    auto preview = renderEditorPreview();
                    verifyClassicChrome();
                    verifyCaptureChrome();
                    // Sample a solid toolbar surface, away from labels and controls.
                    const size_t i = (static_cast<size_t>(3) * preview.width + 3) * 4;
                    require((preview.pixels[i] < 100) == app.darkTheme,
                            "Dark appearance did not theme the toolbar surface.");
                    saveBytes(std::wstring(app.classicUI ? L"ui-top-" : L"ui-side-") +
                                  ThemeNames[theme - ThemePurple] +
                                  (app.darkTheme ? L"-dark.png" : L"-light.png"),
                              app.graphics.png(preview));
                    require(renderedExport().pixels == themeExport.pixels &&
                                app.document.items == themeItems &&
                                app.document.selected == themeSelection &&
                                app.document.canUndo() == themeUndo,
                            "A theme changed annotation colors, image pixels, selection or history.");
                }
                command(AppMenu);
                command(SettingsPageFirst);
            }
        require(saveToolPreferences(), "Cannot save theme preferences.");
        app.colorTheme = 0;
        app.darkTheme = false;
        loadToolPreferences();
        require(app.colorTheme == 3 && app.darkTheme && !app.classicUI,
                "Color, appearance and layout preferences did not restore independently.");
        const auto priorCustom = app.customUIAccent;
        const auto priorPalette = app.palette;
        const auto priorToolColors = app.colors;
        const Color pickedColor = rgb(202, 47, 136);
        customThemePickerTest = {priorCustom, pickedColor, true, {}};
        customUIColor(driveCustomThemePicker);
        require(customThemePickerTest.error.empty() && !app.themePickerOpen &&
                    app.customUIAccent == priorCustom && app.colorTheme == 3 && app.darkTheme,
                "Canceling the custom UI picker changed appearance preferences.");
        customThemePickerTest = {priorCustom, pickedColor, false, {}};
        customUIColor(driveCustomThemePicker);
        require(customThemePickerTest.error.empty() && !app.themePickerOpen &&
                    app.customUIAccent == pickedColor && app.colorTheme == 4 &&
                    settingsControlSelected(ThemeCustom) &&
                    (GetMenuState(app.colorThemeMenu, ThemeCustom, MF_BYCOMMAND) & MF_CHECKED) &&
                    preferenceUInt(app.iniPath, L"Settings", L"CustomUIAccent", 0) == pickedColor &&
                    preferenceUInt(app.iniPath, L"Settings", L"ColorTheme", 0) == 4,
                "Applying the custom UI picker did not select and immediately save its color.");
        processKey(VK_ESCAPE);
        for (int layout : {InterfaceClassic, InterfaceOrange})
            for (int appearance : {AppearanceLight, AppearanceDark})
            {
                command(layout);
                command(appearance);
                require(app.colorTheme == 4 && app.customUIAccent == pickedColor,
                        "Custom UI color was reset by layout or Light/Dark changes.");
                verifyClassicChrome();
                verifyCaptureChrome();
                saveBytes(std::wstring(app.classicUI ? L"ui-top-custom-" : L"ui-side-custom-") +
                              (app.darkTheme ? L"dark.png" : L"light.png"),
                          app.graphics.png(renderEditorPreview()));
                require(renderedExport().pixels == themeExport.pixels &&
                            app.document.items == themeItems && app.palette == priorPalette &&
                            app.colors == priorToolColors && app.document.selected == themeSelection &&
                            app.document.canUndo() == themeUndo,
                        "Custom UI color changed exports, annotations, palette or drawing defaults.");
            }
        app.colorTheme = 0;
        app.customUIAccent = 0;
        app.darkTheme = false;
        loadToolPreferences();
        require(app.colorTheme == 4 && app.customUIAccent == pickedColor && app.darkTheme,
                "Custom UI color did not survive preference reload.");
        command(ThemePurple);
        require(app.customUIAccent == pickedColor,
                "Selecting a preset discarded the remembered custom color.");
        command(AppMenu);
        command(SettingsPageFirst);
        customThemePickerTest = {pickedColor, pickedColor, false, {}};
        customUIColor(driveCustomThemePicker);
        require(customThemePickerTest.error.empty() && app.colorTheme == 4,
                "Reselecting Custom did not seed the picker with the remembered color.");
        saveBytes(L"ui-settings-custom-dark.png", app.graphics.png(renderEditorPreview()));
        // Black, white and pale colors remain usable in both appearances/layouts.
        processKey(VK_ESCAPE);
        for (Color chosen : {rgb(0, 0, 0), rgb(255, 255, 255), rgb(255, 255, 190),
                             rgb(224, 237, 219), rgb(27, 45, 85), rgb(117, 117, 117)})
        {
            customThemePickerTest = {app.customUIAccent, chosen, false, {}};
            customUIColor(driveCustomThemePicker);
            require(customThemePickerTest.error.empty(), "Cannot choose an extreme UI color.");
            for (int appearance : {AppearanceLight, AppearanceDark})
            {
                command(appearance);
                require(uiSolidAccent() == chosen &&
                            colorContrast(uiSolidAccent(), uiCaptureText()) >= 4.5 &&
                            colorContrast(uiAccentText(), uiSelected()) >= 4.5,
                        "Custom UI labels became unreadable with an extreme color.");
                for (int layout : {InterfaceClassic, InterfaceOrange})
                {
                    command(layout);
                    auto preview = renderEditorPreview();
                    verifyClassicChrome();
                    verifyCaptureChrome();
                    saveBytes(std::wstring(app.classicUI ? L"ui-top-exact-" : L"ui-side-exact-") +
                                  std::to_wstring(chosen) +
                                  (app.darkTheme ? L"-dark.png" : L"-light.png"),
                              app.graphics.png(preview));
                    require(renderedExport().pixels == themeExport.pixels &&
                                app.palette == priorPalette && app.colors == priorToolColors,
                            "An exact accent color changed the exported image or drawing colors.");
                    const size_t i = (static_cast<size_t>(3) * preview.width + 3) * 4;
                    require((preview.pixels[i] < 100) == app.darkTheme,
                            "A custom UI color replaced toolbar surfaces in Dark.");
                }
            }
        }
        customThemePickerTest = {app.customUIAccent, pickedColor, false, {}};
        customUIColor(driveCustomThemePicker);
        command(AppMenu);
        command(SettingsPageFirst);
        command(ThemePurple);
        command(AppearanceLight);
        // Compact DPI layouts keep every control reachable by keyboard/scroll.
        for (float dpi : {1.0f, 1.5f, 2.0f})
        {
            app.dpi = dpi;
            SetWindowPos(app.window, nullptr, 0, 0, static_cast<int>(850 * dpi),
                         static_cast<int>(430 * dpi), SWP_NOZORDER | SWP_NOACTIVATE);
            for (int page = SettingsPageFirst; page <= SettingsPageLast; ++page)
            {
                command(page);
                processKey(VK_END);
                const auto l = settingsPanelLayout();
                for (size_t i = app.settingsButtonsStart; i < app.buttons.size(); ++i)
                {
                    const auto r = app.buttons[i].rect;
                    require(r.left >= l.panel.left && r.right <= l.panel.right &&
                                r.top >= l.panel.top && r.bottom <= l.panel.bottom,
                            "A Settings control is clipped by the compact window.");
                }
                saveBytes(L"ui-settings-compact-" + std::to_wstring(static_cast<int>(dpi * 100)) +
                              L"-" + SettingsPages[page - SettingsPageFirst] + L".png",
                          app.graphics.png(renderEditorPreview()));
            }
        }
        app.dpi = 1;
        SetWindowPos(app.window, nullptr, 0, 0, 1280, 840, SWP_NOZORDER | SWP_NOACTIVATE);
        command(SettingsPageFirst);
        mouseDown(mouse({5, 5}));
        require(!app.settingsPanelOpen && app.drag == Drag::None,
                "Outside click did not dismiss Settings and consume the pointer event.");
        require(!app.classicUI && !GetMenu(app.window) && app.collapsedRows == 4 &&
                    (GetMenuState(app.interfaceMenu, InterfaceOrange, MF_BYCOMMAND) & MF_CHECKED),
                "Side panels did not restore their own visibility settings.");
        command(ToggleFormatting);
        command(InterfaceClassic);
        require(app.collapsedRows == 2, "Top toolbars lost their visibility settings.");
        command(ToggleTools);
        loadToolPreferences();
        require(app.classicUI && app.collapsedRows == 2,
                "Top toolbars and their saved visibility did not survive reload.");
        command(ToggleTools);
        command(InterfaceOrange);
        require(app.document.items == layoutItems && app.document.selected == layoutSelection &&
                    app.document.canUndo() == layoutUndo &&
                    renderedExport().pixels == layoutExport.pixels,
                "Changing toolbar layout changed the image, selection, annotations or history.");
        app.fit = false;
        app.view.scale = .9f;
        updateView();
        command(InterfaceClassic);
        command(InterfaceOrange);
        require(!app.fit && std::abs(app.view.scale - .9f) < .001f,
                "Changing toolbar layout reset the user's zoom.");
        app.fit = true;
        updateView();
        command(FullScreen);
        command(InterfaceClassic);
        require(app.fullScreen && !GetMenu(app.window),
                "Layout switch exposed menus in fullscreen.");
        command(FullScreen);
        require(GetMenu(app.window) == app.windowedMenu,
                "Exiting fullscreen did not restore the top menu.");
        command(InterfaceOrange);
        // Each tool's contextual controls remain inside their own regions at all supported DPIs.
        for (int layout : {InterfaceClassic, InterfaceOrange})
        {
            command(layout);
            for (float dpi : {1.0f, 1.5f, 2.0f})
                for (Point size : {Point{850, 430}, Point{1050, 740}, Point{1280, 840}})
                {
                    app.dpi = dpi;
                    SetWindowPos(app.window, nullptr, 0, 0, static_cast<int>(size.x * dpi),
                                 static_cast<int>(size.y * dpi), SWP_NOZORDER | SWP_NOACTIVATE);
                    verifyCaptureChrome();
                    for (Tool tool : {Tool::Select, Tool::Pen, Tool::Highlight, Tool::Text,
                                      Tool::Arrow, Tool::Circle, Tool::Check, Tool::Line})
                    {
                        app.tool = tool;
                        app.document.selected = -1;
                        app.inspectorScroll = 0;
                        updateView();
                        buildButtons();
                        const auto c = clientDips(), viewport = canvasRect();
                        for (const auto &b : app.buttons)
                        {
                            require(b.rect.left >= 0 && b.rect.top >= 0 &&
                                        b.rect.right <= c.right + .1f &&
                                        b.rect.bottom <= c.bottom + .1f,
                                    "A UI control is clipped by the window.");
                            if (!app.classicUI && propertyCommand(b.command) &&
                                b.rect.left >= viewport.left)
                                require(b.rect.left >= viewport.right,
                                        "Properties overlap the screenshot.");
                        }
                        saveBytes(std::wstring(app.classicUI ? L"ui-top-" : L"ui-side-") +
                                      std::to_wstring(static_cast<int>(dpi * 100)) + L"-" +
                                      std::to_wstring(static_cast<int>(size.x)) + L"-" +
                                      ToolNames[static_cast<int>(tool)] + L".png",
                                  app.graphics.png(renderEditorPreview()));
                    }
                }
        }
        app.dpi = 1;
        SetWindowPos(app.window, nullptr, 0, 0, 1280, 840, SWP_NOZORDER | SWP_NOACTIVATE);
        app.tool = Tool::Select;
        app.document.selected = 0;
        app.inspectorScroll = 0;
        saveBytes(L"ui-editor.png", app.graphics.png(renderEditorPreview()));
        command(InterfaceClassic);
        saveBytes(L"ui-top-editor.png", app.graphics.png(renderEditorPreview()));
        command(InterfaceOrange);
        // A large custom palette can be scrolled without changing the screenshot geometry.
        for (int i = 0; i < 56; ++i)
            app.palette.push_back(rgb(i * 3, i * 2, i));
        app.inspectorScroll = inspectorLayout().maxScroll;
        buildButtons();
        require(inspectorLayout().maxScroll > 0 && button(OpacitySlider).left >= canvasRect().right,
                "Long inspector did not scroll to opacity controls.");
        app.palette.assign(Palette.begin(), Palette.end());
        app.inspectorScroll = 0;
        command(ToggleFormatting);
        require(canvasRect().right == clientDips().right,
                "Closing inspector did not return canvas space.");
        command(ToggleFormatting);
        // The footer control toggles view modes, including when a small snip fits at 100%.
        const auto zoomTestImage = app.image;
        const auto zoomTestDocument = app.document;
        auto footerLabel = [&] {
            buildButtons();
            const auto b = std::find_if(app.buttons.begin(), app.buttons.end(),
                                       [](const Button &b) { return b.command == ToggleFit; });
            require(b != app.buttons.end(), "The footer view-mode control is missing.");
            return b->label;
        };
        auto clickViewMode = [&] {
            const auto r = button(ToggleFit);
            const Point point{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
            mouseDown(mouse(point));
            mouseUp(mouse(point));
            renderEditorPreview(); // Painting must retain the explicitly selected mode.
        };
        for (float dpi : {1.0f, 1.5f, 2.0f})
            for (bool small : {false, true})
            {
                app.dpi = dpi;
                SetWindowPos(app.window, nullptr, 0, 0, static_cast<int>(1280 * dpi),
                             static_cast<int>(840 * dpi), SWP_NOZORDER | SWP_NOACTIVATE);
                app.image = Bitmap::create(small ? 160 : 1600, small ? 120 : 1000);
                app.document.clear();
                resetPreview();
                command(Fit);
                require(footerLabel() == L"Fit", "The footer does not show fitted mode.");
                const float fittedScale = app.view.scale;
                clickViewMode();
                require(!app.fit && std::abs(app.view.scale * dpi - 1) < .001f &&
                            footerLabel() == L"100%",
                        "Clicking Fit did not switch to actual size or update its label.");
                clickViewMode();
                require(app.fit && std::abs(app.view.scale - fittedScale) < .001f &&
                            footerLabel() == L"Fit",
                        "Clicking 100% did not restore fitted mode.");
                const auto r = navigationRect();
                zoomAt({(r.left + r.right) / 2, (r.top + r.bottom) / 2}, 1.2f);
                clickViewMode();
                require(app.fit && std::abs(app.view.scale - fittedScale) < .001f,
                        "The footer did not restore Fit after manual zooming.");
            }
        app.dpi = 1;
        SetWindowPos(app.window, nullptr, 0, 0, 1280, 840, SWP_NOZORDER | SWP_NOACTIVATE);
        command(FullScreen);
        command(Fit);
        clickViewMode();
        require(!app.fit && footerLabel() == L"100%", "Fullscreen Fit cannot switch to 100%.");
        clickViewMode();
        require(app.fit && footerLabel() == L"Fit", "Fullscreen 100% cannot switch to Fit.");
        command(FullScreen);
        app.image = zoomTestImage;
        app.document = zoomTestDocument;
        resetPreview();
        command(Fit);
        // Both layouts use a white pulse over the source alpha, with no accent tint or frame.
        app.image = Bitmap::create(160, 120);
        for (size_t i = 0; i < app.image.pixels.size(); i += 4)
        {
            app.image.pixels[i] = 40;
            app.image.pixels[i + 1] = 65;
            app.image.pixels[i + 2] = 100;
            app.image.pixels[i + 3] = 255;
        }
        app.document.clear();
        app.exportOptions.professionalBorder = true;
        app.exportOptions.professionalBlur = true;
        app.exportOptions.professionalRounded = true;
        resetPreview();
        for (int layout : {InterfaceClassic, InterfaceOrange})
        {
            command(layout);
            const auto exported = renderedExport();
            const auto before = renderEditorPreview();
            app.copyFlashStarted = GetTickCount64();
            app.copyNoticeStarted = app.copyFlashStarted;
            const auto flashed = renderEditorPreview();
            auto sample = [&](Point p, int channel) {
                p = app.view.toScreen(p) * app.dpi;
                const size_t i =
                    (static_cast<size_t>(p.y) * before.width + static_cast<int>(p.x)) * 4;
                return std::pair{before.pixels[i + channel], flashed.pixels[i + channel]};
            };
            std::array<float, 3> alpha{};
            for (int channel = 0; channel < 3; ++channel)
            {
                const auto [a, b] = sample({80, 60}, channel);
                require(b > a, "Copy did not flash the image.");
                alpha[channel] = (b - a) / float(255 - a);
                const auto [outsideBefore, outsideAfter] = sample({-18, -18}, channel);
                require(outsideBefore == outsideAfter, "Copy flashed transparent border padding.");
            }
            require(std::abs(alpha[0] - alpha[1]) < .02f && std::abs(alpha[1] - alpha[2]) < .02f,
                    "Copy pulse has a color tint.");
            require(renderedExport().pixels == exported.pixels,
                    "Copy pulse changed exported pixels.");
            app.copyFlashStarted = GetTickCount64() - CopyPulseDuration - 1;
            SendMessageW(app.window, WM_TIMER, CopyFlashTimer, 0);
            require(!app.copyFlashStarted && app.copyNoticeStarted &&
                        renderEditorPreview().pixels != before.pixels,
                    "Copy confirmation disappeared with the image pulse.");
            app.copyNoticeStarted = GetTickCount64() - CopyNoticeDuration - 1;
            SendMessageW(app.window, WM_TIMER, CopyFlashTimer, 0);
            require(!app.copyFlashStarted && !app.copyNoticeStarted &&
                        renderEditorPreview().pixels == before.pixels,
                    "Copy pulse did not disappear cleanly.");
            saveBytes(app.classicUI ? L"ui-top-copy.png" : L"ui-side-copy.png",
                      app.graphics.png(flashed));
        }
        app.image = {};
        app.document.clear();
        app.tool = Tool::Select;
        // The welcome action shares the same exact fill and foreground as the toolbar action.
        for (Color chosen : {rgb(255, 255, 255), rgb(224, 237, 219), rgb(27, 45, 85)})
        {
            customThemePickerTest = {app.customUIAccent, chosen, false, {}};
            customUIColor(driveCustomThemePicker);
            require(customThemePickerTest.error.empty(), "Cannot choose a welcome accent color.");
            for (int appearance : {AppearanceLight, AppearanceDark})
                for (int layout : {InterfaceClassic, InterfaceOrange})
                {
                    command(appearance);
                    command(layout);
                    verifyCaptureChrome();
                    saveBytes(std::wstring(app.classicUI ? L"ui-welcome-top-" : L"ui-welcome-side-") +
                                  std::to_wstring(chosen) +
                                  (app.darkTheme ? L"-dark.png" : L"-light.png"),
                              app.graphics.png(renderEditorPreview()));
                }
        }
        command(ThemePurple);
        command(AppearanceLight);
        for (int layout : {InterfaceClassic, InterfaceOrange})
        {
            command(layout);
            const auto r = button(WelcomeCapture);
            const Point center{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
            require(editorCursor(center) == LoadCursorW(nullptr, IDC_HAND),
                    "Welcome capture icon does not show a clickable cursor.");
            mouseDown(mouse(center));
            mouseUp(mouse(center));
            require(app.capturePending, "Clicking the welcome icon did not initiate a snip.");
            KillTimer(app.window, CaptureTimer);
            cancelCapture();
            ShowWindow(app.window, SW_SHOWNOACTIVATE);
        }
        saveBytes(L"ui-empty.png", app.graphics.png(renderEditorPreview()));
        std::cout
            << "PASS: complete modern settings/menus, overlay input isolation and shortcut recording, "
               "three presets and light/dark in both layouts with identical exports, theme persistence, "
               "custom UI picker Apply/Cancel/validation/reload and remembered color, "
               "exact accent capture fills with readable labels/icons and border hover/press "
               "feedback in both layouts/appearances and welcome actions, neutral Copy, "
               "extreme-color contrast with unchanged drawing defaults, "
               "compact scrolling settings at each DPI; native UI layout at "
               "100/150/200% DPI, compact bounds, preset/custom "
               "stroke sizes, single-step undo/redo, cancellation, opacity export/PNG, "
               "in-place style edits, viewport stability, scrolling and collapse; live toolbar "
               "layout switching/persistence/fullscreen, both layouts at each DPI, neutral "
               "alpha-masked copy pulse and readable confirmation with unchanged exports; "
               "welcome capture icon initiates snips in both layouts; consistent Classic outlines, "
               "neutral actions/zoom/groups and unified split-tool states across every theme; "
               "footer Fit/100% switching for large/small snips at each DPI and fullscreen.\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        result = 1;
    }
    if (app.window)
        DestroyWindow(app.window);
    if (com)
        CoUninitialize();
    if (originalDesktop)
        SetThreadDesktop(originalDesktop);
    if (desktop)
        CloseDesktop(desktop);
    if (originalStation)
        SetProcessWindowStation(originalStation);
    if (station)
        CloseWindowStation(station);
    return result;
}
