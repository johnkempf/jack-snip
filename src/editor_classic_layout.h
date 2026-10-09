// Original ribbon geometry and controls, sharing the same editor commands.
struct ToolbarLayout
{
    Rect draw, shapes, formatting;
};
ToolbarLayout toolbarLayout()
{
    const float width = clientDips().right;
    const float formattingWidth = textMode() ? 264 : 160;
    // Center the whole label/control group between the eyedropper and zoom controls.
    const float paletteRight =
        70 + 34 * std::min(static_cast<int>(app.palette.size()) + 2, paletteColumns());
    const float formattingLeft = (paletteRight + width - 140 - formattingWidth) / 2;
    return {{20, rowTop(1) + 15, width >= 980 ? 466.0f : 318.0f, rowTop(1) + 59},
            {width - 504, rowTop(1) + 15, width - 20, rowTop(1) + 59},
            {formattingLeft, rowTop(2) + 7, formattingLeft + formattingWidth, rowTop(2) + 35}};
}
void buildClassicButtons()
{
    app.buttons.clear();
    const auto layout = toolbarLayout();
    float x = 20;
    auto add = [&](int id, const wchar_t *text, float width, float y, float height = 36) {
        app.buttons.push_back({{x, y, x + width, y + height}, id, text});
        x += width + 4;
    };
    auto addStyleTool = [&](int id, const wchar_t *text, int menu) {
        add(id, text, 84, rowTop(1) + 19);
        x -= 4;
        add(menu, L"", 24, rowTop(1) + 19);
        x += 8;
    };
    if (!app.fullScreen && !(app.collapsedRows & 1))
    {
        add(NewSnip, L"New snip", 142, 11);
        x = 194;
        add(Undo, L"", 36, 11);
        x += 8;
        add(Redo, L"", 36, 11);
        x = 296;
        add(FullScreen, L"", 36, 11);
        x = 344;
        add(CropTool, L"Crop", 82, 11);
        x = clientDips().right - 374;
        add(RecentSnips, L"Recent", 114, 11);
        x = clientDips().right - 250;
        add(Copy, L"Copy", 114, 11);
        x += 6;
        add(SaveAs, L"Save as", 106, 11);
    }
    if (!app.fullScreen && !(app.collapsedRows & 2))
    {
        x = layout.draw.left + 8;
        const bool wide = clientDips().right >= 980;
        add(SelectTool, L"Select", wide ? 86 : 78, rowTop(1) + 19);
        x += wide ? 8 : 4;
        add(PenTool, L"Pen", wide ? 74 : 64, rowTop(1) + 19);
        x += wide ? 8 : 4;
        add(HighlightTool, wide ? L"Highlight" : L"", wide ? 108 : 36, rowTop(1) + 19);
        x += wide ? 8 : 4;
        add(TextTool, wide ? L"Text" : L"", wide ? 74 : 36, rowTop(1) + 19);
        x += wide ? 8 : 4;
        add(EraserTool, L"", 36, rowTop(1) + 19);
        x = layout.shapes.left + 8;
        addStyleTool(CircleTool, L"Shapes", CircleStyleMenu);
        addStyleTool(ArrowTool, L"Arrow", ArrowStyleMenu);
        addStyleTool(CheckTool, L"Check", CheckStyleMenu);
        addStyleTool(LineTool, L"Line", LineStyleMenu);
    }
    if (!app.fullScreen && !(app.collapsedRows & 4))
    {
        const float y = rowTop(2) + 7;
        const int columns = paletteColumns();
        for (int i = 0; i < static_cast<int>(app.palette.size()) + 2; ++i)
        {
            x = 70 + (i % columns) * 34.0f;
            const float top = y + (i / columns) * 34.0f;
            if (i < static_cast<int>(app.palette.size()))
                app.buttons.push_back({{x, top + 2, x + 24, top + 26}, ColorFirst + i, L""});
            else
                add(i == static_cast<int>(app.palette.size()) ? CustomColor : Eyedropper, L"", 28,
                    top, 28);
        }
        if (!app.erasing)
        {
            x = layout.formatting.left + 52;
            add(SizeDown, L"\u2212", 28, y, 28);
            x += 48;
            add(SizeUp, L"+", 28, y, 28);
        }
        if (textMode())
        {
            x = layout.formatting.left + 82;
            add(TextSizeMenu, L"", 46, y, 28);
            x = layout.formatting.left + 170;
            add(TextBold, L"B", 28, y, 28);
            x = layout.formatting.left + 204;
            add(TextBox, L"Box", 60, y, 28);
        }
        x = clientDips().right - 140;
        add(Fit, L"Fit", 48, y, 28);
        add(Actual, L"100%", 64, y, 28);
    }
    if (!app.fullScreen)
        for (int row = 0; row < 3; ++row)
        {
            x = clientDips().right - 19;
            const float height = std::min(24.0f, rowHeight(row) - 2);
            add(ToggleActions + row, L"", 18, rowTop(row) + (rowHeight(row) - height) / 2, height);
        }
    else
    {
        x = 12;
        const float y = clientDips().bottom - StatusHeight + 4;
        add(FullScreen, L"Exit full screen", 130, y, 24);
        add(Fit, L"Fit", 48, y, 24);
        add(Actual, L"100%", 64, y, 24);
        add(RecentSnips, L"Recent", 114, y, 24);
    }

    if (!hasImage())
    {
        auto canvas = canvasRect();
        float middle = canvas.top + canvas.height() / 2;
        x = clientDips().right / 2 - 76;
        add(NewSnip, L"Take a snip", 152, middle + 30, 42);
    }
}
