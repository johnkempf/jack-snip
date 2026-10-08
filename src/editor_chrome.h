// Native editor chrome. Uses the same paint path for the window and offscreen verification.
void drawToolGlyph(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *brush, int id, Point p, Color fg)
{
    if (id >= CircleTool && id <= LineTool)
    {
        Annotation glyph;
        glyph.kind = id == CircleTool ? Tool::Rectangle : static_cast<Tool>(id - SelectTool);
        glyph.style = glyph.kind == Tool::Check ? 2 : 0;
        glyph.thickness = 1.7f;
        glyph.color = fg;
        glyph.a = p + Point{2, 2};
        glyph.b = p + Point{18, 18};
        if (glyph.kind == Tool::Arrow || glyph.kind == Tool::Line)
        {
            glyph.a.y = p.y + 18;
            glyph.b.y = p.y + 2;
        }
        app.graphics.drawAnnotations(rt, {glyph});
    }
    else if (id == AppMenu)
    {
        brush->SetColor(color(fg));
        rt->DrawEllipse(D2D1::Ellipse({p.x + 10, p.y + 10}, 6, 6), brush, 1.6f);
        rt->DrawEllipse(D2D1::Ellipse({p.x + 10, p.y + 10}, 2, 2), brush, 1.6f);
        for (int i = 0; i < 8; ++i)
        {
            const float angle = i * 3.14159265f / 4;
            Point v{std::cos(angle), std::sin(angle)};
            rt->DrawLine({p.x + 10 + v.x * 6, p.y + 10 + v.y * 6},
                         {p.x + 10 + v.x * 9, p.y + 10 + v.y * 9}, brush, 2);
        }
    }
    else
        drawUIIcon(rt, brush, id, p, fg);
}
void paintCaptureSurface(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *brush, Rect r,
                         float radius, bool hover, bool down)
{
    const auto box = D2D1::RoundedRect({r.left, r.top, r.right, r.bottom}, radius, radius);
    // Bypass legacy surface remapping so even pure white stays the chosen color in Dark.
    brush->SetColor(color(uiSolidAccent()));
    rt->FillRoundedRectangle(box, brush);
    brush->SetColor(color(mixColor(uiCaptureBorder(), uiCaptureText(),
                                  down ? .55f : hover ? .28f : 0)));
    const float width = down ? 2 : hover ? 1.5f : 1;
    const float inset = width / 2;
    rt->DrawRoundedRectangle(
        D2D1::RoundedRect({r.left + inset, r.top + inset, r.right - inset, r.bottom - inset},
                         radius, radius), brush, width);
}
void paintEditorChrome(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *brush)
{
    const auto client = clientDips(), canvas = canvasRect();
    const auto l = inspectorLayout();
    const Color white = uiSurface(), edge = uiBorder(), peach = uiSelected();
    auto fill = [&](Rect r, Color c) {
        brush->SetColor(color(themeSurfaceColor(c)));
        rt->FillRectangle({r.left, r.top, r.right, r.bottom}, brush);
    };
    auto surface = [&](Rect r, Color c, bool outline = false, Color border = rgb(216, 222, 230)) {
        auto box = D2D1::RoundedRect({r.left, r.top, r.right, r.bottom}, 5, 5);
        brush->SetColor(color(themeSurfaceColor(c)));
        rt->FillRoundedRectangle(box, brush);
        if (outline)
        {
            brush->SetColor(color(themeSurfaceColor(border)));
            rt->DrawRoundedRectangle(box, brush, 1);
        }
    };
    auto text = [&](const std::wstring &s, Rect r, Color c, IDWriteTextFormat *font = nullptr,
                    bool center = false) {
        if (!font)
            font = app.graphics.font.get();
        font->SetTextAlignment(center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                      : DWRITE_TEXT_ALIGNMENT_LEADING);
        brush->SetColor(color(c));
        rt->DrawText(s.c_str(), static_cast<UINT32>(s.size()), font,
                     {r.left, r.top, r.right, r.bottom}, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    auto rule = [&](float y) { fill({l.body.left + 18, y, l.body.right - 18, y + 1}, edge); };
    auto chevron = [&](Point p, Color c) {
        brush->SetColor(color(c));
        rt->DrawLine({p.x - 3, p.y - 1}, {p.x, p.y + 2}, brush, 1.5f);
        rt->DrawLine({p.x, p.y + 2}, {p.x + 3, p.y - 1}, brush, 1.5f);
    };
    fill({0, client.bottom - StatusHeight, client.right, client.bottom}, white);
    fill({0, client.bottom - StatusHeight, client.right, client.bottom - StatusHeight + 1}, edge);
    if (!app.fullScreen)
    {
        fill({0, 0, client.right, toolbarHeight()}, white);
        fill({0, toolbarHeight() - 1, client.right, toolbarHeight()}, edge);
        if (!(app.collapsedRows & 2))
        {
            fill({0, toolbarHeight(), canvas.left, client.bottom - StatusHeight}, white);
            fill({canvas.left - 1, toolbarHeight(), canvas.left, client.bottom - StatusHeight},
                 edge);
        }
        if (!(app.collapsedRows & 1))
        {
            fill({188, 20, 189, 44}, edge);
            fill({286, 20, 287, 44}, edge);
            const float right = client.right - 336;
            if (right > 328)
            {
                std::wstring filename =
                    hasImage() ? app.savePath.empty()
                                     ? L"Snip " + std::to_wstring(std::max(1U, app.recentSequence))
                                     : std::filesystem::path(app.savePath).filename().wstring()
                               : L"Tiger Snip";
                text(filename, {306, 11, right, 32}, Ink);
                text(hasImage() ? app.dirty              ? L"Unsaved changes"
                                  : app.savePath.empty() ? L"Captured locally"
                                                         : L"Saved locally"
                                : L"Capture. Annotate. Share.",
                     {306, 32, right, 52}, Muted, app.graphics.smallFont.get());
            }
        }
        if (hasImage() && !(app.collapsedRows & 4))
        {
            fill(l.panel, white);
            fill({l.panel.left, l.panel.top, l.panel.left + 1, l.panel.bottom}, edge);
            const int tool = app.erasing                     ? EraserTool
                             : app.cropping                  ? CropTool
                             : inspectorTool() == Tool::Text ? TextTool
                             : inspectorTool() == Tool::Highlight
                                 ? HighlightTool
                                 : SelectTool + static_cast<int>(inspectorTool());
            drawToolGlyph(rt, brush, tool, {l.panel.left + 18, l.panel.top + 21}, Accent);
            text(app.erasing    ? L"Eraser"
                 : app.cropping ? L"Crop"
                                : ToolNames[static_cast<int>(inspectorTool())],
                 {l.panel.left + 48, l.panel.top + 14, l.panel.right - 46, l.panel.top + 48}, Ink);
            text(selected() ? L"Selected annotation" : L"Tool properties",
                 {l.panel.left + 18, l.panel.top + 46, l.panel.right - 18, l.panel.top + 69}, Muted,
                 app.graphics.smallFont.get());
            fill({l.panel.left, l.body.top - 1, l.panel.right, l.body.top}, edge);
            rt->PushAxisAlignedClip({l.body.left, l.body.top, l.body.right, l.body.bottom},
                                    D2D1_ANTIALIAS_MODE_ALIASED);
            if (app.erasing || app.cropping)
            {
                text(app.erasing ? L"Erase whole annotations" : L"Choose the area to keep",
                     {l.color.left, l.color.top, l.color.right, l.color.top + 32}, Ink);
                const wchar_t *lines[] = {app.erasing ? L"Click or drag over an annotation."
                                                      : L"Drag a rectangle on the screenshot.",
                                          app.erasing ? L"The screenshot stays untouched."
                                                      : L"Release to crop the image.",
                                          L"Ctrl+Z restores your changes.",
                                          L"Esc cancels the current action."};
                for (int i = 0; i < 4; ++i)
                    text(lines[i],
                         {l.color.left, l.color.top + 44 + i * 28, l.color.right,
                          l.color.top + 72 + i * 28},
                         Muted, app.graphics.smallFont.get());
            }
            else
            {
                text(L"Color", {l.color.left, l.color.top, l.color.right, l.color.top + 22}, Ink);
                surface({l.color.left + 44, l.color.top + 28, l.color.right, l.color.top + 62},
                        white, true);
                const Color c = activeColor();
                wchar_t hex[16]{};
                swprintf_s(hex, L"#%02X%02X%02X", c & 255, (c >> 8) & 255, (c >> 16) & 255);
                text(hex,
                     {l.color.left + 54, l.color.top + 28, l.color.right - 8, l.color.top + 62},
                     Ink);
                rule(l.size.top - 12);
                if (inspectorTool() != Tool::Check)
                {
                    text(textMode()        ? L"Font size"
                         : highlightMode() ? L"Width"
                                           : L"Stroke",
                         {l.size.left, l.size.top, l.size.right - 104, l.size.bottom}, Ink);
                    surface({l.size.right - 74, l.size.top, l.size.right - 24, l.size.bottom - 2},
                            rgb(247, 248, 250), true);
                    text(std::to_wstring(static_cast<int>(propertySize())) + L" px",
                         {l.size.right - 74, l.size.top, l.size.right - 24, l.size.bottom - 2}, Ink,
                         app.graphics.smallFont.get(), true);
                }
                else
                    text(L"Drag the handles to resize", l.size, Muted,
                         app.graphics.smallFont.get());
                if (inspectorStyleMenu())
                {
                    rule(l.styles.top - 12);
                    text(inspectorTool() == Tool::Arrow   ? L"Arrow style"
                         : inspectorTool() == Tool::Line  ? L"Line style"
                         : inspectorTool() == Tool::Check ? L"Check / X style"
                                                          : L"Shape style",
                         {l.styles.left, l.styles.top, l.styles.right, l.styles.top + 22}, Ink);
                }
                else if (textMode())
                {
                    rule(l.styles.top - 12);
                    text(L"Text style",
                         {l.styles.left, l.styles.top, l.styles.right, l.styles.top + 22}, Ink);
                }
                rule(l.opacity.top - 12);
                text(L"Opacity", l.opacity, Ink);
                surface(
                    {l.opacity.right - 54, l.opacity.top, l.opacity.right, l.opacity.bottom - 2},
                    white, true);
                text(std::to_wstring(static_cast<int>(std::lround(propertyOpacity() * 100))) + L"%",
                     {l.opacity.right - 54, l.opacity.top, l.opacity.right, l.opacity.bottom - 2},
                     Ink, app.graphics.smallFont.get(), true);
                rule(l.help.top - 12);
                const wchar_t *hint = selected() ? L"Drag the handles to adjust."
                                                 : L"Drag on the screenshot to draw.";
                if (inspectorTool() == Tool::Select && !selected())
                    hint = L"Select an annotation to edit it.";
                if (textMode())
                    hint = selected() ? L"Drag side handles to reflow text."
                                      : L"Click the image to add text.";
                text(hint, {l.help.left, l.help.top, l.help.right, l.help.top + 26}, Muted,
                     app.graphics.smallFont.get());
                text(L"Ctrl+Z undoes your last change.",
                     {l.help.left, l.help.top + 30, l.help.right, l.help.top + 56}, Muted,
                     app.graphics.smallFont.get());
                if (highlightMode())
                    text(L"Highlight keeps its soft blending.",
                         {l.help.left, l.help.top + 60, l.help.right, l.help.top + 86}, Muted,
                         app.graphics.smallFont.get());
                else if (textMode() && selected())
                    text(L"Corner handles scale the font.",
                         {l.help.left, l.help.top + 60, l.help.right, l.help.top + 86}, Muted,
                         app.graphics.smallFont.get());
            }
            rt->PopAxisAlignedClip();
            if (l.maxScroll > 0)
            {
                const float h =
                    std::max(24.0f, l.body.height() * l.body.height() / l.contentHeight);
                const float top =
                    l.body.top + (l.body.height() - h) * app.inspectorScroll / l.maxScroll;
                surface({l.panel.right - 5, top, l.panel.right - 2, top + h}, rgb(207, 213, 220));
            }
        }
    }
    for (size_t i = 0; i < app.buttons.size(); ++i)
    {
        if (app.settingsPanelOpen && i >= app.settingsButtonsStart)
            continue;
        const auto &b = app.buttons[i];
        if (recentPanelCommand(b.command) || curvedArrowCommand(b.command) || b.command == WelcomeCapture)
            continue;
        const Rect r = b.rect;
        const bool available = enabled(b.command), hover = app.hover == b.command;
        const bool down = app.pressed == static_cast<int>(i + 1) && hover;
        const bool property =
            !app.fullScreen && r.left >= l.panel.left && propertyCommand(b.command);
        if (property)
            rt->PushAxisAlignedClip({l.body.left, l.body.top, l.body.right, l.body.bottom},
                                    D2D1_ANTIALIAS_MODE_ALIASED);
        Color fg = available ? Ink : Muted;
        bool on = active(b.command);
        const bool rail =
            !app.fullScreen && railCommand(b.command) && r.left < 76 && r.top >= toolbarHeight();
        if (rail)
        {
            if (selected() && !app.erasing && !app.cropping)
                on = b.command == (inspectorTool() == Tool::Text        ? TextTool
                                   : inspectorTool() == Tool::Highlight ? HighlightTool
                                   : inspectorTool() == Tool::Rectangle
                                       ? CircleTool
                                       : SelectTool + static_cast<int>(inspectorTool()));
            if (on || (hover && available))
                surface(r, on ? peach : rgb(246, 248, 250));
            if (on)
                fill({1, r.top + 5, 4, r.bottom - 5}, Accent);
            const float cx = (r.left + r.right) / 2;
            const bool compact = r.height() < 40;
            drawToolGlyph(rt, brush, b.command,
                          {cx - 10, compact ? (r.top + r.bottom) / 2 - 10 : r.top + 7},
                          on ? Accent : fg);
            if (!compact)
                text(b.label, {r.left, r.bottom - 23, r.right, r.bottom - 2}, fg,
                     app.graphics.smallFont.get(), true);
        }
        else if (paletteCommand(b.command) ||
                 (b.command == CustomColor && property && r.top < l.palette.top))
        {
            const Color c =
                paletteCommand(b.command) ? app.palette[b.command - ColorFirst] : activeColor();
            const bool chosen = c == activeColor();
            // Palette swatches show actual annotation colors, even in Dark.
            const auto swatch = D2D1::RoundedRect(
                {r.left + 2, r.top + 2, r.right - 2, r.bottom - 2}, 6, 6);
            brush->SetColor(color(c));
            rt->FillRoundedRectangle(swatch, brush);
            brush->SetColor(color(c == rgb(255, 255, 255) ? uiBorder() : c));
            rt->DrawRoundedRectangle(swatch, brush, 1);
            if (chosen && paletteCommand(b.command))
            {
                brush->SetColor(color(Accent));
                rt->DrawRoundedRectangle(
                    D2D1::RoundedRect({r.left - 1, r.top - 1, r.right + 1, r.bottom + 1}, 5, 5),
                    brush, 1.5f);
            }
        }
        else if (b.command == StrokeSlider || b.command == OpacitySlider)
        {
            const bool opacity = b.command == OpacitySlider;
            const float minimum = opacity ? 0 : textMode() ? 8 : highlightMode() ? 4 : 1;
            const float maximum = opacity ? 100 : textMode() ? 144 : StrokeSliderMax;
            const float value = opacity ? propertyOpacity() * 100 : propertySize();
            const float cx =
                r.left +
                (r.width()) * std::clamp((value - minimum) / (maximum - minimum), 0.0f, 1.0f);
            const float cy = (r.top + r.bottom) / 2;
            fill({r.left, cy - 1.5f, r.right, cy + 1.5f}, edge);
            fill({r.left, cy - 1.5f, cx, cy + 1.5f}, available ? Accent : edge);
            brush->SetColor(color(white));
            rt->FillEllipse(D2D1::Ellipse({cx, cy}, 6, 6), brush);
            brush->SetColor(color(available ? Accent : Muted));
            rt->DrawEllipse(D2D1::Ellipse({cx, cy}, 6, 6), brush, hover || down ? 2.5f : 2);
        }
        else if (b.command >= StrokePresetFirst && b.command <= StrokePresetThird)
        {
            const int px = strokePresets()[b.command - StrokePresetFirst];
            const bool chosen = std::abs(propertySize() - px) < .01f;
            surface(r,
                    chosen  ? peach
                    : hover ? rgb(247, 248, 250)
                            : white,
                    true, chosen ? uiSelectedBorder() : edge);
            text(std::to_wstring(px) + L" px", r, fg, app.graphics.smallFont.get(), true);
        }
        else if (b.command >= CircleStyleMenu && b.command <= LineStyleMenu)
        {
            if (r.right <= canvas.left)
            {
                chevron({(r.left + r.right) / 2, (r.top + r.bottom) / 2}, fg);
                continue;
            }
            surface(r, hover ? rgb(250, 251, 252) : white, true);
            Annotation glyph;
            glyph.kind = inspectorTool();
            glyph.style = selected() ? app.document.items[app.document.selected].style
                                     : app.styles[static_cast<int>(glyph.kind)];
            glyph.color = activeColor();
            glyph.thickness = 1.8f;
            glyph.a = {r.left + 10, r.top + 15};
            glyph.b = {r.left + 37, r.bottom - 15};
            if (glyph.kind == Tool::Arrow || glyph.kind == Tool::Line)
                std::swap(glyph.a.y, glyph.b.y);
            app.graphics.drawAnnotations(rt, {glyph});
            text(styleName(glyph.kind, glyph.style), {r.left + 48, r.top, r.right - 24, r.bottom},
                 fg, app.graphics.smallFont.get());
            chevron({r.right - 14, (r.top + r.bottom) / 2}, Muted);
        }
        else if (b.command == ToggleFormatting)
        {
            if (hover)
                surface(r, rgb(247, 248, 250));
            brush->SetColor(color(Muted));
            const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            if (app.collapsedRows & 4)
            {
                rt->DrawLine({cx + 3, cy - 5}, {cx - 2, cy}, brush, 1.5f);
                rt->DrawLine({cx - 2, cy}, {cx + 3, cy + 5}, brush, 1.5f);
            }
            else
            {
                rt->DrawLine({cx - 4, cy - 4}, {cx + 4, cy + 4}, brush, 1.5f);
                rt->DrawLine({cx + 4, cy - 4}, {cx - 4, cy + 4}, brush, 1.5f);
            }
        }
        else
        {
            const bool primary = b.command == NewSnip || b.command == CaptureMenu;
            const bool outlined = b.command == Copy || b.command == Save ||
                                  b.command == RecentSnips || b.command == TextBold ||
                                  b.command == TextBox || b.command == ToggleFit;
            if (primary)
            {
                if ((b.command == NewSnip && r.top < toolbarHeight()) || b.command == CaptureMenu)
                {
                    const int pressed = app.pressed > 0 &&
                                                app.pressed <= static_cast<int>(app.buttons.size())
                                            ? app.buttons[app.pressed - 1].command : 0;
                    const bool splitHover = app.hover == NewSnip || app.hover == CaptureMenu;
                    const bool splitDown =
                        splitHover && (pressed == NewSnip || pressed == CaptureMenu);
                    rt->PushAxisAlignedClip({r.left, r.top, r.right, r.bottom},
                                            D2D1_ANTIALIAS_MODE_ALIASED);
                    paintCaptureSurface(rt, brush, {20, r.top, 180, r.bottom}, 5,
                                        splitHover, splitDown);
                    rt->PopAxisAlignedClip();
                    if (b.command == CaptureMenu)
                    {
                        brush->SetColor(color(uiCaptureText(), .25f));
                        rt->FillRectangle({r.left, r.top + 8, r.left + 1, r.bottom - 8}, brush);
                    }
                }
                else
                    paintCaptureSurface(rt, brush, r, 5, hover, down);
                fg = uiCaptureText();
            }
            else if (outlined || on || hover)
                surface(r,
                        on      ? peach
                        : hover ? rgb(245, 247, 249)
                                : white,
                        outlined, on ? uiSelectedBorder() : edge);
            if (b.command == CaptureMenu)
                chevron({(r.left + r.right) / 2, (r.top + r.bottom) / 2}, fg);
            else if (b.command == NewSnip || b.command == Copy || b.command == Save ||
                     b.command == RecentSnips)
            {
                drawUIIcon(rt, brush, b.command, {r.left + 10, (r.top + r.bottom) / 2 - 10}, fg);
                text(b.command == Copy && app.status.find(L"Copied") == 0 ? L"Copied" : b.label,
                     {r.left + 38, r.top, r.right - (b.command == RecentSnips ? 24 : 6), r.bottom},
                     fg);
                if (b.command == RecentSnips)
                {
                    surface({r.right - 23, r.top + 8, r.right - 6, r.bottom - 8},
                            rgb(241, 243, 246));
                    text(std::to_wstring(app.recent.size()),
                         {r.right - 23, r.top, r.right - 6, r.bottom}, Muted,
                         app.graphics.smallFont.get(), true);
                }
            }
            else if (b.command == Undo || b.command == Redo || b.command == CustomColor ||
                     b.command == Eyedropper)
                drawUIIcon(rt, brush, b.command,
                           {(r.left + r.right) / 2 - 10, (r.top + r.bottom) / 2 - 10}, fg);
            else if (b.command != TextSizeMenu)
                text(b.command == Actual && hasImage()
                         ? std::to_wstring(
                               static_cast<int>(std::lround(app.view.scale * app.dpi * 100))) +
                               L"%"
                         : b.label,
                     r, fg, app.graphics.smallFont.get(), true);
        }
        if (property)
            rt->PopAxisAlignedClip();
    }
    // Rail split-menu chevrons sit above their tool's selected surface.
    for (const auto &b : app.buttons)
        if (b.command >= CircleStyleMenu && b.command <= LineStyleMenu &&
            b.rect.right <= canvas.left)
            chevron({(b.rect.left + b.rect.right) / 2, (b.rect.top + b.rect.bottom) / 2},
                    enabled(b.command) ? Muted : Muted);
    const float y = client.bottom - StatusHeight;
    std::wstring info = app.status.empty() ? hasImage()
                                                 ? std::to_wstring(app.image.width) + L" \u00D7 " +
                                                       std::to_wstring(app.image.height) + L" px"
                                                 : L"Ready when you are"
                                           : app.status;
    text(info, {app.fullScreen ? 266.0f : 90.0f, y, client.right - 210, client.bottom}, Muted,
         app.graphics.smallFont.get());
    if (app.status.empty() && client.width() > 1060 && !app.fullScreen)
        text(hotkeyName(app.hotkey) + L"  New snip",
             {client.right / 2 - 40, y, client.right - 210, client.bottom}, Muted,
             app.graphics.smallFont.get());
}
