#include "graphics.h"
#include "file_io.h"
#include "test_hooks.h"
#include <objbase.h>
#include <cstring>
#include <stdexcept>
#include <array>

namespace snip
{
D2D1_COLOR_F color(Color c, float alpha)
{
    return D2D1::ColorF((c & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, ((c >> 16) & 255) / 255.0f,
                        alpha);
}
void Graphics::initialize()
{
    if (factory)
        return;
    Graphics ready;
    check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, ready.factory.put()),
          "Cannot initialize Direct2D.");
#ifdef TIGER_SNIP_TESTING
    if (testing::graphicsCheckpoint)
        testing::graphicsCheckpoint(1);
#endif
    check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                              reinterpret_cast<IUnknown **>(ready.textFactory.put())),
          "Cannot initialize text rendering.");
    Com<IDWriteFontCollection> fonts;
    check(ready.textFactory->GetSystemFontCollection(fonts.put()), "Cannot find annotation fonts.");
    UINT fontIndex = 0;
    BOOL hasBahnschrift = FALSE;
    check(fonts->FindFamilyName(L"Bahnschrift", &fontIndex, &hasBahnschrift),
          "Cannot find annotation typeface.");
    if (hasBahnschrift)
        ready.annotationFontFamily = L"Bahnschrift";
    auto makeFont = [&](Com<IDWriteTextFormat> &f, float size, DWRITE_FONT_WEIGHT weight) {
        check(ready.textFactory->CreateTextFormat(
                  L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                  DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", f.put()),
              "Cannot create UI font.");
        check(f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER), "Cannot align UI font.");
        check(f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "Cannot configure UI font.");
    };
    makeFont(ready.font, 13, DWRITE_FONT_WEIGHT_MEDIUM);
    makeFont(ready.smallFont, 12, DWRITE_FONT_WEIGHT_NORMAL);
    makeFont(ready.titleFont, 28, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    makeFont(ready.labelFont, 10, DWRITE_FONT_WEIGHT_SEMI_BOLD);
#ifdef TIGER_SNIP_TESTING
    if (testing::graphicsCheckpoint)
        testing::graphicsCheckpoint(2);
#endif
    check(ready.factory->CreateStrokeStyle(
              D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                          D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
              nullptr, 0, ready.roundStroke.put()),
          "Cannot initialize brush strokes.");
    auto dashed = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                              D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND, 10.0f,
                                              D2D1_DASH_STYLE_DASH);
    check(ready.factory->CreateStrokeStyle(dashed, nullptr, 0, ready.dashStroke.put()),
          "Cannot initialize dashed strokes.");
    dashed.dashStyle = D2D1_DASH_STYLE_DOT;
    check(ready.factory->CreateStrokeStyle(dashed, nullptr, 0, ready.dotStroke.put()),
          "Cannot initialize dotted strokes.");
#ifdef TIGER_SNIP_TESTING
    if (testing::graphicsCheckpoint)
        testing::graphicsCheckpoint(3);
#endif
    // Publish only a complete resource set. Failure leaves this instance retryable.
    factory = std::move(ready.factory);
    textFactory = std::move(ready.textFactory);
    font = std::move(ready.font);
    smallFont = std::move(ready.smallFont);
    titleFont = std::move(ready.titleFont);
    labelFont = std::move(ready.labelFont);
    roundStroke = std::move(ready.roundStroke);
    dashStroke = std::move(ready.dashStroke);
    dotStroke = std::move(ready.dotStroke);
    annotationFontFamily.swap(ready.annotationFontFamily);
}
Color textBackground(Color foreground)
{
    const unsigned brightness = (foreground & 255) * 213 + ((foreground >> 8) & 255) * 715 +
                                ((foreground >> 16) & 255) * 72;
    return brightness > 170000 ? rgb(35, 39, 56) : rgb(255, 255, 255);
}
Com<IDWriteTextLayout> Graphics::textLayout(const Annotation &item)
{
    initialize();
    Com<IDWriteTextFormat> format;
    check(textFactory->CreateTextFormat(annotationFontFamily.c_str(), nullptr,
                                        item.bold ? DWRITE_FONT_WEIGHT_BOLD
                                                  : DWRITE_FONT_WEIGHT_NORMAL,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                        item.fontSize, L"en-us", format.put()),
          "Cannot create annotation font.");
    Com<IDWriteTextLayout> layout;
    const std::wstring content = item.text.empty() ? L" " : item.text;
    check(textFactory->CreateTextLayout(content.c_str(), static_cast<UINT32>(content.size()),
                                        format.get(), std::max(1.0f, item.textWidth), 16384,
                                        layout.put()),
          "Cannot lay out annotation text.");
    return layout;
}
void Graphics::measureText(Annotation &item)
{
    auto layout = textLayout(item);
    DWRITE_TEXT_METRICS metrics{};
    check(layout->GetMetrics(&metrics), "Cannot measure annotation text.");
    const float padding = item.boxed ? 12 : 0;
    const float width = std::max(
        {1.0f, metrics.widthIncludingTrailingWhitespace, item.textFrame ? item.textWidth : 0.0f});
    item.b = item.a +
             Point{width + padding * 2, std::max(metrics.height, item.textHeight) + padding * 2};
}
void Graphics::drawAnnotations(ID2D1RenderTarget *rt, const std::vector<Annotation> &items,
                               int editingText)
{
    Com<ID2D1SolidColorBrush> brush;
    check(rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), brush.put()),
          "Cannot create drawing brush.");
    Com<ID2D1Layer> opacityLayer;
    for (size_t index = 0; index < items.size(); ++index)
    {
        const auto &item = items[index];
        const float opacity = std::clamp(item.opacity, 0.0f, 1.0f);
        if (opacity < 1)
        {
            if (!opacityLayer)
                check(rt->CreateLayer(opacityLayer.put()),
                      "Cannot create annotation opacity layer.");
            // Composite the entire annotation once so crossings, arrow outlines and boxed
            // text keep their appearance as opacity changes. Existing highlight alpha remains.
            rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                                                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                D2D1::Matrix3x2F::Identity(), opacity),
                          opacityLayer.get());
        }
        brush->SetColor(color(item.color));
        float width = item.thickness;
        auto r = item.bounds();
        auto line = [&](Point a, Point b, float w, ID2D1StrokeStyle *stroke = nullptr) {
            rt->DrawLine({a.x, a.y}, {b.x, b.y}, brush.get(), w,
                         stroke ? stroke : roundStroke.get());
        };
        switch (item.kind)
        {
        case Tool::Text: {
            if (item.text.empty() && static_cast<int>(index) != editingText)
                break;
            const float padding = item.boxed ? 12 : 0;
            if (item.boxed)
            {
                auto box = D2D1::RoundedRect({r.left, r.top, r.right, r.bottom}, 10, 10);
                brush->SetColor(color(textBackground(item.color)));
                rt->FillRoundedRectangle(box, brush.get());
                brush->SetColor(color(item.color));
                rt->DrawRoundedRectangle(box, brush.get(), 2);
            }
            if (static_cast<int>(index) == editingText)
                break; // The native inline editor supplies the glyphs and caret.
            auto layout = textLayout(item);
            rt->DrawTextLayout({item.a.x + padding, item.a.y + padding}, layout.get(), brush.get());
            break;
        }
        case Tool::Rectangle: {
            const float radius =
                item.style == 0 ? 0 : std::min({12.0f, r.width() / 4, r.height() / 4});
            auto box = D2D1::RoundedRect({r.left, r.top, r.right, r.bottom}, radius, radius);
            if (item.style >= 2)
            {
                brush->SetColor(color(item.color, item.style == 2 ? .18f : 1.0f));
                rt->FillRoundedRectangle(box, brush.get());
                brush->SetColor(color(item.color));
            }
            rt->DrawRoundedRectangle(box, brush.get(), width, roundStroke.get());
            break;
        }
        case Tool::Highlight: {
            if (item.points.empty())
                break;
            Com<ID2D1PathGeometry> path;
            Com<ID2D1GeometrySink> sink;
            check(factory->CreatePathGeometry(path.put()), "Cannot create highlight path.");
            check(path->Open(sink.put()), "Cannot draw highlight path.");
            sink->SetFillMode(D2D1_FILL_MODE_WINDING);
            for (size_t i = 0; i < item.points.size(); ++i)
            {
                const auto hull = chiselSegment(item.points[i ? i - 1 : 0], item.points[i], width);
                sink->BeginFigure({hull[0].x, hull[0].y}, D2D1_FIGURE_BEGIN_FILLED);
                for (size_t j = 1; j < hull.size(); ++j)
                    sink->AddLine({hull[j].x, hull[j].y});
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            }
            check(sink->Close(), "Cannot finish highlight path.");
            // Fill the union once: one stroke stays uniformly translucent at joins/crossings.
            brush->SetColor(color(item.color, .35f));
            rt->FillGeometry(path.get(), brush.get());
            break;
        }
        case Tool::Pen:
            if (item.points.size() == 1)
                rt->FillEllipse(
                    D2D1::Ellipse({item.points[0].x, item.points[0].y}, width / 2, width / 2),
                    brush.get());
            else if (item.points.size() > 1)
            {
                Com<ID2D1PathGeometry> path;
                Com<ID2D1GeometrySink> sink;
                check(factory->CreatePathGeometry(path.put()), "Cannot create pen path.");
                check(path->Open(sink.put()), "Cannot draw pen path.");
                sink->BeginFigure({item.points[0].x, item.points[0].y}, D2D1_FIGURE_BEGIN_HOLLOW);
                for (size_t i = 1; i < item.points.size(); ++i)
                    sink->AddLine({item.points[i].x, item.points[i].y});
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                check(sink->Close(), "Cannot finish pen path.");
                rt->DrawGeometry(path.get(), brush.get(), width, roundStroke.get());
            }
            break;
        case Tool::Circle: {
            auto ellipse = D2D1::Ellipse({(r.left + r.right) / 2, (r.top + r.bottom) / 2},
                                         r.width() / 2, r.height() / 2);
            if (item.style == 1)
            {
                brush->SetColor(color(item.color, .18f));
                rt->FillEllipse(ellipse, brush.get());
                brush->SetColor(color(item.color));
                rt->DrawEllipse(ellipse, brush.get(), width * 1.2f, roundStroke.get());
            }
            else
                rt->DrawEllipse(ellipse, brush.get(), width,
                                item.style == 2 ? dashStroke.get() : roundStroke.get());
            break;
        }
        case Tool::Arrow: {
            Point v = item.b - item.a;
            float len = length(v);
            if (item.style != 0 && len > .01f)
            {
                auto outline = item.arrowContour();
                Com<ID2D1PathGeometry> path;
                Com<ID2D1GeometrySink> sink;
                check(factory->CreatePathGeometry(path.put()), "Cannot create arrow geometry.");
                check(path->Open(sink.put()), "Cannot draw arrow geometry.");
                sink->BeginFigure({outline[0].x, outline[0].y}, D2D1_FIGURE_BEGIN_FILLED);
                for (size_t i = 1; i < outline.size(); ++i)
                    sink->AddLine({outline[i].x, outline[i].y});
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                check(sink->Close(), "Cannot finish arrow geometry.");
                rt->FillGeometry(path.get(), brush.get());
                brush->SetColor(color(rgb(12, 12, 16)));
                rt->DrawGeometry(path.get(), brush.get(),
                                 std::min(len * .06f, std::max(1.2f, width * .45f)),
                                 item.style == 4 ? nullptr : roundStroke.get());
                if (item.style == 2 || item.style == 3 || item.style == 4)
                {
                    const Color c = item.color;
                    brush->SetColor(color(rgb((c & 255) / 2 + 127, ((c >> 8) & 255) / 2 + 127,
                                              ((c >> 16) & 255) / 2 + 127),
                                          .85f));
                    Point previous = item.arrowSpine(.22f);
                    float end = std::clamp(1 - std::min(len * .45f,
                                                        std::max({22.0f, width * 6, len * .18f})) /
                                                   len,
                                           .55f, .84f) *
                                .88f;
                    for (int i = 1; i <= 32; ++i)
                    {
                        Point next = item.arrowSpine(.22f + (end - .22f) * i / 32.0f);
                        line(previous, next, std::min(len * .012f, std::max(1.0f, width * .5f)));
                        previous = next;
                    }
                }
            }
            else
            {
                line(item.a, item.b, width);
                if (len > .01f)
                {
                    Point u = v * (1 / len), n{-u.y, u.x};
                    float head = std::min(len * .45f, std::max(12.0f, width * 3));
                    line(item.b, item.b - u * head + n * (head * .5f), width);
                    line(item.b, item.b - u * head - n * (head * .5f), width);
                }
            }
            break;
        }
        case Tool::Line:
            line(item.a, item.b, width,
                 item.style == 1   ? dashStroke.get()
                 : item.style == 2 ? dotStroke.get()
                                   : nullptr);
            break;
        case Tool::Check: {
            float w = r.width(), h = r.height(), stroke = std::max(1.0f, std::min(w, h) * .06f);
            const bool cross = item.style >= 3;
            const auto badge = item.style % 3;
            if (badge == 0)
                rt->DrawRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(r.left, r.top, r.right, r.bottom), w * .12f,
                                      h * .12f),
                    brush.get(), stroke);
            else if (badge == 1)
                rt->DrawEllipse(D2D1::Ellipse({(r.left + r.right) / 2, (r.top + r.bottom) / 2},
                                              w * .48f, h * .48f),
                                brush.get(), stroke);
            const float markStroke = stroke * (badge == 2 ? 1.8f : 1.4f);
            if (cross)
            {
                line({r.left + w * .28f, r.top + h * .28f}, {r.left + w * .72f, r.top + h * .72f},
                     markStroke);
                line({r.left + w * .72f, r.top + h * .28f}, {r.left + w * .28f, r.top + h * .72f},
                     markStroke);
            }
            else
            {
                line({r.left + w * .23f, r.top + h * .52f}, {r.left + w * .43f, r.top + h * .72f},
                     markStroke);
                line({r.left + w * .43f, r.top + h * .72f}, {r.left + w * .79f, r.top + h * .28f},
                     markStroke);
            }
            break;
        }
        default:
            break;
        }
        if (opacity < 1)
            rt->PopLayer();
    }
}
static Com<IWICImagingFactory> wicFactory()
{
    Com<IWICImagingFactory> wic;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           __uuidof(IWICImagingFactory), reinterpret_cast<void **>(wic.put())),
          "Cannot initialize Windows image encoding.");
    return wic;
}
Bitmap Graphics::flatten(const Bitmap &image, const std::vector<Annotation> &items, int editingText)
{
    if (image.empty())
        throw std::runtime_error("Take a snip first.");
    if (items.empty())
        return image;
    initialize();
    auto wic = wicFactory();
    Com<IWICBitmap> bitmap;
    check(wic->CreateBitmap(image.width, image.height, GUID_WICPixelFormat32bppPBGRA,
                            WICBitmapCacheOnLoad, bitmap.put()),
          "Cannot create export image.");
    Com<ID2D1RenderTarget> rt;
    auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    check(factory->CreateWicBitmapRenderTarget(bitmap.get(), properties, rt.put()),
          "Cannot initialize image export renderer.");
    Com<ID2D1Bitmap> base;
    check(rt->CreateBitmap(D2D1::SizeU(image.width, image.height), image.pixels.data(),
                           image.width * 4,
                           D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                    D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                  96, 96),
                           base.put()),
          "Cannot load screenshot into export renderer.");
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(1, 1, 1));
    rt->DrawBitmap(
        base.get(),
        D2D1::RectF(0, 0, static_cast<float>(image.width), static_cast<float>(image.height)), 1,
        D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    drawAnnotations(rt.get(), items, editingText);
    check(rt->EndDraw(), "Cannot render annotations.");
    Bitmap result = Bitmap::create(image.width, image.height);
    check(bitmap->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(result.pixels.size()),
                             result.pixels.data()),
          "Cannot read exported image.");
    return result;
}
Bitmap Graphics::flattenRegion(const Bitmap &image, const std::vector<Annotation> &items,
                               int editingText, int x, int y, int width, int height)
{
    auto shifted = items;
    for (auto &item : shifted)
        item.move({-float(x), -float(y)});
    return flatten(image.crop(x, y, width, height), shifted, editingText);
}
const Bitmap &Graphics::samtecLogo(int mark)
{
    auto &cached = samtecLogos_.at(mark);
    if (!cached.empty())
        return cached;
    const HMODULE module = GetModuleHandleW(nullptr);
    const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(201 + mark), RT_RCDATA);
    const DWORD size = resource ? SizeofResource(module, resource) : 0;
    const HGLOBAL memory = resource ? LoadResource(module, resource) : nullptr;
    const auto bytes = memory ? static_cast<const uint8_t *>(LockResource(memory)) : nullptr;
    if (!bytes || !size)
        throw std::runtime_error("Cannot load the embedded Samtec logo.");
    const auto decoded = decode(std::vector<uint8_t>(bytes, bytes + size));
    // Ignore transparent margins in the master asset when determining watermark size.
    int left = decoded.width, top = decoded.height, right = 0, bottom = 0;
    for (int y = 0; y < decoded.height; ++y)
        for (int x = 0; x < decoded.width; ++x)
            if (decoded.pixels[(static_cast<size_t>(y) * decoded.width + x) * 4 + 3] >= 8)
            {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x + 1);
                bottom = std::max(bottom, y + 1);
            }
    if (left >= right || top >= bottom)
        throw std::runtime_error("The Samtec logo asset is empty.");
    cached = decoded.crop(left, top, right - left, bottom - top);
    return cached;
}
static void compositePixel(Bitmap &image, int x, int y, Color value, double alpha)
{
    if (alpha <= 0 || x < 0 || y < 0 || x >= image.width || y >= image.height)
        return;
    const size_t out = (static_cast<size_t>(y) * image.width + x) * 4;
    const double previous = image.pixels[out + 3] / 255.0,
                 finalAlpha = alpha + previous * (1 - alpha);
    for (int c = 0; c < 3; ++c)
    {
        const unsigned component = (value >> ((2 - c) * 8)) & 255;
        image.pixels[out + c] = static_cast<uint8_t>(std::lround(
            (component * alpha + image.pixels[out + c] * previous * (1 - alpha)) / finalAlpha));
    }
    image.pixels[out + 3] = static_cast<uint8_t>(std::lround(finalAlpha * 255));
}
static Bitmap resizedTransparent(const Bitmap &image, int width, int height)
{
    auto premultiplied = image;
    for (size_t i = 0; i < premultiplied.pixels.size(); i += 4)
        for (int c = 0; c < 3; ++c)
            premultiplied.pixels[i + c] = static_cast<uint8_t>(
                (premultiplied.pixels[i + c] * premultiplied.pixels[i + 3] + 127) / 255);
    auto wic = wicFactory();
    Com<IWICBitmap> source;
    check(wic->CreateBitmapFromMemory(image.width, image.height, GUID_WICPixelFormat32bppPBGRA,
                                      image.width * 4,
                                      static_cast<UINT>(premultiplied.pixels.size()),
                                      premultiplied.pixels.data(), source.put()),
          "Cannot read Samtec logo pixels.");
    Com<IWICBitmapScaler> scaler;
    check(wic->CreateBitmapScaler(scaler.put()), "Cannot scale Samtec logo.");
    check(scaler->Initialize(source.get(), width, height, WICBitmapInterpolationModeFant),
          "Cannot resize Samtec logo.");
    auto scaled = Bitmap::create(width, height);
    check(scaler->CopyPixels(nullptr, width * 4, static_cast<UINT>(scaled.pixels.size()),
                             scaled.pixels.data()),
          "Cannot render Samtec logo.");
    for (size_t i = 0; i < scaled.pixels.size(); i += 4)
        for (int c = 0; c < 3; ++c)
            scaled.pixels[i + c] =
                scaled.pixels[i + 3]
                    ? static_cast<uint8_t>(
                          std::min(255U, (scaled.pixels[i + c] * 255U + scaled.pixels[i + 3] / 2U) /
                                             scaled.pixels[i + 3]))
                    : 0;
    return scaled;
}
Bitmap Graphics::samtecBadge(uint8_t style, int logoHeight, bool lightWatermark)
{
    style = style < 6 ? style : 0;
    const int mark = style / 2;
    const bool soft = style % 2;
    const auto &logo = samtecLogo(mark);
    const int height = std::clamp(logoHeight, 1, 128);
    const int width =
        std::max(1, static_cast<int>(std::lround(height * double(logo.width) / logo.height)));
    const auto resized = resizedTransparent(logo, width, height);
    if (soft)
    {
        // A transparent monochrome mark with a faint opposing halo around the artwork only.
        constexpr int halo = 3;
        auto watermark = Bitmap::create(width + halo * 2, height + halo * 2);
        const Color ink = lightWatermark ? rgb(238, 238, 238) : rgb(80, 80, 80);
        const Color edge = lightWatermark ? rgb(0, 0, 0) : rgb(255, 255, 255);
        for (int y = -halo; y < height + halo; ++y)
            for (int x = -halo; x < width + halo; ++x)
            {
                const double alpha =
                    x >= 0 && x < width && y >= 0 && y < height
                        ? resized.pixels[(static_cast<size_t>(y) * width + x) * 4 + 3] / 255.0
                        : 0;
                double glow = 0;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                        if (x + dx >= 0 && x + dx < width && y + dy >= 0 && y + dy < height)
                        {
                            const double nearby =
                                resized.pixels[(static_cast<size_t>(y + dy) * width + x + dx) * 4 +
                                               3] /
                                255.0;
                            glow = std::max(glow, nearby * std::exp(-(dx * dx + dy * dy) / 2.0));
                        }
                compositePixel(watermark, x + halo, y + halo, edge, glow * (1 - alpha) * .10);
                compositePixel(watermark, x + halo, y + halo, ink, alpha * .32);
            }
        return watermark;
    }
    const int padding = std::max(2, static_cast<int>(std::lround(height * .18)));
    const int halo = 6;
    const int cardWidth = (mark == 0 ? height : width) + padding * 2;
    const int cardHeight = height + padding * 2;
    const double radius = std::min(7.0, cardHeight * .15);
    auto badge = Bitmap::create(cardWidth + halo * 2, cardHeight + halo * 2);
    auto distance = [&](double x, double y) {
        const double qx = std::abs(x - halo - cardWidth / 2.0) - (cardWidth / 2.0 - radius);
        const double qy = std::abs(y - halo - cardHeight / 2.0) - (cardHeight / 2.0 - radius);
        return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) -
               radius;
    };
    for (int y = 0; y < badge.height; ++y)
        for (int x = 0; x < badge.width; ++x)
        {
            const double d = distance(x + .5, y + .5), outside = std::max(0.0, d);
            const double sigma = 2.2;
            const double shadow = .12 * std::exp(-outside * outside / (2 * sigma * sigma));
            compositePixel(badge, x, y, rgb(85, 90, 105), shadow);
            const double coverage = std::clamp(.5 - d, 0.0, 1.0);
            compositePixel(badge, x, y, rgb(255, 255, 255), coverage * .98);
            const double border = coverage * std::clamp(d + 1.5, 0.0, 1.0) * .22;
            compositePixel(badge, x, y, rgb(128, 128, 128), border);
        }
    const int left = halo + (cardWidth - width) / 2, top = halo + padding;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const size_t i = (static_cast<size_t>(y) * width + x) * 4;
            compositePixel(badge, left + x, top + y,
                           rgb(resized.pixels[i + 2], resized.pixels[i + 1], resized.pixels[i]),
                           resized.pixels[i + 3] / 255.0 * .94);
        }
    return badge;
}
Rect Graphics::samtecLogoBounds(const Bitmap &image, uint8_t style)
{
    style = style < 6 ? style : 0;
    const int side = std::min(image.width, image.height);
    const int margin =
        std::min(std::max(1, static_cast<int>(std::lround(side * .025))), (side - 1) / 2);
    auto master = samtecBadge(style);
    constexpr double fractions[] = {.09, .075, .085, .07, .055, .05};
    constexpr int minHeights[] = {32, 26, 32, 26, 22, 20};
    constexpr int maxHeights[] = {80, 64, 80, 64, 52, 44};
    const int desiredHeight =
        std::clamp(static_cast<int>(std::lround(side * fractions[style])), minHeights[style],
                   maxHeights[style]);
    // Keep small marks recognizable, but limit their footprint on tiny/narrow captures.
    const double availableWidth = std::min(double(image.width - margin * 2),
                                           std::max(1.0, image.width * .35));
    const double availableHeight = std::min(double(image.height - margin * 2),
                                            std::max(1.0, image.height * .30));
    const double scale = std::min({double(desiredHeight) / master.height,
                                   availableWidth / master.width,
                                   availableHeight / master.height});
    const int width = std::max(1, static_cast<int>(std::lround(master.width * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(master.height * scale)));
    const int left = image.width - margin - width, top = image.height - margin - height;
    return {float(left), float(top), float(left + width), float(top + height)};
}
void Graphics::applySamtecLogo(Bitmap &image, uint8_t style)
{
    style = style < 6 ? style : 0;
    const auto bounds = samtecLogoBounds(image, style);
    const int left = static_cast<int>(bounds.left), top = static_cast<int>(bounds.top);
    const int width = static_cast<int>(bounds.width()), height = static_cast<int>(bounds.height());
    auto master = samtecBadge(style);
    const bool soft = style % 2;
    if (soft)
    {
        double luminance = 0;
        int samples = 0;
        const int step = std::max(1, std::min(width, height) / 16);
        for (int y = top; y < top + height; y += step)
            for (int x = left; x < left + width; x += step)
            {
                const size_t i = (static_cast<size_t>(y) * image.width + x) * 4;
                const double alpha = image.pixels[i + 3] / 255.0;
                luminance += (.2126 * image.pixels[i + 2] + .7152 * image.pixels[i + 1] +
                              .0722 * image.pixels[i]) *
                                 alpha +
                             255 * (1 - alpha);
                ++samples;
            }
        if (luminance / samples < 128)
            master = samtecBadge(style, 48, true);
    }
    const auto badge = resizedTransparent(master, width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const size_t i = (static_cast<size_t>(y) * width + x) * 4;
            compositePixel(image, left + x, top + y,
                           rgb(badge.pixels[i + 2], badge.pixels[i + 1], badge.pixels[i]),
                           badge.pixels[i + 3] / 255.0);
        }
}
Bitmap Graphics::exportImage(const Bitmap &image, const std::vector<Annotation> &items,
                             const ExportOptions &options, int editingText)
{
    auto content = flatten(image, items, editingText);
    if (options.samtecLogo)
        applySamtecLogo(content, options.samtecStyle);
    if (options.jpg || !options.professionalBorder ||
        (!options.professionalBlur && !options.professionalRounded))
        return content;

    const int padding = options.professionalBlur ? 20 : 0;
    constexpr int blur = 16;
    constexpr double radius = 8, spread = 1, offsetY = 2, sigma = 10;
    // A soft neutral halo without a hard outline; fully covered screenshot pixels
    // remain byte-for-byte unchanged. Rounding alone needs no extra padding.
    constexpr double shadowOpacity = .48;
    const double left = padding, top = padding;
    const double right = left + content.width, bottom = top + content.height;
    const double corner = options.professionalRounded
                              ? std::min({radius, content.width / 2.0, content.height / 2.0})
                              : 0;
    auto result = Bitmap::create(content.width + padding * 2, content.height + padding * 2);

    // A truncated separable Gaussian. Integrating horizontal row spans means only the
    // visible halo needs convolution, without full-image floating-point scratch buffers.
    std::array<double, blur * 2 + 1> kernel{};
    std::array<double, blur * 2 + 2> cumulative{};
    double sum = 0;
    for (int i = -blur; i <= blur; ++i)
        sum += kernel[i + blur] = std::exp(-i * i / (2 * sigma * sigma));
    for (size_t i = 0; i < kernel.size(); ++i)
    {
        kernel[i] /= sum;
        cumulative[i + 1] = cumulative[i] + kernel[i];
    }
    auto integral = [&](double coordinate) {
        const double index = coordinate + blur + .5;
        if (index <= 0)
            return 0.0;
        if (index >= kernel.size())
            return 1.0;
        const size_t i = static_cast<size_t>(index);
        return cumulative[i] + (index - i) * kernel[i];
    };
    auto coverage = [&](double x, double y, double expansion) {
        const double r = corner + expansion;
        const double dx = std::abs(x - (left + right) / 2) - (content.width / 2.0 + expansion - r);
        const double dy = std::abs(y - (top + bottom) / 2) - (content.height / 2.0 + expansion - r);
        const double distance =
            std::hypot(std::max(dx, 0.0), std::max(dy, 0.0)) + std::min(std::max(dx, dy), 0.0) - r;
        return std::clamp(.5 - distance, 0.0, 1.0);
    };
    for (int y = 0; y < result.height; ++y)
    {
        std::array<double, blur * 2 + 1> rowLeft{}, rowRight{};
        std::array<bool, blur * 2 + 1> rowVisible{};
        for (int j = -blur; options.professionalBlur && j <= blur; ++j)
        {
            const double row = y + .5 - offsetY - j;
            if (row < top - spread || row >= bottom + spread)
                continue;
            const double r = corner + spread;
            const double dy = std::max({top - spread + r - row, row - (bottom + spread - r), 0.0});
            const double inset = r - std::sqrt(std::max(0.0, r * r - dy * dy));
            rowLeft[j + blur] = left - spread + inset;
            rowRight[j + blur] = right + spread - inset;
            rowVisible[j + blur] = true;
        }
        for (int x = 0; x < result.width; ++x)
        {
            const size_t out = (static_cast<size_t>(y) * result.width + x) * 4;
            const double inside = coverage(x + .5, y + .5, 0);
            const uint8_t *pixel = nullptr;
            if (x >= padding && x < padding + content.width && y >= padding &&
                y < padding + content.height)
                pixel =
                    &content
                         .pixels[(static_cast<size_t>(y - padding) * content.width + x - padding) *
                                 4];
            // Keep every fully covered screenshot pixel byte-for-byte unchanged.
            if (inside == 1 && pixel && pixel[3] == 255)
            {
                std::memcpy(&result.pixels[out], pixel, 4);
                continue;
            }
            double shadow = 0;
            for (size_t j = 0; j < kernel.size(); ++j)
                if (rowVisible[j])
                    shadow += kernel[j] *
                              (integral(rowRight[j] - x - .5) - integral(rowLeft[j] - x - .5));
            double alpha = shadow * shadowOpacity;
            std::array<double, 3> channels = {90 * alpha, 90 * alpha, 90 * alpha};
            auto over = [&](const std::array<double, 3> &rgb, double opacity) {
                for (size_t i = 0; i < channels.size(); ++i)
                    channels[i] = rgb[i] * opacity + channels[i] * (1 - opacity);
                alpha = opacity + alpha * (1 - opacity);
            };
            if (pixel)
                over({double(pixel[0]), double(pixel[1]), double(pixel[2])},
                     inside * pixel[3] / 255.0);
            result.pixels[out + 3] = static_cast<uint8_t>(std::lround(alpha * 255));
            if (result.pixels[out + 3])
                for (size_t i = 0; i < channels.size(); ++i)
                    result.pixels[out + i] = static_cast<uint8_t>(std::lround(channels[i] / alpha));
        }
    }
    return result;
}
std::vector<uint8_t> Graphics::png(const Bitmap &bitmap)
{
    auto wic = wicFactory();
    Com<IStream> stream;
    check(CreateStreamOnHGlobal(nullptr, TRUE, stream.put()), "Cannot allocate PNG stream.");
    Com<IWICBitmapEncoder> encoder;
    check(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()),
          "Cannot create PNG encoder.");
    check(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache),
          "Cannot initialize PNG encoder.");
    Com<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(frame.put(), nullptr), "Cannot create PNG frame.");
    check(frame->Initialize(nullptr), "Cannot initialize PNG frame.");
    check(frame->SetSize(bitmap.width, bitmap.height), "Cannot set PNG size.");
    check(frame->SetResolution(96, 96), "Cannot set PNG resolution.");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&format), "Cannot set PNG pixel format.");
    // WIC's PNG encoder may select a different pixel format; convert through a bitmap source.
    Com<IWICBitmap> source;
    check(wic->CreateBitmapFromMemory(bitmap.width, bitmap.height, GUID_WICPixelFormat32bppBGRA,
                                      bitmap.width * 4, static_cast<UINT>(bitmap.pixels.size()),
                                      const_cast<BYTE *>(bitmap.pixels.data()), source.put()),
          "Cannot create PNG source.");
    Com<IWICFormatConverter> converter;
    check(wic->CreateFormatConverter(converter.put()), "Cannot create PNG converter.");
    check(converter->Initialize(source.get(), format, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom),
          "Cannot convert PNG pixels.");
    check(frame->WriteSource(converter.get(), nullptr), "Cannot encode PNG pixels.");
    check(frame->Commit(), "Cannot finish PNG frame.");
    check(encoder->Commit(), "Cannot finish PNG file.");
    STATSTG stat{};
    check(stream->Stat(&stat, STATFLAG_NONAME), "Cannot read PNG length.");
    if (stat.cbSize.QuadPart > UINT32_MAX)
        throw std::runtime_error("PNG file is too large.");
    std::vector<uint8_t> bytes(static_cast<size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    check(stream->Seek(zero, STREAM_SEEK_SET, nullptr), "Cannot rewind PNG stream.");
    ULONG read = 0;
    check(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read),
          "Cannot read PNG stream.");
    if (read != bytes.size())
        throw std::runtime_error("PNG encoding was incomplete.");
    return bytes;
}
std::vector<uint8_t> Graphics::jpeg(const Bitmap &bitmap)
{
    auto wic = wicFactory();
    Com<IStream> stream;
    check(CreateStreamOnHGlobal(nullptr, TRUE, stream.put()), "Cannot allocate JPG stream.");
    Com<IWICBitmapEncoder> encoder;
    check(wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, encoder.put()),
          "Cannot create JPG encoder.");
    check(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache),
          "Cannot initialize JPG encoder.");
    Com<IWICBitmapFrameEncode> frame;
    Com<IPropertyBag2> options;
    check(encoder->CreateNewFrame(frame.put(), options.put()), "Cannot create JPG frame.");
    PROPBAG2 properties[2]{};
    properties[0].pstrName = const_cast<wchar_t *>(L"ImageQuality");
    properties[1].pstrName = const_cast<wchar_t *>(L"JpegYCrCbSubsampling");
    VARIANT values[2]{};
    values[0].vt = VT_R4;
    values[0].fltVal = .95f;
    values[1].vt = VT_UI1;
    values[1].bVal = WICJpegYCrCbSubsampling444;
    check(options->Write(2, properties, values), "Cannot set JPG quality.");
    check(frame->Initialize(options.get()), "Cannot initialize JPG frame.");
    check(frame->SetSize(bitmap.width, bitmap.height), "Cannot set JPG size.");
    check(frame->SetResolution(96, 96), "Cannot set JPG resolution.");
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    check(frame->SetPixelFormat(&format), "Cannot set JPG pixel format.");
    // Imported images can contain straight alpha. Composite onto white before
    // encoding because JPG cannot preserve transparent source pixels.
    std::vector<uint8_t> opaque(bitmap.pixels.size() / 4 * 3);
    for (size_t pixel = 0; pixel < bitmap.pixels.size() / 4; ++pixel)
    {
        const unsigned alpha = bitmap.pixels[pixel * 4 + 3];
        for (size_t channel = 0; channel < 3; ++channel)
            opaque[pixel * 3 + channel] = static_cast<uint8_t>(
                (bitmap.pixels[pixel * 4 + channel] * alpha + 255 * (255 - alpha) + 127) / 255);
    }
    Com<IWICBitmap> source;
    check(wic->CreateBitmapFromMemory(bitmap.width, bitmap.height, GUID_WICPixelFormat24bppBGR,
                                      bitmap.width * 3, static_cast<UINT>(opaque.size()),
                                      opaque.data(), source.put()),
          "Cannot create JPG source.");
    Com<IWICFormatConverter> converter;
    check(wic->CreateFormatConverter(converter.put()), "Cannot create JPG converter.");
    check(converter->Initialize(source.get(), format, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom),
          "Cannot convert JPG pixels.");
    check(frame->WriteSource(converter.get(), nullptr), "Cannot encode JPG pixels.");
    check(frame->Commit(), "Cannot finish JPG frame.");
    check(encoder->Commit(), "Cannot finish JPG file.");
    STATSTG stat{};
    check(stream->Stat(&stat, STATFLAG_NONAME), "Cannot read JPG length.");
    if (stat.cbSize.QuadPart > UINT32_MAX)
        throw std::runtime_error("JPG file is too large.");
    std::vector<uint8_t> bytes(static_cast<size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    check(stream->Seek(zero, STREAM_SEEK_SET, nullptr), "Cannot rewind JPG stream.");
    ULONG read = 0;
    check(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read),
          "Cannot read JPG stream.");
    if (read != bytes.size())
        throw std::runtime_error("JPG encoding was incomplete.");
    return bytes;
}
Bitmap Graphics::decode(const std::vector<uint8_t> &bytes)
{
    auto wic = wicFactory();
    Com<IWICStream> stream;
    check(wic->CreateStream(stream.put()), "Decode stream failed.");
    check(stream->InitializeFromMemory(const_cast<BYTE *>(bytes.data()),
                                       static_cast<DWORD>(bytes.size())),
          "Decode stream data failed.");
    Com<IWICBitmapDecoder> decoder;
    check(wic->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                       decoder.put()),
          "PNG decoding failed.");
    Com<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, frame.put()), "Decode frame failed.");
    UINT w = 0, h = 0;
    check(frame->GetSize(&w, &h), "Decode size failed.");
    auto result = Bitmap::create(w, h);
    Com<IWICFormatConverter> converter;
    check(wic->CreateFormatConverter(converter.put()), "Decode converter failed.");
    check(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                nullptr, 0, WICBitmapPaletteTypeCustom),
          "Decode conversion failed.");
    check(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(result.pixels.size()),
                                result.pixels.data()),
          "Decode pixels failed.");
    return result;
}
void Graphics::test()
{
    runModelTests();
    auto source = Bitmap::create(160, 120);
    std::fill(source.pixels.begin(), source.pixels.end(), 255);
    {
        // Real JPEG round trips, including white screenshots, dark pixels and alpha.
        for (const auto channels : {std::array<uint8_t, 4>{255, 255, 255, 255},
                                    std::array<uint8_t, 4>{0, 0, 0, 255},
                                    std::array<uint8_t, 4>{160, 96, 64, 255},
                                    std::array<uint8_t, 4>{60, 100, 140, 128},
                                    std::array<uint8_t, 4>{0, 0, 0, 0}})
        {
            auto fixture = Bitmap::create(32, 24);
            for (size_t i = 0; i < fixture.pixels.size(); i += 4)
                std::copy(channels.begin(), channels.end(), fixture.pixels.begin() + i);
            const auto original = fixture.pixels;
            const auto bytes = jpeg(fixture);
            const auto decoded = decode(bytes);
            if (bytes.size() < 2 || bytes[0] != 0xff || bytes[1] != 0xd8 ||
                decoded.width != fixture.width || decoded.height != fixture.height ||
                fixture.pixels != original)
                throw std::runtime_error("JPG encoding, dimensions or source preservation failed.");
            for (size_t i = 0; i < decoded.pixels.size(); i += 4)
            {
                if (decoded.pixels[i + 3] != 255)
                    throw std::runtime_error("JPG decoding must be opaque.");
                for (size_t c = 0; c < 3; ++c)
                {
                    const int expected = (channels[c] * channels[3] + 255 * (255 - channels[3]) + 127) / 255;
                    if (std::abs(decoded.pixels[i + c] - expected) > 4)
                        throw std::runtime_error("JPG colors or transparent-pixel compositing failed.");
                }
            }
        }
        auto tiny = Bitmap::create(1, 1);
        std::fill(tiny.pixels.begin(), tiny.pixels.end(), 255);
        if (decode(jpeg(tiny)).pixels != tiny.pixels)
            throw std::runtime_error("Tiny white JPG export failed.");
        ExportOptions options{true, true};
        options.jpg = true;
        const auto plain = exportImage(source, {}, {false, true});
        const auto jpgExport = exportImage(source, {}, options);
        if (jpgExport.width != plain.width || jpgExport.height != plain.height ||
            jpgExport.pixels != plain.pixels || !options.professionalBlur || !options.professionalRounded)
            throw std::runtime_error("JPG did not suppress Professional Border and retain logo/content.");
        options.jpg = false;
        const auto restored = exportImage(source, {}, options);
        if (restored.width != source.width + 40 || restored.height != source.height + 40)
            throw std::runtime_error("PNG did not restore Professional Border after JPG.");
        saveBytes(L"jpg-export-preview.jpg", jpeg(jpgExport));
    }
    {
        Annotation highlight;
        highlight.kind = Tool::Highlight;
        highlight.color = rgb(250, 204, 21);
        highlight.thickness = 20;
        highlight.points = {{20, 50}, {80, 50}, {140, 50}, {20, 50}, {140, 50}};
        const auto highlighted = flatten(source, {highlight});
        auto nearColor = [](Color actual, Color expected) {
            for (int channel = 0; channel < 3; ++channel)
                if (std::abs(static_cast<int>((actual >> (channel * 8)) & 255) -
                             static_cast<int>((expected >> (channel * 8)) & 255)) > 2)
                    return false;
            return true;
        };
        if (!nearColor(*highlighted.sample({40, 50}), rgb(253, 237, 173)) ||
            highlighted.sample({40, 50}) != highlighted.sample({80, 50}) ||
            highlighted.sample({80, 50}) != highlighted.sample({120, 50}) ||
            highlighted.sample({80, 65}) != rgb(255, 255, 255) ||
            decode(png(highlighted)).pixels != highlighted.pixels)
            throw std::runtime_error(
                "Highlight transparency, stroke overlap, clipping, or PNG round trip failed.");
        highlight.points = {{80, 50}};
        const auto chisel = flatten(source, {highlight});
        if (!nearColor(*chisel.sample({80, 42}), rgb(253, 237, 173)) ||
            chisel.sample({72, 50}) != rgb(255, 255, 255))
            throw std::runtime_error("Highlight nib is not a broad slanted chisel.");
        auto textSource = source;
        const size_t blackPixel = (50 * 160 + 80) * 4;
        textSource.pixels[blackPixel] = textSource.pixels[blackPixel + 1] =
            textSource.pixels[blackPixel + 2] = 0;
        const auto readable = flatten(textSource, {highlight});
        if (!nearColor(*readable.sample({80, 50}), rgb(88, 71, 7)))
            throw std::runtime_error("Highlight obscured the underlying text.");
        const auto twice = flatten(source, {highlight, highlight});
        if (*twice.sample({80, 50}) == *chisel.sample({80, 50}))
            throw std::runtime_error("Separate highlight strokes did not blend in document order.");
        auto highlightPreview = Bitmap::create(640, 240);
        std::fill(highlightPreview.pixels.begin(), highlightPreview.pixels.end(), 255);
        Annotation label;
        label.kind = Tool::Text;
        label.color = rgb(15, 23, 42);
        label.fontSize = 24;
        label.a = {32, 32};
        label.text = L"Readable text under a yellow chisel highlight";
        measureText(label);
        highlight.points = {{30, 50}, {580, 50}};
        highlight.thickness = 30;
        Annotation blue = highlight;
        blue.color = rgb(14, 165, 233);
        blue.points = {{40, 140}, {250, 140}, {320, 110}, {400, 170}, {575, 145}};
        saveBytes(L"highlight-preview.png",
                  png(flatten(highlightPreview, {label, highlight, blue})));
    }
    Annotation pen;
    pen.kind = Tool::Pen;
    pen.color = rgb(255, 0, 0);
    pen.thickness = 8;
    pen.points = {{10, 10}, {100, 10}};
    Annotation circle;
    circle.kind = Tool::Circle;
    circle.color = rgb(0, 0, 255);
    circle.thickness = 6;
    circle.a = {20, 30};
    circle.b = {60, 70};
    Annotation arrow;
    arrow.kind = Tool::Arrow;
    arrow.color = rgb(0, 255, 0);
    arrow.thickness = 6;
    arrow.a = {80, 30};
    arrow.b = {130, 80};
    Annotation mark;
    mark.kind = Tool::Check;
    mark.color = rgb(0, 128, 0);
    mark.a = {10, 80};
    mark.b = {40, 110};
    auto flattened = flatten(source, {pen, circle, arrow, mark});
    auto encoded = png(flattened);
    auto decoded = decode(encoded);
    if (decoded.width != 160 || decoded.height != 120 || decoded.pixels != flattened.pixels)
        throw std::runtime_error("PNG round-trip test failed.");
    auto pixel = [&](int x, int y, Color c) {
        size_t i = (static_cast<size_t>(y) * 160 + x) * 4;
        return flattened.pixels[i] == ((c >> 16) & 255) &&
               flattened.pixels[i + 1] == ((c >> 8) & 255) &&
               flattened.pixels[i + 2] == (c & 255) && flattened.pixels[i + 3] == 255;
    };
    if (!pixel(50, 10, rgb(255, 0, 0)))
        throw std::runtime_error("Pen export pixel test failed.");
    if (!pixel(20, 50, rgb(0, 0, 255)))
        throw std::runtime_error("Circle export pixel test failed.");
    if (!pixel(105, 55, rgb(0, 255, 0)))
        throw std::runtime_error("Arrow export pixel test failed.");
    size_t checkPixel = (95 * 160 + 10) * 4;
    if (flattened.pixels[checkPixel] > 100 || flattened.pixels[checkPixel + 2] > 100 ||
        flattened.pixels[checkPixel + 1] < 100 || flattened.pixels[checkPixel + 1] > 180)
        throw std::runtime_error("Check export pixel test failed.");
    if (!pixel(159, 119, rgb(255, 255, 255)))
        throw std::runtime_error("Image background export test failed.");
    auto stickerPreview = Bitmap::create(720, 240);
    std::fill(stickerPreview.pixels.begin(), stickerPreview.pixels.end(), 255);
    std::vector<Annotation> stickers;
    const wchar_t *stickerNames[] = {L"Boxed check", L"Circle badge",   L"Simple check",
                                     L"Boxed X",     L"Circle X badge", L"Simple X"};
    for (int style = 0; style < 6; ++style)
    {
        auto sticker = mark;
        sticker.style = static_cast<uint8_t>(style);
        sticker.color = style < 3 ? rgb(34, 197, 94) : rgb(239, 68, 68);
        for (int side : {24, 80})
        {
            sticker.a = {20, 20};
            sticker.b = {20.0f + side, 20.0f + side};
            const auto rendered = flatten(source, {sticker});
            const Point center{20.0f + side * .5f, 20.0f + side * .5f};
            if ((style >= 3 &&
                 (rendered.sample(center) != sticker.color ||
                  rendered.sample({20.0f + side * .3f, 20.0f + side * .3f}) != sticker.color ||
                  rendered.sample({20.0f + side * .7f, 20.0f + side * .3f}) != sticker.color)) ||
                (style < 3 && rendered.sample(center) == sticker.color) ||
                (style % 3 == 2 && rendered.sample({20, center.y}) != rgb(255, 255, 255)) ||
                (style % 3 != 2 && rendered.sample({20, center.y}) == rgb(255, 255, 255)) ||
                decode(png(rendered)).pixels != rendered.pixels)
                throw std::runtime_error(
                    "Check/X artwork, badge, size, color, or PNG round trip failed: style " +
                    std::to_string(style) + ", size " + std::to_string(side));
            sticker.a = {20.0f + style * 120, side == 80 ? 40.0f : 170.0f};
            sticker.b = sticker.a + Point{float(side), float(side)};
            stickers.push_back(sticker);
        }
        Annotation label;
        label.kind = Tool::Text;
        label.text = stickerNames[style];
        label.fontSize = 14;
        label.color = rgb(35, 39, 56);
        label.a = {10.0f + style * 120, 10};
        measureText(label);
        stickers.push_back(label);
    }
    saveBytes(L"check-x-style-preview.png", png(flatten(stickerPreview, stickers)));
    pen.move({0, 20});
    auto moved = flatten(source, {pen});
    size_t old = (10 * 160 + 50) * 4, next = (30 * 160 + 50) * 4;
    if (moved.pixels[old] != 255 || moved.pixels[next] != 0 || moved.pixels[next + 2] != 255)
        throw std::runtime_error("Moved sticker export test failed.");
    Annotation rectangle;
    rectangle.kind = Tool::Rectangle;
    rectangle.color = rgb(20, 90, 170);
    rectangle.thickness = 4;
    rectangle.a = {20, 20};
    rectangle.b = {100, 90};
    for (int style = 0; style < 4; ++style)
    {
        rectangle.style = static_cast<uint8_t>(style);
        const auto shape = flatten(source, {rectangle});
        const auto center = shape.sample({60, 55});
        if (shape.sample({20, 55}) != rectangle.color ||
            (style < 2 && center != rgb(255, 255, 255)) ||
            (style == 2 && (center == rgb(255, 255, 255) || center == rectangle.color)) ||
            (style == 3 && center != rectangle.color))
            throw std::runtime_error("Square/rounded/highlight/filled rectangle export failed.");
    }
    auto textSource = Bitmap::create(640, 320);
    for (size_t i = 0; i < textSource.pixels.size(); i += 4)
    {
        textSource.pixels[i] = textSource.pixels[i + 1] = textSource.pixels[i + 2] = 230;
        textSource.pixels[i + 3] = 255;
    }
    Annotation text;
    text.kind = Tool::Text;
    text.a = {20, 20};
    text.color = rgb(25, 50, 75);
    text.text = L"Review this value";
    text.fontSize = 24;
    measureText(text);
    const auto annotationLayout = textLayout(text);
    UINT32 familyLength = 0;
    check(annotationLayout->GetFontFamilyNameLength(0, &familyLength),
          "Cannot inspect annotation font name.");
    std::wstring family(familyLength + 1, L'\0');
    check(annotationLayout->GetFontFamilyName(0, family.data(), static_cast<UINT32>(family.size())),
          "Cannot inspect annotation typeface.");
    family.resize(family.size() - 1);
    if (family != annotationFontFamily)
        throw std::runtime_error("Text export did not use the default annotation typeface.");
    const auto plainBounds = text.bounds();
    const auto plain = flatten(textSource, {text});
    size_t inkPixels = 0;
    for (int y = static_cast<int>(text.a.y); y < text.b.y; ++y)
        for (int x = static_cast<int>(text.a.x); x < text.b.x; ++x)
            if (plain.sample({static_cast<float>(x), static_cast<float>(y)}) == text.color)
                ++inkPixels;
    text.bold = true;
    measureText(text);
    if (inkPixels < 30 || flatten(textSource, {text}).pixels == plain.pixels ||
        text.bounds().width() < plainBounds.width())
        throw std::runtime_error("Text rendering, font measurement, or bold export failed.");
    text.text += L"\r\nPlease confirm before sharing";
    text.boxed = true;
    measureText(text);
    const auto boxed = flatten(textSource, {text});
    if (text.bounds().height() <= plainBounds.height() + 24 ||
        boxed.sample({text.a.x + 15, text.b.y - 5}) != rgb(255, 255, 255) ||
        boxed.sample({0, 0}) != textSource.sample({0, 0}) ||
        decode(png(boxed)).pixels != boxed.pixels)
        throw std::runtime_error("Multiline/boxed text or text PNG round trip failed.");
    text.color = rgb(255, 255, 255);
    if (flatten(textSource, {text}).sample({text.a.x + 15, text.b.y - 5}) !=
        textBackground(text.color))
        throw std::runtime_error("White boxed text lost its contrasting background.");
    text.color = rgb(25, 50, 75);
    rectangle.a = {420, 160};
    rectangle.b = {605, 260};
    rectangle.style = 1;
    saveBytes(L"text-shape-preview.png", png(flatten(textSource, {text, rectangle})));
    const std::vector<Annotation> borderItems = {text, rectangle};
    const auto unstyled = flatten(textSource, borderItems);
    const auto disabled = exportImage(textSource, borderItems);
    const auto bordered = exportImage(textSource, borderItems, {true});
    if (disabled.width != unstyled.width || disabled.height != unstyled.height ||
        disabled.pixels != unstyled.pixels || bordered.width != unstyled.width + 40 ||
        bordered.height != unstyled.height + 40)
        throw std::runtime_error("Professional Border default or padded dimensions failed.");
    for (int y = 0; y < unstyled.height; ++y)
        for (int x = 0; x < unstyled.width; ++x)
            if ((x >= 8 && x < unstyled.width - 8) || (y >= 8 && y < unstyled.height - 8))
            {
                const size_t src = (static_cast<size_t>(y) * unstyled.width + x) * 4;
                const size_t dst = (static_cast<size_t>(y + 20) * bordered.width + x + 20) * 4;
                if (std::memcmp(&unstyled.pixels[src], &bordered.pixels[dst], 4))
                    throw std::runtime_error(
                        "Professional Border altered screenshot content pixels.");
            }
    auto alphaAt = [&](int x, int y) {
        return bordered.pixels[(static_cast<size_t>(y) * bordered.width + x) * 4 + 3];
    };
    const int middle = bordered.width / 2;
    if (alphaAt(20, 20) >= 255 || alphaAt(middle, 20) != 255 || alphaAt(middle, 19) < 30 ||
        alphaAt(middle, 19) > 80 || alphaAt(middle, 19) - alphaAt(middle, 18) > 8 ||
        alphaAt(middle, 18) <= alphaAt(middle, 12) || !alphaAt(middle, 12) || alphaAt(middle, 3))
        throw std::runtime_error("Professional Border rounding or smooth halo falloff failed.");
    size_t haloPixels = 0;
    for (int y = 0; y < bordered.height; ++y)
        for (int x = 0; x < bordered.width; ++x)
        {
            const size_t i = (static_cast<size_t>(y) * bordered.width + x) * 4;
            if ((x == 0 || y == 0 || x == bordered.width - 1 || y == bordered.height - 1) &&
                bordered.pixels[i + 3])
                throw std::runtime_error(
                    "Professional Border halo was clipped at the padding edge.");
            if (x < 18 || y < 18 || x >= bordered.width - 18 || y >= bordered.height - 18)
            {
                if (!bordered.pixels[i + 3])
                    continue;
                ++haloPixels;
                if (bordered.pixels[i] != 90 || bordered.pixels[i + 1] != 90 ||
                    bordered.pixels[i + 2] != 90 || bordered.pixels[i + 3] > 122)
                    throw std::runtime_error(
                        "Professional Border shadow is not faint neutral gray.");
            }
        }
    if (haloPixels < 100 ||
        alphaAt(18, bordered.height / 2) != alphaAt(bordered.width - 19, bordered.height / 2) ||
        decode(png(bordered)).pixels != bordered.pixels)
        throw std::runtime_error(
            "Professional Border symmetry or transparent PNG round trip failed.");
    // A visible, softened halo on both white and black backgrounds, without a bright ring.
    // Test composited contrast rather than merely accepting nonzero alpha values.
    const size_t halo = (static_cast<size_t>(bordered.height / 2) * bordered.width + 17) * 4;
    const double haloAlpha = bordered.pixels[halo + 3] / 255.0;
    const int onWhite = static_cast<int>(std::lround(90 * haloAlpha + 255 * (1 - haloAlpha)));
    const int onBlack = static_cast<int>(std::lround(90 * haloAlpha));
    if (255 - onWhite < 29 || onBlack < 16 || 255 - onWhite > 42 || onBlack > 24)
        throw std::runtime_error(
            "Professional Border halo is too faint or too strong on white/black backgrounds.");
    const double fartherAlpha =
        bordered.pixels[(static_cast<size_t>(bordered.height / 2) * bordered.width + 12) * 4 + 3] /
        255.0;
    if (std::lround(90 * fartherAlpha) < 8 || std::lround(165 * fartherAlpha) < 15)
        throw std::runtime_error(
            "Professional Border blur does not visibly extend beyond the edge.");
    for (bool blurEnabled : {false, true})
        for (bool rounded : {false, true})
        {
            ExportOptions options{true};
            options.professionalBlur = blurEnabled;
            options.professionalRounded = rounded;
            const auto sample = exportImage(textSource, borderItems, options);
            const int padding = blurEnabled ? 20 : 0;
            if (sample.width != unstyled.width + padding * 2 ||
                sample.height != unstyled.height + padding * 2 ||
                decode(png(sample)).pixels != sample.pixels)
                throw std::runtime_error(
                    "Independent Professional Border dimensions or PNG export failed.");
            if (!blurEnabled && !rounded && sample.pixels != unstyled.pixels)
                throw std::runtime_error(
                    "Disabling both professional effects changed the screenshot.");
            const size_t cornerPixel = (static_cast<size_t>(padding) * sample.width + padding) * 4;
            if (rounded ? sample.pixels[cornerPixel + 3] >= 255
                        : std::memcmp(&sample.pixels[cornerPixel], unstyled.pixels.data(), 4) != 0)
                throw std::runtime_error("Rounded corners did not toggle independently of blur.");
            for (int y = 0; y < unstyled.height; ++y)
                for (int x = 0; x < unstyled.width; ++x)
                    if (!rounded || (x >= 8 && x < unstyled.width - 8) ||
                        (y >= 8 && y < unstyled.height - 8))
                    {
                        const size_t src = (static_cast<size_t>(y) * unstyled.width + x) * 4;
                        const size_t dst =
                            (static_cast<size_t>(y + padding) * sample.width + x + padding) * 4;
                        if (std::memcmp(&unstyled.pixels[src], &sample.pixels[dst], 4))
                            throw std::runtime_error(
                                "A professional effect combination altered screenshot content.");
                    }
            options.professionalBorder = false;
            if (exportImage(textSource, borderItems, options).pixels != unstyled.pixels)
                throw std::runtime_error(
                    "The professional master toggle did not disable both effects.");
        }
    for (const auto &size : {std::pair{1, 1}, std::pair{3, 24}, std::pair{24, 3}})
    {
        auto tiny = Bitmap::create(size.first, size.second);
        std::fill(tiny.pixels.begin(), tiny.pixels.end(), 255);
        const auto exported = exportImage(tiny, {}, {true});
        if (exported.width != tiny.width + 40 || exported.height != tiny.height + 40 ||
            decode(png(exported)).pixels != exported.pixels)
            throw std::runtime_error("Professional Border failed on a narrow or one-pixel snip.");
    }
    saveBytes(L"professional-border.png", png(bordered));
    // Compare OFF (top) and ON (bottom) against both email background extremes.
    auto makeBorderPreview = [&](const Bitmap &unstyled, const Bitmap &bordered,
                                 const wchar_t *path) {
        auto borderPreview = Bitmap::create(bordered.width * 2 + 40, bordered.height * 2 + 100);
        for (int y = 0; y < borderPreview.height; ++y)
            for (int x = 0; x < borderPreview.width; ++x)
            {
                const size_t i = (static_cast<size_t>(y) * borderPreview.width + x) * 4;
                const uint8_t background = x < borderPreview.width / 2 ? 255 : 0;
                borderPreview.pixels[i] = borderPreview.pixels[i + 1] =
                    borderPreview.pixels[i + 2] = background;
                borderPreview.pixels[i + 3] = 255;
            }
        std::vector<Annotation> labels;
        for (int column = 0; column < 2; ++column)
            for (int row = 0; row < 2; ++row)
            {
                const Bitmap &sample = row ? bordered : unstyled;
                const int dx = 10 + column * (bordered.width + 20) + (row ? 0 : 20);
                const int dy = 45 + row * (bordered.height + 50) + (row ? 0 : 20);
                for (int y = 0; y < sample.height; ++y)
                    for (int x = 0; x < sample.width; ++x)
                    {
                        const size_t src = (static_cast<size_t>(y) * sample.width + x) * 4;
                        const size_t dst =
                            (static_cast<size_t>(y + dy) * borderPreview.width + x + dx) * 4;
                        const double opacity = sample.pixels[src + 3] / 255.0;
                        for (int c = 0; c < 3; ++c)
                            borderPreview.pixels[dst + c] = static_cast<uint8_t>(
                                std::lround(sample.pixels[src + c] * opacity +
                                            borderPreview.pixels[dst + c] * (1 - opacity)));
                    }
                Annotation label;
                label.kind = Tool::Text;
                label.fontSize = 16;
                label.bold = true;
                label.color = column ? rgb(235, 235, 235) : rgb(35, 39, 56);
                label.a = {float(30 + column * (bordered.width + 20)),
                           float(15 + row * (bordered.height + 50))};
                label.text = row ? L"Professional Border ON" : L"Professional Border OFF";
                measureText(label);
                labels.push_back(label);
            }
        saveBytes(path, png(flatten(borderPreview, labels)));
    };
    makeBorderPreview(unstyled, bordered, L"professional-border-preview.png");
    auto darkSource = textSource;
    for (size_t i = 0; i < darkSource.pixels.size(); i += 4)
        darkSource.pixels[i] = darkSource.pixels[i + 1] = darkSource.pixels[i + 2] = 35;
    Annotation lightText = text;
    lightText.color = rgb(235, 235, 235);
    lightText.boxed = false;
    measureText(lightText);
    const std::vector<Annotation> darkItems = {lightText, rectangle};
    const auto darkPlain = exportImage(darkSource, darkItems);
    const auto darkBordered = exportImage(darkSource, darkItems, {true});
    makeBorderPreview(darkPlain, darkBordered, L"professional-border-dark-preview.png");
    const auto &logo = samtecLogo();
    auto logoPixel = [&](double x, double y) {
        return &logo.pixels[(static_cast<size_t>(y * (logo.height - 1)) * logo.width +
                             static_cast<int>(x * (logo.width - 1))) *
                            4];
    };
    const auto bar = logoPixel(.5, .07), orange = logoPixel(.5, .52);
    if (logo.height < logo.width * 1.4 || logo.height > logo.width * 1.8 ||
        logoPixel(.75, .385)[3] > 5 || logoPixel(.25, .59)[3] > 5 || bar[3] < 240 || bar[0] > 10 ||
        bar[1] > 10 || bar[2] > 10 || orange[3] < 240 || orange[2] < 220 || orange[1] < 60 ||
        orange[1] > 150 || orange[0] > 65)
        throw std::runtime_error(
            "Samtec logo lost its transparent cutouts, black bars, orange fill, or proportions.");
    saveBytes(L"samtec-logo-transparent.png", png(logo));
    size_t previousChanged = 0;
    for (int side : {40, 160, 640})
    {
        auto screenshot = Bitmap::create(side, side);
        std::fill(screenshot.pixels.begin(), screenshot.pixels.end(), 255);
        const auto branded = exportImage(screenshot, {}, {false, true});
        const auto disabledLogo = exportImage(screenshot, {}, {false, false});
        size_t changed = 0;
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x)
            {
                const size_t i = (static_cast<size_t>(y) * side + x) * 4;
                if (std::memcmp(&screenshot.pixels[i], &branded.pixels[i], 4))
                {
                    ++changed;
                    if (x < side * .60 || y < side * .65 || x == side - 1 || y == side - 1 ||
                        branded.pixels[i + 3] != 255 || branded.pixels[i + 2] < 8)
                        throw std::runtime_error(
                            "Samtec watermark was misplaced, opaque, or changed image alpha.");
                }
            }
        if (branded.width != side || branded.height != side || changed <= previousChanged ||
            disabledLogo.pixels != screenshot.pixels ||
            decode(png(branded)).pixels != branded.pixels)
            throw std::runtime_error(
                "Samtec watermark default, scaling, dimensions, or PNG round trip failed.");
        previousChanged = changed;
    }
    auto transparentSource = Bitmap::create(640, 320);
    const auto transparentLogo = exportImage(transparentSource, {}, {false, true});
    if (transparentLogo.pixels[3] || decode(png(transparentLogo)).pixels != transparentLogo.pixels)
        throw std::runtime_error("Samtec watermark did not preserve transparent image pixels.");
    const auto brandedLight = exportImage(textSource, borderItems, {false, true});
    const auto borderedLight = exportImage(textSource, borderItems, {true, true});
    const auto brandedDark = exportImage(darkSource, darkItems, {false, true});
    const auto borderedDark = exportImage(darkSource, darkItems, {true, true});
    if (brandedDark.pixels == darkPlain.pixels ||
        decode(png(borderedDark)).pixels != borderedDark.pixels)
        throw std::runtime_error(
            "Samtec Logo did not work on dark screenshots with Professional Border.");
    makeBorderPreview(brandedLight, borderedLight, L"samtec-logo-light-preview.png");
    makeBorderPreview(brandedDark, borderedDark, L"samtec-logo-dark-preview.png");
    saveBytes(L"samtec-logo-snippet.png", png(borderedLight));
    for (const auto &size : {std::pair{1, 1}, std::pair{3, 24}, std::pair{24, 3}})
    {
        auto tiny = Bitmap::create(size.first, size.second);
        std::fill(tiny.pixels.begin(), tiny.pixels.end(), 255);
        const auto exported = exportImage(tiny, {}, {true, true});
        if (exported.width != tiny.width + 40 || exported.height != tiny.height + 40 ||
            decode(png(exported)).pixels != exported.pixels)
            throw std::runtime_error("Samtec Logo failed on a narrow or one-pixel snip.");
    }
    for (int mark : {1, 2})
    {
        const auto &asset = samtecLogo(mark);
        size_t transparent = 0;
        for (size_t i = 3; i < asset.pixels.size(); i += 4)
            transparent += asset.pixels[i] <= 5;
        if (transparent < static_cast<size_t>(asset.width * asset.height) / 10 ||
            asset.width <= asset.height)
            throw std::runtime_error(
                "Tiger or wordmark lost its transparent cutouts or proportions.");
        saveBytes(mark == 1 ? L"samtec-tiger-transparent.png" : L"samtec-wordmark-transparent.png",
                  png(asset));
    }
    for (uint8_t style = 0; style < 6; ++style)
    {
        for (auto size : {std::pair{266, 111}, std::pair{160, 80}, std::pair{40, 40},
                          std::pair{24, 240}, std::pair{240, 24}, std::pair{1, 1}})
        {
            const auto source = Bitmap::create(size.first, size.second);
            const auto bounds = samtecLogoBounds(source, style);
            if (bounds.left < 0 || bounds.top < 0 || bounds.right > source.width ||
                bounds.bottom > source.height || bounds.width() > source.width * .35f + 1 ||
                bounds.height() > source.height * .30f + 1)
                throw std::runtime_error("A small-snippet logo covers too much content or escapes the image.");
            if (size.first == 266 && size.second == 111 &&
                bounds.height() < (style < 4 ? (style % 2 ? 24 : 30) : 18))
                throw std::runtime_error("A small-snippet logo shrank below recognizable size.");
        }
        for (bool dark : {false, true})
        {
            auto source = Bitmap::create(266, 111);
            const Color background = dark ? rgb(45, 50, 60) : rgb(248, 249, 251);
            for (size_t i = 0; i < source.pixels.size(); i += 4)
            {
                source.pixels[i] = (background >> 16) & 255;
                source.pixels[i + 1] = (background >> 8) & 255;
                source.pixels[i + 2] = background & 255;
                source.pixels[i + 3] = 255;
            }
            Annotation text;
            text.kind = Tool::Text;
            text.a = {12, 12};
            text.fontSize = 13;
            text.color = dark ? rgb(233, 237, 244) : rgb(32, 38, 46);
            text.text = L"Preferences reset.\nDefault tools and colors.\nReady to take a snip.";
            measureText(text);
            const auto plain = flatten(source, {text});
            const auto branded = exportImage(source, {text}, {false, true, style});
            int recognizablePixels = 0;
            for (size_t i = 0; i < branded.pixels.size(); i += 4)
                recognizablePixels += std::abs(int(branded.pixels[i]) - int(plain.pixels[i])) >= 25;
            if (recognizablePixels < 40 || branded.width != source.width ||
                branded.height != source.height || decode(png(branded)).pixels != branded.pixels)
                throw std::runtime_error("A small-snippet logo is unreadable or changed export dimensions.");
            saveBytes(L"samtec-small-style-" + std::to_wstring(style + 1) +
                          (dark ? L"-dark.png" : L"-light.png"),
                      png(exportImage(source, {text}, {true, true, style})));
        }
    }
    auto stylesPreview = Bitmap::create(1680, 1920);
    std::fill(stylesPreview.pixels.begin(), stylesPreview.pixels.end(), 255);
    std::vector<Annotation> previewLabels;
    const wchar_t *names[] = {L"S - White badge",        L"S - Soft watermark",
                              L"Tiger - White badge",    L"Tiger - Soft watermark",
                              L"Wordmark - White badge", L"Wordmark - Soft watermark"};
    for (uint8_t style = 0; style < 6; ++style)
    {
        const auto badge = samtecBadge(style);
        if (badge.pixels[3] > 8 || badge.width <= 0 || badge.height <= 0)
            throw std::runtime_error("Logo badge lacks transparent padding for its halo.");
        if (style % 2)
        {
            const auto light = samtecBadge(style, 48, true);
            size_t visible = 0, transparent = 0;
            for (size_t i = 0; i < badge.pixels.size(); i += 4)
            {
                const int alpha = badge.pixels[i + 3];
                visible += alpha >= 40;
                transparent += alpha == 0;
                if (alpha > 83 ||
                    (alpha && (badge.pixels[i] != badge.pixels[i + 1] ||
                               badge.pixels[i] != badge.pixels[i + 2])) ||
                    light.pixels[i + 3] != alpha)
                    throw std::runtime_error(
                        "Soft watermark must be faint, neutral, and free of orange accents.");
            }
            if (visible < 30 || transparent < badge.pixels.size() / 64 ||
                badge.width >= samtecBadge(style - 1).width ||
                badge.height >= samtecBadge(style - 1).height)
                throw std::runtime_error("Soft watermark lacks visible artwork, transparent "
                                         "cutouts, or a compact size: " +
                                         std::to_string(style) + ", visible " +
                                         std::to_string(visible) + ", transparent " +
                                         std::to_string(transparent));
        }
        saveBytes(L"samtec-style-" + std::to_wstring(style + 1) + L".png", png(badge));
        for (int background = 0; background < 3; ++background)
        {
            auto source = Bitmap::create(520, 240);
            for (size_t i = 0; i < source.pixels.size(); i += 4)
            {
                source.pixels[i] = source.pixels[i + 1] = source.pixels[i + 2] =
                    background == 1 ? 24 : 255;
                source.pixels[i + 3] = 255;
            }
            std::vector<Annotation> text;
            Annotation heading;
            heading.kind = Tool::Text;
            heading.a = {16, 18};
            heading.text = L"Quarterly review";
            heading.bold = true;
            heading.fontSize = 20;
            heading.color = background == 1 ? rgb(240, 240, 245) : rgb(35, 39, 56);
            measureText(heading);
            text.push_back(heading);
            if (background == 2)
                for (int row = 0; row < 8; ++row)
                {
                    auto line = heading;
                    line.a = {16, 60.0f + row * 23};
                    line.bold = false;
                    line.fontSize = 14;
                    line.text =
                        L"Review the attached figures and confirm these details for approval.";
                    measureText(line);
                    text.push_back(line);
                }
            const auto plain = flatten(source, text);
            const auto rendered = exportImage(source, text, {false, true, style});
            const auto bordered = exportImage(source, text, {true, true, style});
            if (rendered.width != source.width || rendered.height != source.height ||
                rendered.pixels == plain.pixels ||
                decode(png(rendered)).pixels != rendered.pixels ||
                decode(png(bordered)).pixels != bordered.pixels ||
                exportImage(source, text, {false, false, style}).pixels != plain.pixels)
                throw std::runtime_error(
                    "A Samtec style failed export, border, disabled, or PNG consistency.");
            if (style % 2 && background < 2)
            {
                int greatestContrast = 0;
                for (size_t i = 0; i < rendered.pixels.size(); i += 4)
                {
                    const int change = int(rendered.pixels[i]) - int(plain.pixels[i]);
                    greatestContrast = std::max(greatestContrast, std::abs(change));
                    if ((background == 0 && change < -80) || (background == 1 && change > 80))
                        throw std::runtime_error(
                            "Soft watermark is too prominent on a plain background.");
                }
                if (greatestContrast < 25)
                    throw std::runtime_error(
                        "Soft watermark is unreadable on a light or dark background.");
            }
            for (int y = 0; y < source.height; ++y)
                for (int x = 0; x < source.width; ++x)
                {
                    const size_t i = (static_cast<size_t>(y) * source.width + x) * 4;
                    if (std::memcmp(plain.pixels.data() + i, rendered.pixels.data() + i, 4) &&
                        (x < source.width * .65 || y < source.height * .78 ||
                         rendered.pixels[i + 3] != 255))
                        throw std::runtime_error(
                            "Samtec badge changed pixels outside its bottom-right area.");
                }
            const int left = background * 560 + 20, top = style * 320 + 60;
            for (int y = 0; y < bordered.height; ++y)
                for (int x = 0; x < bordered.width; ++x)
                {
                    const size_t i = (static_cast<size_t>(y) * bordered.width + x) * 4;
                    compositePixel(
                        stylesPreview, left + x, top + y,
                        rgb(bordered.pixels[i + 2], bordered.pixels[i + 1], bordered.pixels[i]),
                        bordered.pixels[i + 3] / 255.0);
                }
            Annotation label = heading;
            label.a = {float(left), float(top - 42)};
            label.text = std::wstring(names[style]) + (background == 0   ? L" / White"
                                                       : background == 1 ? L" / Dark"
                                                                         : L" / Text");
            label.color = rgb(35, 39, 56);
            label.fontSize = 17;
            measureText(label);
            previewLabels.push_back(label);
        }
        for (const auto &size : {std::pair{1, 1}, std::pair{3, 24}, std::pair{24, 3}})
        {
            const auto source = Bitmap::create(size.first, size.second);
            const auto tiny = exportImage(source, {}, {true, true, style});
            if (decode(png(tiny)).pixels != tiny.pixels)
                throw std::runtime_error("A Samtec style failed on a tiny transparent snip.");
        }
    }
    saveBytes(L"samtec-styles-preview.png", png(flatten(stylesPreview, previewLabels)));
    auto preview = Bitmap::create(1600, 360);
    std::fill(preview.pixels.begin(), preview.pixels.end(), 255);
    std::vector<Annotation> samples;
    for (int style = 0; style < 5; ++style)
    {
        Annotation sample;
        sample.kind = Tool::Arrow;
        sample.style = static_cast<uint8_t>(style);
        sample.thickness = 6;
        sample.color = rgb(239, 45, 45);
        sample.a = {60.0f + style * 320, 100};
        sample.b = {270.0f + style * 320, 100};
        if (style == 2)
        {
            sample.a = {885, 40};
            sample.b = {725, 190};
        }
        if (!sample.hit(sample.arrowSpine(.5f), 0) || sample.hit({0, 359}, 0))
            throw std::runtime_error("Arrow artwork hit testing failed.");
        samples.push_back(sample);
        if (style < 3)
        {
            sample.kind = Tool::Line;
            sample.color = rgb(37, 99, 235);
            sample.a = {60.0f + style * 320, 285};
            sample.b = {270.0f + style * 320, 285};
            samples.push_back(sample);
        }
    }
    auto rendered = flatten(preview, samples);
    int runs[3]{};
    for (int style = 0; style < 3; ++style)
    {
        bool previous = false;
        for (int x = 50 + style * 320; x < 280 + style * 320; ++x)
        {
            size_t i = (static_cast<size_t>(285) * rendered.width + x) * 4;
            bool blue = rendered.pixels[i] > 180 && rendered.pixels[i + 1] < 150 &&
                        rendered.pixels[i + 2] < 100;
            if (blue && !previous)
                ++runs[style];
            previous = blue;
        }
        if (style)
        {
            int black = 0, red = 0;
            for (int y = 15; y < 225; ++y)
                for (int x = style * 320; x < (style + 1) * 320; ++x)
                {
                    size_t i = (static_cast<size_t>(y) * rendered.width + x) * 4;
                    if (rendered.pixels[i] < 40 && rendered.pixels[i + 1] < 40 &&
                        rendered.pixels[i + 2] < 40)
                        ++black;
                    if (rendered.pixels[i + 2] > 180 && rendered.pixels[i] < 100)
                        ++red;
                }
            if (black < 10 || red < 40)
                throw std::runtime_error("Outlined arrow export lost its border or fill.");
        }
    }
    if (runs[0] != 1 || runs[1] < 3 || runs[2] <= runs[1])
        throw std::runtime_error("Solid/dashed/dotted line export patterns failed.");
    const size_t shinePixel = (static_cast<size_t>(100) * rendered.width + 1125) * 4;
    if (rendered.pixels[shinePixel + 2] < 200 || rendered.pixels[shinePixel] < 80 ||
        rendered.pixels[shinePixel] > 200)
        throw std::runtime_error("Straight gloss arrow lost its highlight.");
    const Annotation &gloss = samples[samples.size() - 2];
    if (length(gloss.arrowSpine(.5f) - Point{1125, 100}) > .001f || gloss.hit({1125, 140}, 0) ||
        !gloss.hit({1125, 100}, 0))
        throw std::runtime_error("Straight gloss arrow geometry or selection failed.");
    int glossBorder = 0, glossFill = 0;
    for (int y = 60; y < 145; ++y)
        for (int x = 1000; x < 1250; ++x)
        {
            const size_t i = (static_cast<size_t>(y) * rendered.width + x) * 4;
            if (rendered.pixels[i] < 40 && rendered.pixels[i + 1] < 40 &&
                rendered.pixels[i + 2] < 40)
                ++glossBorder;
            if (rendered.pixels[i + 2] > 180 && rendered.pixels[i] < 100)
                ++glossFill;
        }
    if (glossBorder < 10 || glossFill < 40 || decode(png(rendered)).pixels != rendered.pixels)
        throw std::runtime_error("Straight gloss arrow border/fill or PNG round trip failed.");
    const Annotation &block = samples.back();
    // Near the tail, the block's wide shaft is filled where the tapered style is empty.
    const Point tailInside{1345, 92}, tailOutside{1330, 100};
    if (!block.hit(tailInside, 0) || block.hit(tailOutside, 0) ||
        rendered.sample(tailInside) != block.color ||
        length(block.arrowSpine(.5f) - Point{1445, 100}) > .001f)
        throw std::runtime_error("Block gloss arrow shaft, flat tail, or hit testing failed.");
    const size_t blockHighlight = (static_cast<size_t>(100) * rendered.width + 1445) * 4;
    const auto tailBorder = rendered.sample({1340, 92});
    if (!tailBorder || (*tailBorder & 255) > 40 || rendered.pixels[blockHighlight + 2] < 200 ||
        rendered.pixels[blockHighlight] < 80 || rendered.pixels[blockHighlight] > 200)
        throw std::runtime_error("Block gloss arrow lost its square outline or highlight.");
    Annotation rotated = block;
    rotated.a = {80, 80};
    rotated.b = {20, 200};
    if (!rotated.hit(rotated.arrowSpine(.3f), 0) ||
        rotated.hit(rotated.a - (rotated.b - rotated.a) * .1f, 0))
        throw std::runtime_error("Rotated block gloss arrow selection failed.");
    for (float distance : {0.0f, 1.0f, 8.0f})
    {
        rotated.b = rotated.a + Point{distance, 0};
        const auto tinyArrow = flatten(source, {rotated});
        if (decode(png(tinyArrow)).pixels != tinyArrow.pixels)
            throw std::runtime_error("Tiny block gloss arrow export failed.");
    }
    saveBytes(L"annotation-style-preview.png", png(rendered));
}
} // namespace snip
