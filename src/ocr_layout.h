#pragma once
#include "model.h"
#include <stop_token>

namespace snip
{
struct TextRow
{
    int top = 0, bottom = 0;
    bool clipped = false;
    int glyphHeight = 0;
    bool condensed = false;
};
struct TextLayout
{
    Bitmap image;
    std::vector<TextRow> rows;
    bool reliable = false;
};
// Locate complete rows on flat UI/document backgrounds. Remove table rules and
// clipped rows before OCR can invent text from their remaining letter fragments.
TextLayout analyzeTextRows(const Bitmap &image, std::stop_token stop);
} // namespace snip
