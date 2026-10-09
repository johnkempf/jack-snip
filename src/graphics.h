#pragma once
#include "model.h"
#include "windows_support.h"
#include "capture.h"
#include "clipboard.h"
#include "file_io.h"
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <string>
#include <utility>
#include <array>

namespace snip
{
template <class T> class Com
{
    T *p_ = nullptr;

  public:
    Com() = default;
    ~Com() { reset(); }
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    Com(Com &&other) noexcept : p_(std::exchange(other.p_, nullptr)) {}
    Com &operator=(Com &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            p_ = std::exchange(other.p_, nullptr);
        }
        return *this;
    }
    T *get() const { return p_; }
    T *operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    T **put()
    {
        reset();
        return &p_;
    }
    void reset()
    {
        if (p_)
        {
            p_->Release();
            p_ = nullptr;
        }
    }
};
D2D1_COLOR_F color(Color value, float alpha = 1);
Color textBackground(Color foreground);
struct ExportOptions
{
    bool professionalBorder = false;
    bool samtecLogo = false;
    uint8_t samtecStyle = 0;
    bool professionalBlur = true;
    bool professionalRounded = true;
    bool jpg = false;
    bool operator==(const ExportOptions &) const = default;
};
class Graphics
{
    std::array<Bitmap, 3> samtecLogos_;
    const Bitmap &samtecLogo(int mark = 0);
    void applySamtecLogo(Bitmap &image, uint8_t style);

  public:
    Com<ID2D1Factory> factory;
    Com<IDWriteFactory> textFactory;
    Com<IDWriteTextFormat> font, smallFont, titleFont, labelFont;
    std::wstring annotationFontFamily = L"Segoe UI";
    Com<ID2D1StrokeStyle> roundStroke, dashStroke, dotStroke;
    void initialize();
    void drawAnnotations(ID2D1RenderTarget *target, const std::vector<Annotation> &items,
                         int editingText = -1);
    Com<IDWriteTextLayout> textLayout(const Annotation &item);
    void measureText(Annotation &item);
    Bitmap flatten(const Bitmap &image, const std::vector<Annotation> &items, int editingText = -1);
    Bitmap flattenRegion(const Bitmap &image, const std::vector<Annotation> &items, int editingText,
                         int x, int y, int width, int height);
    Rect samtecLogoBounds(const Bitmap &image, uint8_t style);
    // All export destinations use this pipeline; flatten remains the unstyled editing image.
    Bitmap exportImage(const Bitmap &image, const std::vector<Annotation> &items,
                       const ExportOptions &options = {}, int editingText = -1);
    std::vector<uint8_t> png(const Bitmap &bitmap);
    // JPEG has no alpha; transparent edges and shadows are composited onto white.
    std::vector<uint8_t> jpeg(const Bitmap &bitmap);
    Bitmap decode(const std::vector<uint8_t> &bytes);
    Bitmap samtecBadge(uint8_t style, int logoHeight = 48, bool lightWatermark = false);
    void test();
};
} // namespace snip
