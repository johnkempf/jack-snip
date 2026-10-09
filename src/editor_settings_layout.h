// Settings are an editor overlay. Its controls share the existing command handlers.
constexpr std::array<const wchar_t *, 7> SettingsPages = {
    L"Appearance", L"General", L"Capture", L"Export", L"View", L"Actions", L"About"};
enum class SettingsControlKind
{
    Navigation,
    Heading,
    Text,
    Button,
    Toggle,
    NestedToggle,
    Group,
    Choice,
    Accent,
    Logo
};
struct SettingsControl
{
    Rect rect;
    int command = 0;
    SettingsControlKind kind = SettingsControlKind::Text;
    std::wstring title, detail;
    bool content = true;
};
struct SettingsPanelLayout
{
    Rect panel, body, navigation;
    float maxScroll = 0;
    std::vector<SettingsControl> controls;
};
SettingsPanelLayout settingsPanelLayout()
{
    SettingsPanelLayout l;
    const auto c = clientDips();
    const float width = std::min(760.0f, c.width() - 32),
                height = std::min(646.0f, c.height() - 32);
    l.panel = {(c.width() - width) / 2, (c.height() - height) / 2, (c.width() + width) / 2,
               (c.height() + height) / 2};
    const float navWidth = width < 640 ? 126 : 154;
    l.navigation = {l.panel.left + 12, l.panel.top + 78, l.panel.left + navWidth - 10,
                    l.panel.bottom - 62};
    l.body = {l.panel.left + navWidth + 24, l.panel.top + 82, l.panel.right - 26,
              l.panel.bottom - 64};
    const float navHeight = std::min(44.0f, l.navigation.height() / SettingsPages.size());
    for (int i = 0; i < static_cast<int>(SettingsPages.size()); ++i)
        l.controls.push_back({{l.navigation.left, l.navigation.top + i * navHeight,
                               l.navigation.right, l.navigation.top + (i + 1) * navHeight - 4},
                              SettingsPageFirst + i,
                              SettingsControlKind::Navigation,
                              SettingsPages[i],
                              L"",
                              false});
    l.controls.push_back(
        {{l.panel.right - 44, l.panel.top + 20, l.panel.right - 20, l.panel.top + 44},
         SettingsDismiss,
         SettingsControlKind::Button,
         L"\u00D7",
         L"",
         false});
    l.controls.push_back(
        {{l.panel.right - 108, l.panel.bottom - 45, l.panel.right - 24, l.panel.bottom - 13},
         SettingsDone,
         SettingsControlKind::Button,
         L"Done",
         L"",
         false});
    float y = 0;
    auto row = [&](int command, SettingsControlKind kind, const std::wstring &title,
                   const std::wstring &detail = L"", float height = 64) {
        l.controls.push_back(
            {{l.body.left, y, l.body.right, y + height}, command, kind, title, detail});
        y += height + 12;
    };
    auto heading = [&](const wchar_t *title) {
        row(0, SettingsControlKind::Heading, title, L"", 26);
    };
    auto choices = [&](int first, const wchar_t *a, const wchar_t *b) {
        const float middle = (l.body.left + l.body.right) / 2;
        l.controls.push_back(
            {{l.body.left, y, middle - 4, y + 40}, first, SettingsControlKind::Choice, a, L""});
        l.controls.push_back(
            {{middle + 4, y, l.body.right, y + 40}, first + 1, SettingsControlKind::Choice, b, L""});
        y += 62;
    };
    switch (app.settingsPage)
    {
    case 0:
        heading(L"Interface layout");
        choices(InterfaceClassic, L"Classic UI", L"New UI");
        row(0, SettingsControlKind::Text,
            L"New UI uses side panels. Classic UI uses top toolbars.", L"", 48);
        heading(L"Color theme");
        for (size_t slot = 0; slot < PresetThemeIndices.size(); ++slot)
        {
            const int i = PresetThemeIndices[slot];
            const float width = (l.body.width() - 20) / 3;
            const float left = l.body.left + slot * (width + 10);
            l.controls.push_back({{left, y, left + width, y + 56},
                                  ThemePurple + i,
                                  SettingsControlKind::Accent,
                                  ThemeNames[i], L""});
        }
        y += 80;
        row(ThemeCustom, SettingsControlKind::Accent, L"Custom color",
            colorHex(app.customUIAccent) + L"  \u00B7  Choose color", 56);
        heading(L"Appearance");
        choices(AppearanceLight, L"Light", L"Dark");
        row(0, SettingsControlKind::Text, L"Accent shades adapt to Light and Dark for readability.", L"", 36);
        break;
    case 1:
        row(SettingsRenderer, SettingsControlKind::Toggle, L"Software rendering",
            L"Compatibility mode for smoother updates on some PCs.");
        row(Startup, SettingsControlKind::Toggle, L"Run at sign-in",
            L"Keep Tiger Snip ready in the tray.");
        row(SaveLocation, SettingsControlKind::Button, L"Save location",
            app.saveFolder.empty() ? L"Default location \u00B7 Click to choose a folder"
                                   : app.saveFolder,
            82);
        row(0, SettingsControlKind::Text, L"Preferences are saved for your Windows account.", L"",
            48);
        break;
    case 2:
        row(SettingsAreaKey, SettingsControlKind::Button, L"Capture an area",
            app.settingsRecording == SettingsAreaKey
                ? L"Press a shortcut \u00B7 Esc cancels \u00B7 Backspace disables"
                : hotkeyName(app.hotkey),
            64);
        row(SettingsAllKey, SettingsControlKind::Button, L"Capture all monitors",
            app.settingsRecording == SettingsAllKey
                ? L"Press a shortcut \u00B7 Esc cancels \u00B7 Backspace disables"
                : hotkeyName(app.instantHotkey),
            64);
        row(SettingsTextKey, SettingsControlKind::Button, L"Copy text from an area",
            app.settingsRecording == SettingsTextKey
                ? L"Press a shortcut \u00B7 Esc cancels \u00B7 Backspace disables"
                : hotkeyName(app.textHotkey),
            64);
        row(0, SettingsControlKind::Text, L"Click a shortcut to change it.",
            L"Use a single key or Ctrl/Alt/Shift/Win + key. Global shortcuts override "
            L"other apps. F12 is reserved.\n\n"
            L"If Print Screen opens Windows' capture tool, turn off its shortcut in "
            L"Windows Settings > Accessibility > Keyboard.", 126);
        row(AutoCopy, SettingsControlKind::Toggle, L"Auto copy new snips",
            L"Image captures copy automatically. Text always copies with a preview.");
        break;
    case 3: {
        heading(L"Save format");
        choices(SaveFormatPng, L"PNG", L"JPG");
        row(0, SettingsControlKind::Text,
            app.exportOptions.jpg ? L"Professional Border is disabled for JPG."
                                  : L"PNG preserves transparency and image quality.",
            app.exportOptions.jpg ? L"JPG does not support transparency. Switch to PNG for border, blur, and rounded corners."
                                  : L"Choose JPG for a compressed image without Professional Border.", 80);
        const size_t borderGroup = l.controls.size();
        l.controls.push_back({{l.body.left, y, l.body.right, y}, 0,
                              SettingsControlKind::Group, L"", L""});
        row(ProfessionalBorder, SettingsControlKind::Toggle, L"Professional Border",
            L"The original soft border, applied to Copy and Save.");
        row(0, SettingsControlKind::Text,
            app.exportOptions.jpg ? L"Unavailable while JPG exports are selected."
            : app.exportOptions.professionalBorder ? L"Professional Border options"
                                                  : L"Enable Professional Border to use these options.",
            L"", 28);
        l.controls.back().rect.left += 24;
        l.controls.back().rect.right -= 12;
        auto borderOption = [&](int command, const wchar_t *title, const wchar_t *detail) {
            row(command, SettingsControlKind::NestedToggle, title, detail);
            l.controls.back().rect.left += 24;
            l.controls.back().rect.right -= 12;
        };
        borderOption(ProfessionalBlur, L"Soft shadow", L"Adds transparent padding around the image.");
        borderOption(ProfessionalRounded, L"Rounded corners", L"Keeps screenshot content unchanged.");
        l.controls[borderGroup].rect.bottom = y;
        y += 12;
        row(SamtecLogo, SettingsControlKind::Toggle, L"Samtec logo",
            L"Choose a badge or subtle watermark below.");
        heading(L"Logo style");
        const float half = (l.body.width() - 10) / 2;
        for (int i = 0; i < 6; ++i)
        {
            const float left = l.body.left + (i % 2) * (half + 10), top = y + (i / 2) * 104;
            l.controls.push_back({{left, top, left + half, top + 94},
                                  LogoStyleFirst + i,
                                  SettingsControlKind::Logo,
                                  LogoStyleNames[i], L""});
        }
        y += 322;
        break;
    }
    case 4:
        row(ToggleActions, SettingsControlKind::Toggle, app.classicUI ? L"Actions" : L"Command bar",
            L"Capture, undo, save, and copy controls.");
        row(ToggleTools, SettingsControlKind::Toggle,
            app.classicUI ? L"Tools and shapes" : L"Tool rail",
            app.classicUI ? L"Drawing tools above the image." : L"Drawing tools on the left.");
        row(ToggleFormatting, SettingsControlKind::Toggle,
            app.classicUI ? L"Color and size" : L"Properties panel",
            L"Color, size, style, and opacity controls.");
        row(FullScreen, SettingsControlKind::Button,
            app.fullScreen ? L"Exit full screen" : L"Full screen",
            L"F11 \u00B7 Esc returns to the editor.");
        row(Fit, SettingsControlKind::Button, L"Fit image", L"Center the complete screenshot.", 56);
        row(Actual, SettingsControlKind::Button, L"Actual size (100%)", L"Show one image pixel per screen pixel.", 56);
        break;
    case 5:
        for (auto [id, label] : std::array<std::pair<int, const wchar_t *>, 15>{
                 {{NewSnip, L"New snip"},
                  {InstantSnip, L"Capture all monitors"},
                  {TextSnip, L"Copy text from an area"},
                  {RecentSnips, L"Recent snips"},
                  {Copy, L"Copy image"},
                  {CopySnipText, L"Copy text from snip"},
                  {Save, L"Save image"},
                  {SaveAs, L"Save as"},
                  {Undo, L"Undo"},
                  {Redo, L"Redo"},
                  {DeleteSelected, L"Delete selected annotation"},
                  {Clear, L"Clear annotations"},
                  {CropTool, L"Crop image"},
                  {EraserTool, L"Eraser"},
                  {Exit, L"Exit Tiger Snip"}}})
            row(id, SettingsControlKind::Button, label, L"", 42);
        break;
    default:
        row(0, SettingsControlKind::Heading, L"Tiger Snip 1.0.2", L"", 36);
        row(0, SettingsControlKind::Text, L"Capture. Annotate. Share.", L"Developed by Jack Kempf",
            72);
        row(0, SettingsControlKind::Text, L"Useful shortcuts",
            L"Ctrl+N  New snip\nCtrl+C  Copy\nCtrl+S  Save as\nCtrl+Shift+S  Save as\nCtrl+Z / "
            L"Ctrl+Y  Undo / redo\nCtrl+Shift+R  Recent snips\nF11  Full screen\nEsc  Cancel\n[ / "
            L"]  Brush size",
            230);
        row(0, SettingsControlKind::Text, L"Choose tools from the toolbar.",
            L"Plain letter keys do not activate tools.", 64);
        row(0, SettingsControlKind::Text, L"Close the window to keep Tiger Snip in the tray.",
            L"Actions \u2192 Exit quits the app.", 64);
        break;
    }
    l.maxScroll = std::max(0.0f, y - l.body.height());
    const float offset = l.body.top - std::clamp(app.settingsScroll, 0.0f, l.maxScroll);
    for (auto &control : l.controls)
        if (control.content)
        {
            control.rect.top += offset;
            control.rect.bottom += offset;
        }
    return l;
}
void buildSettingsPanelButtons()
{
    auto l = settingsPanelLayout();
    app.settingsScroll = std::clamp(app.settingsScroll, 0.0f, l.maxScroll);
    app.settingsButtonsStart = app.buttons.size();
    for (const auto &control : l.controls)
        if (control.command)
        {
            auto r = control.rect;
            if (control.content)
            {
                r.top = std::max(r.top, l.body.top);
                r.bottom = std::min(r.bottom, l.body.bottom);
            }
            if (r.bottom > r.top)
                app.buttons.push_back({r, control.command, control.title});
        }
}
