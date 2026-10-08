#include "ocr_layout.h"
#include <array>
#include <cstring>

namespace snip
{
TextLayout analyzeTextRows(const Bitmap &image, std::stop_token stop)
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
        const int bg =
            static_cast<int>(std::max_element(votes.begin(), votes.end()) - votes.begin());
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
            mask[static_cast<size_t>(y) * width + x] = difference > 45;
        }
    }
    // Photographs and textured regions do not offer a reliable background for
    // this conservative containment check; let the general OCR handle them.
    result.image = image;
    if (flatRows < height * .8)
        return result;
    result.reliable = true;
    std::vector<uint8_t> ink(height), clipped(height);
    std::vector<size_t> component;
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
                          (h <= 3 && w >= width * .85 && w > h * 6);
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
        for (int y = top; y <= bottom; ++y)
        {
            ink[y] = 1;
            clipped[y] |= cut;
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
