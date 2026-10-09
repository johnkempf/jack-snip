bool settingsControlSelected(int id)
{
    if (id >= ThemePurple && id <= ThemeCustom)
        return app.colorTheme == static_cast<unsigned>(id - ThemePurple);
    if (id >= SettingsPageFirst && id <= SettingsPageLast)
        return id - SettingsPageFirst == app.settingsPage;
    if (id >= LogoStyleFirst && id < LogoStyleFirst + 6)
        return app.exportOptions.samtecLogo && id - LogoStyleFirst == app.exportOptions.samtecStyle;
    switch (id)
    {
    case InterfaceClassic:
        return app.classicUI;
    case InterfaceOrange:
        return !app.classicUI;
    case AppearanceLight:
        return !app.darkTheme;
    case AppearanceDark:
        return app.darkTheme;
    case SettingsRenderer:
        return app.softwareRendering;
    case Startup:
        return app.settingsStartup;
    case AutoCopy:
        return app.autoCopy;
    case SaveFormatPng:
        return !app.exportOptions.jpg;
    case SaveFormatJpg:
        return app.exportOptions.jpg;
    case ProfessionalBorder:
        return !app.exportOptions.jpg && app.exportOptions.professionalBorder;
    case ProfessionalBlur:
        return !app.exportOptions.jpg && app.exportOptions.professionalBorder && app.exportOptions.professionalBlur;
    case ProfessionalRounded:
        return !app.exportOptions.jpg && app.exportOptions.professionalBorder && app.exportOptions.professionalRounded;
    case SamtecLogo:
        return app.exportOptions.samtecLogo;
    case ToggleActions:
    case ToggleTools:
    case ToggleFormatting:
        return !(app.collapsedRows & (1U << (id - ToggleActions)));
    default:
        return false;
    }
}
void paintSettingsPanel(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *brush)
{
    const auto l = settingsPanelLayout();
    const auto c = clientDips();
    auto fill = [&](Rect r, Color colorValue, float radius = 0) {
        brush->SetColor(color(colorValue));
        rt->FillRoundedRectangle(
            D2D1::RoundedRect({r.left, r.top, r.right, r.bottom}, radius, radius), brush);
    };
    auto text = [&](const std::wstring &s, Rect r, Color fg, bool small = false,
                    bool centered = false, bool wrap = false) {
        auto font = small ? app.graphics.smallFont.get() : app.graphics.font.get();
        font->SetTextAlignment(centered ? DWRITE_TEXT_ALIGNMENT_CENTER
                                        : DWRITE_TEXT_ALIGNMENT_LEADING);
        font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        const auto wrapping = font->GetWordWrapping();
        if (wrap)
            font->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        brush->SetColor(color(fg));
        rt->DrawText(s.c_str(), static_cast<UINT32>(s.size()), font,
                     {r.left, r.top, r.right, r.bottom}, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        if (wrap)
            font->SetWordWrapping(wrapping);
    };
    brush->SetColor(color(rgb(0, 0, 0), app.darkTheme ? .46f : .16f));
    rt->FillRectangle({0, 0, c.right, c.bottom}, brush);
    fill(l.panel, uiSurface(), 12);
    brush->SetColor(color(uiBorder()));
    rt->DrawRoundedRectangle(
        D2D1::RoundedRect({l.panel.left, l.panel.top, l.panel.right, l.panel.bottom}, 12, 12),
        brush, 1);
    text(L"Settings", {l.panel.left + 24, l.panel.top + 15, l.panel.right - 56, l.panel.top + 41},
         Ink);
    text(L"Make Tiger Snip fit your workflow.",
         {l.panel.left + 24, l.panel.top + 42, l.panel.right - 56, l.panel.top + 65}, Muted, true);
    fill({l.panel.left + 1, l.panel.top + 72, l.panel.right - 1, l.panel.top + 73}, uiBorder());
    fill({l.body.left - 16, l.panel.top + 73, l.body.left - 15, l.panel.bottom - 53}, uiBorder());
    fill({l.panel.left + 1, l.panel.bottom - 53, l.panel.right - 1, l.panel.bottom - 52},
         uiBorder());
    text(app.settingsError.empty() ? L"Changes are saved automatically." : app.settingsError,
         {l.panel.left + 24, l.panel.bottom - 45, l.panel.right - 124, l.panel.bottom - 13},
         app.settingsError.empty() ? Muted : Ink, true, false, true);
    for (const auto &control : l.controls)
    {
        const auto r = control.rect;
        if (control.content)
        {
            if (r.bottom <= l.body.top || r.top >= l.body.bottom)
                continue;
            rt->PushAxisAlignedClip({l.body.left, l.body.top, l.body.right, l.body.bottom},
                                    D2D1_ANTIALIAS_MODE_ALIASED);
        }
        const bool available = !control.command || enabled(control.command);
        const bool selected = settingsControlSelected(control.command);
        const bool hover = control.command && app.hover == control.command;
        const Color fg = available ? Ink : Muted;
        if (control.kind == SettingsControlKind::Group)
        {
            fill(r, uiRaised(), 8);
            brush->SetColor(color(uiBorder()));
            rt->DrawRoundedRectangle(
                D2D1::RoundedRect({r.left + .5f, r.top + .5f, r.right - .5f, r.bottom - .5f}, 8, 8),
                brush, 1);
            fill({r.left + 12, r.top + 64, r.right - 12, r.top + 65}, uiBorder());
        }
        else if (control.kind == SettingsControlKind::Heading ||
            control.kind == SettingsControlKind::Text)
        {
            text(control.title, {r.left, r.top, r.right, r.top + 29},
                 app.settingsPage == 3 && !control.command && control.kind == SettingsControlKind::Text
                     ? Muted : Ink);
            if (!control.detail.empty())
                text(control.detail, {r.left, r.top + 30, r.right, r.bottom}, Muted, true, false, true);
        }
        else
        {
            const bool nested = control.kind == SettingsControlKind::NestedToggle;
            const bool borderHeader = control.command == ProfessionalBorder;
            if (borderHeader)
                fill({r.left + 1, r.top + 1, r.right - 1, r.bottom},
                     selected ? uiSelected() : uiSurface(), 7);
            else if (!nested || selected || hover)
                fill(r, selected ? uiSelected() : hover ? (nested ? uiSurface() : uiRaised())
                                                        : uiSurface(), 7);
            if (control.kind != SettingsControlKind::Navigation && !nested && !borderHeader)
            {
                brush->SetColor(color(selected ? uiSelectedBorder() : uiBorder()));
                rt->DrawRoundedRectangle(
                    D2D1::RoundedRect({r.left + .5f, r.top + .5f, r.right - .5f, r.bottom - .5f}, 7,
                                      7),
                    brush, 1);
            }
            if (control.kind == SettingsControlKind::Navigation)
                text(control.title, {r.left + 12, r.top, r.right - 8, r.bottom},
                     selected ? uiAccentText() : fg);
            else if (control.kind == SettingsControlKind::Toggle || nested)
            {
                text(control.title, {r.left + 12, r.top + 9, r.right - 64, r.top + 33}, fg);
                text(control.detail, {r.left + 12, r.top + 34, r.right - 64, r.bottom - 4}, Muted,
                     true);
                const float cy = (r.top + r.bottom) / 2;
                fill({r.right - 54, cy - 10, r.right - 16, cy + 10},
                     selected && available ? Accent : uiBorder(), 10);
                brush->SetColor(color(rgb(255, 255, 255)));
                rt->FillEllipse(D2D1::Ellipse({r.right - (selected ? 26 : 44), cy}, 7, 7), brush);
            }
            else if (control.kind == SettingsControlKind::Accent)
            {
                const auto accent = control.command == ThemeCustom ? app.customUIAccent
                                      : ThemeAccents[control.command - ThemePurple];
                fill({r.left + 12, r.top + 15, r.left + 38, r.top + 41}, accent, 6);
                if (control.command == ThemeCustom)
                {
                    brush->SetColor(color(uiBorder()));
                    rt->DrawRoundedRectangle(D2D1::RoundedRect(
                        {r.left + 12, r.top + 15, r.left + 38, r.top + 41}, 6, 6), brush, 1);
                }
                text(control.title, {r.left + 48, r.top, r.right - 28, r.bottom}, fg);
                if (control.command == ThemeCustom)
                    text(control.detail, {r.left + 160, r.top, r.right - 28, r.bottom}, Muted, true);
                if (selected)
                    text(L"\u2713", {r.right - 24, r.top, r.right - 6, r.bottom}, Accent);
            }
            else if (control.kind == SettingsControlKind::Logo)
            {
                const int index = control.command - LogoStyleFirst;
                auto &pixels = app.settingsLogoPreviews[index];
                if (pixels.empty())
                    pixels =
                        app.graphics.samtecBadge(static_cast<uint8_t>(index), 28, app.darkTheme);
                auto premultiplied = pixels.pixels;
                for (size_t i = 0; i < premultiplied.size(); i += 4)
                    for (int channel = 0; channel < 3; ++channel)
                        premultiplied[i + channel] = static_cast<uint8_t>(
                            (premultiplied[i + channel] * premultiplied[i + 3] + 127) / 255);
                Com<ID2D1Bitmap> bitmap;
                check(rt->CreateBitmap(
                          D2D1::SizeU(pixels.width, pixels.height), premultiplied.data(),
                          pixels.width * 4,
                          D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                   D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                 96, 96),
                          bitmap.put()),
                      "Cannot draw logo choice.");
                const float scale =
                    std::min((r.width() - 24) / pixels.width, 46.0f / pixels.height);
                const float width = pixels.width * scale, height = pixels.height * scale;
                const float x = (r.left + r.right - width) / 2, y = r.top + (54 - height) / 2 + 4;
                rt->DrawBitmap(bitmap.get(), {x, y, x + width, y + height});
                text(control.title, {r.left + 10, r.top + 61, r.right - 8, r.bottom - 4}, fg, true);
            }
            else
            {
                if (control.command == SettingsDismiss)
                {
                    brush->SetColor(color(Muted));
                    const Point center{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
                    rt->DrawLine({center.x - 4, center.y - 4}, {center.x + 4, center.y + 4}, brush, 1.5f);
                    rt->DrawLine({center.x + 4, center.y - 4}, {center.x - 4, center.y + 4}, brush, 1.5f);
                }
                const bool recording = app.settingsRecording == control.command;
                if (control.command != SettingsDismiss)
                {
                    // Single-line labels use the button's actual height, including Done's
                    // shorter footer button. Detailed rows retain their two-line layout.
                    const Rect label = control.detail.empty()
                                           ? Rect{r.left + 12, r.top, r.right - 12, r.bottom}
                                           : Rect{r.left + 12, r.top + 10, r.right - 12, r.top + 34};
                    text(control.title, label, selected ? uiAccentText() : fg, false,
                         control.command == SettingsDone);
                }
                if (!control.detail.empty())
                    text(control.detail, {r.left + 12, r.top + 37, r.right - 12, r.bottom - 6},
                         recording ? Accent : Muted, true);
            }
            if (control.command == app.settingsFocus)
            {
                brush->SetColor(color(Accent));
                rt->DrawRoundedRectangle(
                    D2D1::RoundedRect({r.left + 2, r.top + 2, r.right - 2, r.bottom - 2}, 6, 6),
                    brush, 1.5f);
            }
        }
        if (control.content)
            rt->PopAxisAlignedClip();
    }
    if (l.maxScroll > 0)
    {
        const float track = l.body.height(),
                    h = std::max(24.0f, track * track / (track + l.maxScroll));
        const float top =
            l.body.top + (track - h) * std::clamp(app.settingsScroll / l.maxScroll, 0.0f, 1.0f);
        fill({l.panel.right - 8, top, l.panel.right - 5, top + h}, uiBorder(), 2);
    }
}
