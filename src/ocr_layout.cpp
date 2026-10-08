#include "ocr_layout.h"
#include <array>
#include <cstring>

namespace snip
{
TextLayout analyzeTextRows(const Bitmap &image, std::stop_token stop, bool coreOnly)
{
    TextLayout result;
    if (image.empty() || stop.stop_requested())
        return result;
    const int width = image.width, height = image.height;
    std::vector<std::array<uint8_t, 3>> backgrounds(height);
    std::vector<uint8_t> mask(static_cast<size_t>(width) * height);
    int flatRows = 0;
    auto bucket = [](const uint8_t *p) {
        return (p[0] >> 4) | ((p[1] >> 4) << 4) | ((p[2] >> 4) << 8);
    };
    for (int y = 0; y < height; ++y)
    {
        if (stop.stop_requested())
            return {};
        std::array<int, 4096> counts{}, votes{};
        const auto row = &image.pixels[static_cast<size_t>(y) * width * 4];
        for (int x = 0; x < width; ++x)
        {
            const int bin = bucket(&row[x * 4]);
            ++counts[bin];
            votes[bin] += 1 + (x < 2 || x >= width - 2 ? std::max(1, width / 4) : 0);
        }
        // Prefer the panel color at the edges, but a thin field/window border
        // cannot be the background of the whole row.
        for (size_t bin = 0; bin < votes.size(); ++bin)
            if (counts[bin] < std::max(4, width / 8))
                votes[bin] = counts[bin];
        int bg = static_cast<int>(std::max_element(votes.begin(), votes.end()) - votes.begin());
        // Dense strokes can outnumber a white panel locally. Keep a light edge
        // background, but do not let a minority dark stroke win just because it
        // touches the selection edge (especially a partially selected digit).
        if (coreOnly && counts[bg] < std::max(1, width / 4) &&
            ((bg & 15) < 14 || ((bg >> 4) & 15) < 14 || (bg >> 8) < 14))
            bg = static_cast<int>(std::max_element(counts.begin(), counts.end()) - counts.begin());
        flatRows += counts[bg] >= std::max(1, width / 4);
        std::array<unsigned, 3> sum{};
        for (int x = 0; x < width; ++x)
            if (bucket(&row[x * 4]) == bg)
                for (int c = 0; c < 3; ++c)
                    sum[c] += row[x * 4 + c];
        for (int c = 0; c < 3; ++c)
            backgrounds[y][c] = static_cast<uint8_t>(sum[c] / counts[bg]);
        for (int x = 0; x < width; ++x)
        {
            int difference = 0;
            for (int c = 0; c < 3; ++c)
                difference = std::max(difference, std::abs(row[x * 4 + c] - backgrounds[y][c]));
            if (coreOnly)
                difference =
                    std::abs((row[x * 4 + 2] * 299 + row[x * 4 + 1] * 587 + row[x * 4] * 114) -
                             (backgrounds[y][2] * 299 + backgrounds[y][1] * 587 +
                              backgrounds[y][0] * 114)) /
                    1000;
            mask[static_cast<size_t>(y) * width + x] = difference > (coreOnly ? 128 : 45);
        }
    }
    // Photographs and textured regions do not offer a reliable background for
    // this conservative containment check; let the general OCR handle them.
    result.image = image;
    if (flatRows < height * .8)
        return result;
    result.reliable = true;
    // A one-pixel alternating focus outline is UI chrome, not a text row.
    // Remove only isolated, near-full-width dotted rules. Keeping neighboring
    // rows intact preserves actual clipped letters and barcode strokes.
    std::vector<uint8_t> dottedRows(height);
    for (int y = 0; y < height && width >= 16; ++y)
    {
        const auto *row = &mask[static_cast<size_t>(y) * width];
        int first = width, last = -1, runs = 0, longest = 0, length = 0;
        for (int x = 0; x < width; ++x)
        {
            if (row[x])
            {
                first = std::min(first, x);
                last = x;
                runs += x == 0 || !row[x - 1];
            }
            length = x && row[x] == row[x - 1] ? length + 1 : 1;
            longest = std::max(longest, length);
        }
        dottedRows[y] = first <= 2 && last >= width - 3 && runs >= width / 3 && longest <= 2;
    }
    for (int y = 0; y < height; ++y)
        if (dottedRows[y] && (y == 0 || !dottedRows[y - 1]) &&
            (y + 1 == height || !dottedRows[y + 1]))
            for (int x = 0; x < width; ++x)
            {
                const auto index = static_cast<size_t>(y) * width + x;
                mask[index] = 0;
                std::memcpy(&result.image.pixels[index * 4], backgrounds[y].data(), 3);
            }
    std::vector<uint8_t> ink(height), clipped(height);
    std::vector<size_t> component;
    struct Glyph
    {
        int left, right, top, bottom, width, height;
        bool cut, discard = false;
        std::vector<size_t> edgePixels;
    };
    std::vector<Glyph> glyphs;
    for (size_t start = 0; start < mask.size(); ++start)
    {
        if (mask[start] != 1)
            continue;
        if (stop.stop_requested())
            return {};
        component.clear();
        component.push_back(start);
        mask[start] = 2;
        int left = width, right = 0, top = height, bottom = 0;
        for (size_t i = 0; i < component.size(); ++i)
        {
            const int x = static_cast<int>(component[i] % width),
                      y = static_cast<int>(component[i] / width);
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                        continue;
                    const auto next = static_cast<size_t>(ny) * width + nx;
                    if (mask[next] == 1)
                    {
                        mask[next] = 2;
                        component.push_back(next);
                    }
                }
        }
        const int w = right - left + 1, h = bottom - top + 1;
        const bool rule = (w <= 3 && h >= 12 && h > w * 6 && (left <= 2 || right >= width - 3) &&
                           (top == 0 || bottom == height - 1)) ||
                          (h <= 3 && w >= width * .85 && w > h * 6) ||
                          (coreOnly && h <= 3 && w >= width * .5 && w > h * 20);
        if (rule)
        {
            for (const auto index : component)
            {
                const auto &bg = backgrounds[index / width];
                std::memcpy(&result.image.pixels[index * 4], bg.data(), 3);
            }
            continue;
        }
        const bool cut = left == 0 || right == width - 1 || top == 0 || bottom == height - 1;
        glyphs.push_back({left, right, top, bottom, w, h, cut, false, {}});
        if (cut || (coreOnly && w <= 3 && h <= 3 && (left <= 2 || right >= width - 3)))
            glyphs.back().edgePixels = component;
    }
    // A selection can include a field border, scrollbar/icon, or a fragment of
    // an unrelated neighboring row. Do not let those join otherwise complete
    // letters into one clipped band. A cut letter aligned with the text still
    // invalidates its row, so we never silently copy a truncated identifier.
    for (auto &glyph : glyphs)
    {
        if (stop.stop_requested())
            return {};
        const Glyph *reference = nullptr;
        const bool edgeFragment = coreOnly && glyph.width <= 3 && glyph.height <= 3 &&
                                  (glyph.left <= 2 || glyph.right >= width - 3);
        if ((glyph.cut && (glyph.left == 0 || glyph.right == width - 1)) || edgeFragment)
            for (const auto &neighbor : glyphs)
            {
                if (neighbor.cut || neighbor.height < 6)
                    continue;
                const int separation =
                    std::max(neighbor.top - glyph.bottom, glyph.top - neighbor.bottom);
                if (separation > (edgeFragment ? std::max(2.0, neighbor.height * .25) : 0.0))
                    continue;
                if (!reference || neighbor.height > reference->height ||
                    (neighbor.height == reference->height &&
                     std::abs(neighbor.left - glyph.left) < std::abs(reference->left - glyph.left)))
                    reference = &neighbor;
            }
        if (reference)
        {
            const auto &neighbor = *reference;
            const int gap =
                std::max(neighbor.left - glyph.right - 1, glyph.left - neighbor.right - 1);
            const bool isolatedSpeck =
                glyph.width <= 2 && glyph.height <= 2 && gap >= neighbor.height;
            const int overhang = std::max(neighbor.top - glyph.top, glyph.bottom - neighbor.bottom);
            const bool differentRow =
                glyph.height > neighbor.height + std::max(3.0, neighbor.height * .2) &&
                overhang > std::max(4.0, neighbor.height * .3);
            const int overlap =
                std::min(glyph.bottom, neighbor.bottom) - std::max(glyph.top, neighbor.top) + 1;
            const bool adjacentFragment = glyph.height < neighbor.height * .7 &&
                                          overlap <= neighbor.height * .2 &&
                                          (!edgeFragment || glyph.bottom < neighbor.bottom - 2);
            if (isolatedSpeck || differentRow || adjacentFragment)
                glyph.discard = true;
        }
        if (glyph.discard)
        {
            result.removedEdgeFragment |= edgeFragment;
            for (const auto index : glyph.edgePixels)
                std::memcpy(&result.image.pixels[index * 4], backgrounds[index / width].data(), 3);
            continue;
        }
        for (int y = glyph.top; y <= glyph.bottom; ++y)
        {
            ink[y] = 1;
            clipped[y] |= glyph.cut;
        }
    }
    for (int y = 0; y < height;)
    {
        if (!ink[y])
        {
            ++y;
            continue;
        }
        TextRow row{y, y + 1, false};
        int lastInk = y;
        for (; y < height && y - lastInk <= 2; ++y)
            if (ink[y])
            {
                lastInk = y;
                row.bottom = y + 1;
                row.clipped |= clipped[y] != 0;
            }
        if (row.clipped)
            for (int cy = std::max(0, row.top - 1); cy < std::min(height, row.bottom + 1); ++cy)
                for (int x = 0; x < width; ++x)
                    std::memcpy(&result.image.pixels[(static_cast<size_t>(cy) * width + x) * 4],
                                backgrounds[cy].data(), 3);
        // Preserve a bounded uniform panel surrounding the row. Different row
        // heights/selection margins must not change quote or hyphen recognition.
        const auto &bg = backgrounds[(row.top + row.bottom - 1) / 2];
        row.glyphHeight = row.bottom - row.top;
        row.inkTop = row.top;
        row.inkBottom = row.bottom;
        row.left = width;
        std::vector<std::pair<int, int>> spans;
        for (const auto &glyph : glyphs)
            if (!glyph.discard && glyph.top >= row.top && glyph.bottom < row.bottom)
            {
                row.left = std::min(row.left, glyph.left);
                row.right = std::max(row.right, glyph.right + 1);
                // Strong strokes exclude their one-pixel antialiased fringe.
                // Count that coverage when distinguishing word spacing from
                // the normal gap around a narrow digit such as 1.
                spans.emplace_back(glyph.left - (coreOnly ? 1 : 0),
                                   glyph.right + 1 + (coreOnly ? 1 : 0));
            }
        std::sort(spans.begin(), spans.end());
        int end = row.left, largestGap = 0;
        for (auto [left, right] : spans)
        {
            largestGap = std::max(largestGap, left - end);
            end = std::max(end, right);
        }
        // Keep word/quote spacing in natural text. Normalize tightly spaced
        // labels and identifiers, where changing margins can drop a hyphen.
        row.compact = largestGap <= row.glyphHeight * .25;
        // Tall condensed fonts can look like separated characters or O/l to
        // Windows OCR. Measure actual glyph proportions, independent of text.
        std::vector<double> proportions;
        for (const auto &glyph : glyphs)
            if (!glyph.discard && glyph.top >= row.top && glyph.bottom < row.bottom &&
                glyph.height >= row.glyphHeight * .7 && glyph.width <= glyph.height * 1.5)
                proportions.push_back(static_cast<double>(glyph.width) / glyph.height);
        if (row.glyphHeight >= 16 && proportions.size() >= 4)
        {
            std::sort(proportions.begin(), proportions.end());
            row.condensed = proportions[proportions.size() / 2] < .55;
            row.veryCondensed = coreOnly && proportions[proportions.size() / 2] < .45;
        }
        const int first = std::max(0, row.top - 16), last = std::min(height, row.bottom + 16);
        auto sameBackground = [&](int cy) {
            for (int c = 0; c < 3; ++c)
                if (std::abs(bg[c] - backgrounds[cy][c]) > 16)
                    return false;
            return true;
        };
        while (row.top > first && !ink[row.top - 1] && sameBackground(row.top - 1))
            --row.top;
        while (row.bottom < last && !ink[row.bottom] && sameBackground(row.bottom))
            ++row.bottom;
        result.rows.push_back(row);
    }
    return result;
}
} // namespace snip
