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
    bool compact = false;
    bool veryCondensed = false;
    int left = 0, right = 0, inkTop = 0, inkBottom = 0;
};
struct TextLayout
{
    Bitmap image;
    std::vector<TextRow> rows;
    bool reliable = false;
    bool removedEdgeFragment = false;
};
// Locate complete rows on flat UI/document backgrounds. Remove table rules and
// clipped rows before OCR can invent text from their remaining letter fragments.
TextLayout analyzeTextRows(const Bitmap &image, std::stop_token stop, bool coreOnly = false);
} // namespace snip
