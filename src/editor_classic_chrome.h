// Original ribbon presentation. Screenshot compositing and exports remain shared.
void paintClassicEditorChrome(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *brush)
{
    const auto client = clientDips();
    auto fill = [&](Rect r, Color c) {
        brush->SetColor(color(c));
        rt->FillRectangle({r.left, r.top, r.right, r.bottom}, brush);
    };
    auto text = [&](const std::wstring &s, Rect r, Color c, IDWriteTextFormat *font,
                    bool centered = false) {
        brush->SetColor(color(c));
        font->SetTextAlignment(centered ? DWRITE_TEXT_ALIGNMENT_CENTER
                                        : DWRITE_TEXT_ALIGNMENT_LEADING);
        rt->DrawText(s.c_str(), static_cast<UINT32>(s.size()), font,
                     {r.left, r.top, r.right, r.bottom}, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    auto rounded = [&](Rect r, Color c, float radius = 10) {
        brush->SetColor(color(c));
        rt->FillRoundedRectangle(
            D2D1::RoundedRect({r.left, r.top, r.right, r.bottom}, radius, radius), brush);
    };
    auto divider = [&](float x, float top, float bottom) {
        fill({x, top, x + 1, bottom}, uiBorder());
    };
    auto panel = [&](Rect r, Color background, Color border) {
        rounded(r, background, 10);
        brush->SetColor(color(border));
        rt->DrawRoundedRectangle(D2D1::RoundedRect({r.left, r.top, r.right, r.bottom}, 10, 10),
                                 brush, 1);
    };
    fill({0, client.bottom - StatusHeight, client.right, client.bottom}, uiSurface());
    fill({0, client.bottom - StatusHeight, client.right, client.bottom - StatusHeight + 1},
         uiBorder());
    fill({0, 0, client.right, toolbarHeight()}, uiSurface());
    if (!app.fullScreen)
        for (int row = 0; row < 3; ++row)
        {
            const float top = rowTop(row), bottom = top + rowHeight(row);
            if (row == 2 || (app.collapsedRows & (1U << row)))
                fill({0, top, client.right, bottom}, uiRaised());
            fill({0, bottom - 1, client.right, bottom}, uiBorder());
            if (app.collapsedRows & (1U << row))
            {
                const wchar_t *labels[] = {L"Actions", L"Tools & shapes", L"Color & size"};
                text(labels[row], {20, top, client.right - 24, bottom}, Muted,
                     app.graphics.smallFont.get());
            }
        }
    // The capture icon and action are one button; history uses the space it frees.
    if (!app.fullScreen && !(app.collapsedRows & 1))
    {
        divider(180, 19, 39);
        if (hasImage() && client.right > 1100)
            text(std::to_wstring(app.image.width) + L" \u00D7 " + std::to_wstring(app.image.height),
                 {client.right - 514, 11, client.right - 386, 47}, Muted,
                 app.graphics.smallFont.get(), true);
    }
    const auto layout = toolbarLayout();
    const float toolTop = rowTop(1), formatTop = rowTop(2);
    if (!app.fullScreen && !(app.collapsedRows & 2))
    {
        text(L"DRAW", {layout.draw.left + 2, toolTop + 1, layout.draw.right, toolTop + 13}, Muted,
             app.graphics.labelFont.get());
        text(L"SHAPES", {layout.shapes.left + 2, toolTop + 1, layout.shapes.right, toolTop + 13},
             Muted, app.graphics.labelFont.get());
        divider((layout.draw.right + layout.shapes.left) / 2, toolTop + 17, toolTop + 57);
        panel(layout.draw, uiRaised(), uiBorder());
        panel(layout.shapes, uiRaised(), uiBorder());
    }
    if (!app.fullScreen && !(app.collapsedRows & 4))
    {
        text(L"Color", {22, formatTop + 5, 66, formatTop + 37}, Muted,
             app.graphics.smallFont.get());
        const float formattingLeft = layout.formatting.left;
        divider(formattingLeft - 10, formatTop + 11, formatTop + 31);
        if (app.erasing)
            text(L"Whole object",
                 {formattingLeft, formatTop + 5, formattingLeft + 160, formatTop + 37}, Muted,
                 app.graphics.smallFont.get(), true);
        else
        {
            text(textMode()        ? L"Size"
                 : highlightMode() ? L"Width"
                                   : L"Stroke",
                 {formattingLeft, formatTop + 5, formattingLeft + 45, formatTop + 37}, Muted,
                 app.graphics.smallFont.get());
            rounded({formattingLeft + 52, formatTop + 7, formattingLeft + 160, formatTop + 35},
                    uiSurface(), 8);
        }
        divider((layout.formatting.right + client.right - 140) / 2, formatTop + 11, formatTop + 31);
        rounded({client.right - 140, formatTop + 7, client.right - 24, formatTop + 35}, uiSurface(),
                8);
    }
    for (size_t index = 0; index < app.buttons.size(); ++index)
    {
        const auto &button = app.buttons[index];
        if (curvedArrowCommand(button.command) || recentPanelCommand(button.command) ||
            button.command == WelcomeCapture)
            continue; // These selection controls are painted above the image below.
        auto r = button.rect;
        const bool available = enabled(button.command);
        const bool styleMenu = button.command >= CircleStyleMenu && button.command <= LineStyleMenu;
        const int partner = button.command == CircleTool        ? CircleStyleMenu
                            : button.command == ArrowTool       ? ArrowStyleMenu
                            : button.command == CheckTool       ? CheckStyleMenu
                            : button.command == LineTool        ? LineStyleMenu
                            : button.command == CircleStyleMenu ? CircleTool
                            : button.command == ArrowStyleMenu  ? ArrowTool
                            : button.command == CheckStyleMenu  ? CheckTool
                            : button.command == LineStyleMenu   ? LineTool
                                                                : 0;
        const bool on = active(button.command) || (styleMenu && active(partner));
        const bool over = app.hover == button.command || (partner && app.hover == partner);
        const int pressed = app.pressed > 0 && app.pressed <= static_cast<int>(app.buttons.size())
                                ? app.buttons[app.pressed - 1].command
                                : 0;
        const bool down = over && (pressed == button.command || (partner && pressed == partner));
        if (paletteCommand(button.command))
        {
            Color value = app.palette[button.command - ColorFirst];
            bool chosen = activeColor() == value;
            float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            if (chosen || over)
            {
                brush->SetColor(color(chosen ? Accent : uiBorder()));
                rt->DrawEllipse(D2D1::Ellipse({cx, cy}, 12, 12), brush, chosen ? 2 : 1);
            }
            brush->SetColor(color(value));
            rt->FillEllipse(D2D1::Ellipse({cx, cy}, 8.5f, 8.5f), brush);
            if (value == rgb(255, 255, 255))
            {
                brush->SetColor(color(uiBorder()));
                rt->DrawEllipse(D2D1::Ellipse({cx, cy}, 8.5f, 8.5f), brush, 1);
            }
            continue;
        }
        // Copy success is communicated by the shared confirmation, not a sticky action tint.
        const bool toolButton = (button.command >= SelectTool && button.command <= LineTool) ||
                                button.command == HighlightTool || button.command == TextTool ||
                                button.command == EraserTool;
        const bool accentSelection = available && on &&
                                     (toolButton || styleMenu || button.command == CropTool ||
                                      button.command == Eyedropper || button.command == TextBold ||
                                      button.command == TextBox);
        const bool neutralSelection = available && on && !accentSelection;
        Color bg = accentSelection    ? uiSelected()
                   : neutralSelection  ? uiRaised()
                   : over && available ? mixColor(uiSurface(), Ink, .06f)
                                       : uiSurface();
        Color border = accentSelection    ? uiSelectedBorder()
                       : neutralSelection ? mixColor(uiBorder(), Ink, .25f)
                                          : uiBorder();
        if (down)
            bg = mixColor(bg, Ink, .08f);
        if (button.command == NewSnip)
            paintCaptureSurface(rt, brush, r, 8, over, down);
        else if (styleMenu || (button.command >= CircleTool && button.command <= LineTool))
        {
            // Split tools share one surface; their small chevron remains a separate hit target.
            Rect whole = styleMenu ? Rect{r.left - 84, r.top, r.right, r.bottom}
                                   : Rect{r.left, r.top, r.right + 24, r.bottom};
            rt->PushAxisAlignedClip({r.left, r.top, r.right, r.bottom},
                                    D2D1_ANTIALIAS_MODE_ALIASED);
            rounded(whole, bg, 8);
            brush->SetColor(color(border));
            rt->DrawRoundedRectangle(D2D1::RoundedRect({whole.left + .5f, whole.top + .5f,
                                                        whole.right - .5f, whole.bottom - .5f},
                                                       8, 8),
                                     brush, 1);
            rt->PopAxisAlignedClip();
        }
        else
        {
            rounded(r, bg, 8);
            if (button.command != NewSnip &&
                !(button.command >= ToggleActions && button.command <= ToggleFormatting))
            {
                brush->SetColor(color(border));
                rt->DrawRoundedRectangle(
                    D2D1::RoundedRect({r.left + .5f, r.top + .5f, r.right - .5f, r.bottom - .5f}, 8,
                                      8),
                    brush, 1);
            }
        }
        Color fg = !available                  ? Muted
                   : button.command == NewSnip ? uiCaptureText()
                   : accentSelection           ? uiAccentText()
                                               : Ink;
        if (button.command >= ToggleActions && button.command <= ToggleFormatting)
        {
            brush->SetColor(color(over ? Accent : Muted));
            const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            const float direction =
                (app.collapsedRows & (1U << (button.command - ToggleActions))) ? 1 : -1;
            rt->DrawLine({cx - 4, cy - direction * 2}, {cx, cy + direction * 2}, brush, 1.6f);
            rt->DrawLine({cx, cy + direction * 2}, {cx + 4, cy - direction * 2}, brush, 1.6f);
        }
        else if (button.command == FullScreen && !app.fullScreen)
        {
            brush->SetColor(color(fg));
            const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            for (float sx : {-1.0f, 1.0f})
                for (float sy : {-1.0f, 1.0f})
                {
                    rt->DrawLine({cx + sx * 3, cy + sy * 7}, {cx + sx * 7, cy + sy * 7}, brush,
                                 1.5f);
                    rt->DrawLine({cx + sx * 7, cy + sy * 7}, {cx + sx * 7, cy + sy * 3}, brush,
                                 1.5f);
                }
        }
        else if (styleMenu)
        {
            brush->SetColor(color(fg));
            float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            rt->DrawLine({cx - 3, cy - 1}, {cx, cy + 2}, brush, 1.3f);
            rt->DrawLine({cx, cy + 2}, {cx + 3, cy - 1}, brush, 1.3f);
        }
        else if (button.command >= CircleTool && button.command <= LineTool)
        {
            Annotation icon;
            icon.kind = button.command == CircleTool
                            ? app.geometryTool
                            : static_cast<Tool>(button.command - SelectTool);
            icon.color = fg;
            icon.thickness = 1.7f;
            icon.style = app.styles[static_cast<size_t>(icon.kind)];
            icon.a = {r.left + 9, r.top + 10};
            icon.b = {r.left + 25, r.top + 26};
            if (icon.kind == Tool::Arrow || icon.kind == Tool::Line)
            {
                icon.a.y = r.top + 25;
                icon.b.y = r.top + 11;
            }
            app.graphics.drawAnnotations(rt, {icon});
            text(button.label, {r.left + 31, r.top, r.right, r.bottom}, fg,
                 app.graphics.font.get());
        }
        else if (button.command == NewSnip || button.command == Copy || button.command == Save ||
                 button.command == SaveAs ||
                 button.command == SelectTool || button.command == PenTool ||
                 button.command == TextTool || button.command == HighlightTool ||
                 button.command == EraserTool)
        {
            float inset = button.command == NewSnip ? 12 : 9;
            drawUIIcon(rt, brush, button.command, {r.left + inset, (r.top + r.bottom) / 2 - 10},
                       fg);
            text(button.label, {r.left + inset + 27, r.top, r.right - 4, r.bottom}, fg,
                 app.graphics.font.get());
        }
        else if (button.command == RecentSnips)
        {
            drawUIIcon(rt, brush, RecentSnips, {r.left + 8, (r.top + r.bottom) / 2 - 10}, fg);
            text(L"Recent", {r.left + 32, r.top, r.right - 26, r.bottom}, fg,
                 app.graphics.smallFont.get());
            rounded({r.right - 23, r.top + 7, r.right - 5, r.bottom - 7}, uiRaised(), 4);
            text(std::to_wstring(app.recent.size()), {r.right - 23, r.top, r.right - 5, r.bottom},
                 Muted, app.graphics.smallFont.get(), true);
        }
        else if (button.command == Undo || button.command == Redo ||
                 button.command == CustomColor || button.command == Eyedropper)
            drawUIIcon(rt, brush, button.command,
                       {(r.left + r.right) / 2 - 10, (r.top + r.bottom) / 2 - 10}, fg);
        else if (button.command == TextSizeMenu)
        {
            const float size =
                selected() && app.document.items[app.document.selected].kind == Tool::Text
                    ? app.document.items[app.document.selected].fontSize
                    : app.fontSize;
            text(std::to_wstring(static_cast<int>(size)), r, fg, app.graphics.smallFont.get(),
                 true);
        }
        else
            text(button.label, r, fg, app.graphics.font.get(), true);
    }
    auto minus = std::find_if(app.buttons.begin(), app.buttons.end(),
                              [](const Button &b) { return b.command == SizeDown; });
    float size = selected() && app.document.items[app.document.selected].kind != Tool::Check
                     ? app.document.items[app.document.selected].thickness
                     : brushWidth();
    if (minus != app.buttons.end() && !textMode())
        text(std::to_wstring(static_cast<int>(size)) + L" px",
             {minus->rect.right + 2, minus->rect.top, minus->rect.right + 52, minus->rect.bottom},
             Ink, app.graphics.smallFont.get(), true);
    std::wstring message = app.status;
    if (message.empty())
    {
        if (hasImage())
        {
            const wchar_t *hints[] = {
                L"Select: drag image to pan; drag annotations to move; handles resize",
                L"Pen: drag to draw; Ctrl+Z undoes",
                L"Circle: drag to draw; Shift makes a circle",
                L"Arrow: drag to draw; select and drag endpoints to turn",
                L"Check / X: click to place; drag to size",
                L"Line: drag to draw; Shift snaps angle; drag endpoints to resize",
                L"Rectangle: drag to draw; Shift makes a square",
                L"Text: click and type; Ctrl+Enter finishes; double-click to edit",
                (L"Highlight: drag with the chisel brush; change color or width below; Ctrl+Z "
                 L"undoes")};
            message =
                app.cropping
                    ? L"Crop: drag the area to keep; release to crop; Esc cancels; Ctrl+Z restores"
                : app.pickingColor ? L"Eyedropper: click the image to pick a color; Esc cancels"
                : app.erasing  ? L"Eraser: click or drag to delete whole annotations; Ctrl+Z undoes"
                : app.textEdit ? L"Text: Ctrl+Enter finishes; Enter adds a line; Esc cancels"
                : curvedArrowSelected() ? L"Curved arrow: Flip changes the bend; Ctrl+Z undoes"
                                        : hints[static_cast<int>(app.tool)];
        }
        else
            message = L"Ready when you are";
    }
    brush->SetColor(color(Muted));
    if (!app.fullScreen)
        rt->FillEllipse(D2D1::Ellipse({22, client.bottom - StatusHeight / 2}, 3, 3), brush);
    text(message,
         {app.fullScreen ? 402.0f : 34.0f, client.bottom - StatusHeight,
          client.right - (hasImage() ? 292 : 14), client.bottom},
         Muted, app.graphics.smallFont.get());
    if (hasImage())
        text(std::to_wstring(static_cast<int>(std::round(app.view.scale * app.dpi * 100))) +
                 L"%   \u00B7   " + std::to_wstring(app.image.width) + L" \u00D7 " +
                 std::to_wstring(app.image.height),
             {client.right - 190, client.bottom - StatusHeight, client.right - 12, client.bottom},
             Muted, app.graphics.smallFont.get(), true);
}
