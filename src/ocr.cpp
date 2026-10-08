#include "ocr.h"
#include "graphics.h"
#include "ocr_layout.h"
#include <roapi.h>
#include <winstring.h>
#include <asyncinfo.h>
#ifdef __MINGW32__
// MinGW's projection specializes IReference twice for BYTE/boolean (both unsigned
// char). We do not use that specialization; skip it without changing the toolchain.
#define ____FIReference_1_boolean_INTERFACE_DEFINED__
#endif
#include <windows.graphics.imaging.h>
#include <windows.storage.streams.h>
#include <robuffer.h>
#include <cstring>
#include <array>
#include <climits>
#include <thread>

namespace snip
{
namespace
{
// The bundled MinGW headers omit Windows.Media.Ocr. These minimal ABI declarations
// follow the Windows SDK interfaces (also published in microsoft/windows-rs):
// https://github.com/microsoft/windows-rs/blob/master/crates/libs/windows/src/Windows/Media/Ocr/mod.rs
// Keep WinRT behind this module; no SDK projection or extra runtime is required.
struct OcrLine : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_Words(IInspectable **) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Text(HSTRING *) = 0;
};
struct OcrLines : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32, OcrLine **) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32 *) = 0;
    virtual HRESULT STDMETHODCALLTYPE IndexOf(OcrLine *, UINT32 *, boolean *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32, UINT32, OcrLine **, UINT32 *) = 0;
};
struct OcrResult : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_Lines(OcrLines **) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TextAngle(IInspectable **) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Text(HSTRING *) = 0;
};
struct OcrOperation : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown *) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown **) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(OcrResult **) = 0;
};
struct OcrEngine : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE
    RecognizeAsync(ABI::Windows::Graphics::Imaging::ISoftwareBitmap *, OcrOperation **) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RecognizerLanguage(IInspectable **) = 0;
};
struct OcrStatics : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_MaxImageDimension(UINT32 *) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AvailableRecognizerLanguages(IInspectable **) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsLanguageSupported(IInspectable *, boolean *) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryCreateFromLanguage(IInspectable *, OcrEngine **) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryCreateFromUserProfileLanguages(OcrEngine **) = 0;
};
constexpr GUID OcrStaticsId = {
    0x5bffa85a, 0x3384, 0x3540, {0x99, 0x40, 0x69, 0x91, 0x20, 0xd4, 0x28, 0xa8}};
struct Runtime
{
    Runtime() { check(RoInitialize(RO_INIT_MULTITHREADED), "Cannot initialize text recognition."); }
    ~Runtime() { RoUninitialize(); }
};
struct RuntimeString
{
    HSTRING value = nullptr;
    ~RuntimeString() { WindowsDeleteString(value); }
};
template <class T> Com<T> factory(const wchar_t *name, const GUID &id)
{
    RuntimeString text;
    check(WindowsCreateString(name, static_cast<UINT32>(wcslen(name)), &text.value),
          "Cannot prepare text recognition.");
    Com<T> result;
    check(RoGetActivationFactory(text.value, id, reinterpret_cast<void **>(result.put())),
          "Windows text recognition is unavailable.");
    return result;
}

Bitmap prepareImage(const Bitmap &image, UINT32 limit, std::stop_token stop,
                    double smallScale = 3.0)
{
    // OCR needs whitespace around a line, even when the user selected it precisely.
    // Add only generated pixels; never read text outside the selected screen area.
    const int padding = std::min(32, static_cast<int>(limit / 4));
    const int available = static_cast<int>(limit) - padding * 2;
    const bool small = image.height <= 128;
    const double desiredScale = small ? smallScale : 1.0;
    const double scale = std::min(desiredScale, static_cast<double>(available) /
                                                    std::max(image.width, image.height));
    const int contentWidth = std::max(1, static_cast<int>(image.width * scale)),
              contentHeight = std::max(1, static_cast<int>(image.height * scale));
    const int width = contentWidth + padding * 2, height = contentHeight + padding * 2;

    std::array<size_t, 256> histogram{};
    int low = 255, high = 0;
    bool uniform = true;
    auto grayAt = [&](int x, int y) {
        x = std::clamp(x, 0, image.width - 1);
        y = std::clamp(y, 0, image.height - 1);
        const auto pixel = &image.pixels[(static_cast<size_t>(y) * image.width + x) * 4];
        return (pixel[2] * 299 + pixel[1] * 587 + pixel[0] * 114) / 1000.0;
    };
    for (int y = 0; y < image.height; ++y)
    {
        if (stop.stop_requested())
            return {};
        for (int x = 0; x < image.width; ++x)
        {
            const int gray = static_cast<int>(grayAt(x, y));
            ++histogram[gray];
            low = std::min(low, gray);
            high = std::max(high, gray);
            const auto p = &image.pixels[(static_cast<size_t>(y) * image.width + x) * 4];
            uniform = uniform && std::memcmp(p, image.pixels.data(), 3) == 0;
        }
    }
    if (uniform)
        return {}; // Uniform captures contain no glyphs.
    const int background =
        static_cast<int>(std::max_element(histogram.begin(), histogram.end()) - histogram.begin());
    // Normalize small labels with a mostly uniform background. Keep mixed content
    // in its original colors so a dark panel and a light panel can coexist.
    size_t backgroundPixels = 0;
    for (int gray = std::max(0, background - 8); gray <= std::min(255, background + 8); ++gray)
        backgroundPixels += histogram[gray];
    const bool normalize = small && high > low &&
                           backgroundPixels >= static_cast<size_t>(image.width) * image.height / 2;
    const bool invert = background < (low + high) / 2;
    size_t ink = 0, solidInk = 0;
    if (normalize && invert)
    {
        for (int gray = low; gray <= high; ++gray)
        {
            const double strength = static_cast<double>(gray - low) / (high - low);
            if (strength > .35)
                ink += histogram[gray];
            if (strength > .8)
                solidInk += histogram[gray];
        }
    }
    // Bright bold screen fonts have thick antialiased edges after inversion.
    // Lighten those edges to keep adjacent stems and counters separate. Thin
    // fonts retain their original coverage (especially punctuation and digits).
    const double gamma = ink && static_cast<double>(solidInk) / ink > .6 ? .25 : 1.0;
    auto pixels = Bitmap::create(width, height);
    std::fill(pixels.pixels.begin(), pixels.pixels.end(), 255);
    if (!normalize)
    {
        // Pick an actual background pixel rather than averaging colored text into it.
        const uint8_t *color = image.pixels.data();
        for (size_t i = 0; i < image.pixels.size(); i += 4)
        {
            const auto p = &image.pixels[i];
            if ((p[2] * 299 + p[1] * 587 + p[0] * 114) / 1000 == background)
            {
                color = p;
                break;
            }
        }
        for (size_t i = 0; i < pixels.pixels.size(); i += 4)
            std::memcpy(&pixels.pixels[i], color, 3);
    }
    for (int y = 0; y < contentHeight; ++y)
    {
        if (stop.stop_requested())
            return {};
        const double sy = (y + .5) / scale - .5;
        const int iy = static_cast<int>(std::floor(sy));
        const double fy = sy - iy;
        for (int x = 0; x < contentWidth; ++x)
        {
            const double sx = (x + .5) / scale - .5;
            const int ix = static_cast<int>(std::floor(sx));
            const double fx = sx - ix;
            auto target =
                &pixels.pixels[(static_cast<size_t>(y + padding) * width + x + padding) * 4];
            if (normalize)
            {
                double gray = (1 - fy) * ((1 - fx) * grayAt(ix, iy) + fx * grayAt(ix + 1, iy)) +
                              fy * ((1 - fx) * grayAt(ix, iy + 1) + fx * grayAt(ix + 1, iy + 1));
                gray = std::clamp((gray - low) / (high - low), 0.0, 1.0);
                if (invert)
                    gray = 1 - gray;
                const auto value = static_cast<uint8_t>(std::round(255 * std::pow(gray, gamma)));
                target[0] = target[1] = target[2] = value;
            }
            else
            {
                for (int c = 0; c < 3; ++c)
                {
                    auto channel = [&](int px, int py) {
                        px = std::clamp(px, 0, image.width - 1);
                        py = std::clamp(py, 0, image.height - 1);
                        return image.pixels[(static_cast<size_t>(py) * image.width + px) * 4 + c];
                    };
                    target[c] = static_cast<uint8_t>(std::round(
                        (1 - fy) * ((1 - fx) * channel(ix, iy) + fx * channel(ix + 1, iy)) +
                        fy * ((1 - fx) * channel(ix, iy + 1) + fx * channel(ix + 1, iy + 1))));
                }
            }
        }
    }
    return pixels;
}
std::wstring recognizePrepared(OcrEngine *engine, const Bitmap &pixels, std::stop_token stop,
                               ULONGLONG deadline)
{
    if (pixels.empty() || stop.stop_requested())
        return {};
    const int width = pixels.width, height = pixels.height;
    using namespace ABI::Windows::Graphics::Imaging;
    using namespace ABI::Windows::Storage::Streams;
    auto bitmapFactory = factory<ISoftwareBitmapFactory>(L"Windows.Graphics.Imaging.SoftwareBitmap",
                                                         __uuidof(ISoftwareBitmapFactory));
    Com<ISoftwareBitmap> bitmap;
    check(bitmapFactory->CreateWithAlpha(BitmapPixelFormat_Bgra8, width, height,
                                         BitmapAlphaMode_Ignore, bitmap.put()),
          "Cannot prepare the image for text recognition.");
    auto bufferFactory =
        factory<IBufferFactory>(L"Windows.Storage.Streams.Buffer", __uuidof(IBufferFactory));
    Com<IBuffer> buffer;
    const auto bytes = static_cast<UINT32>(pixels.pixels.size());
    check(bufferFactory->Create(bytes, buffer.put()), "Cannot allocate text recognition pixels.");
    check(buffer->put_Length(bytes), "Cannot size text recognition pixels.");
    Com<::Windows::Storage::Streams::IBufferByteAccess> access;
    check(buffer->QueryInterface(__uuidof(::Windows::Storage::Streams::IBufferByteAccess),
                                 reinterpret_cast<void **>(access.put())),
          "Cannot access text recognition pixels.");
    BYTE *data = nullptr;
    check(access->Buffer(&data), "Cannot access text recognition pixels.");
    std::memcpy(data, pixels.pixels.data(), bytes);
    check(bitmap->CopyFromBuffer(buffer.get()), "Cannot load text recognition pixels.");
    if (stop.stop_requested())
        return {};
    Com<OcrOperation> operation;
    check(engine->RecognizeAsync(bitmap.get(), operation.put()),
          "Cannot read text from this image.");
    Com<IAsyncInfo> info;
    check(operation->QueryInterface(__uuidof(IAsyncInfo), reinterpret_cast<void **>(info.put())),
          "Cannot monitor text recognition.");
    AsyncStatus state = Started;
    while (state == Started)
    {
        if (stop.stop_requested() || GetTickCount64() >= deadline)
        {
            info->Cancel();
            if (stop.stop_requested())
                return {};
            throw std::runtime_error(
                "Text recognition timed out. Select a smaller area and try again.");
        }
        check(info->get_Status(&state), "Cannot monitor text recognition.");
        if (state == Started)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (state != Completed)
    {
        HRESULT failure = E_FAIL;
        info->get_ErrorCode(&failure);
        check(FAILED(failure) ? failure : E_FAIL, "Windows could not recognize this text.");
    }
    Com<OcrResult> result;
    check(operation->GetResults(result.put()), "Cannot retrieve recognized text.");
    Com<OcrLines> lines;
    check(result->get_Lines(lines.put()), "Cannot retrieve recognized text lines.");
    UINT32 count = 0;
    check(lines->get_Size(&count), "Cannot count recognized text lines.");
    std::wstring text;
    for (UINT32 i = 0; i < count; ++i)
    {
        Com<OcrLine> line;
        check(lines->GetAt(i, line.put()), "Cannot retrieve a recognized text line.");
        RuntimeString value;
        check(line->get_Text(&value.value), "Cannot retrieve a recognized text line.");
        UINT32 length = 0;
        const wchar_t *characters = WindowsGetStringRawBuffer(value.value, &length);
        if (length)
        {
            if (!text.empty())
                text += L"\r\n";
            text.append(characters, length);
        }
    }
    info->Close();
    return text;
}
} // namespace

std::wstring recognizeText(const Bitmap &image, std::stop_token stop)
{
    if (image.empty() || stop.stop_requested())
        return {};
    auto layout = analyzeTextRows(image, stop);
    if (stop.stop_requested())
        return {};
    if (layout.reliable && std::none_of(layout.rows.begin(), layout.rows.end(),
                                        [](const TextRow &row) { return !row.clipped; }))
        return {};
    const auto &source = layout.image.empty() ? image : layout.image;
    Runtime runtime;
    auto statics = factory<OcrStatics>(L"Windows.Media.Ocr.OcrEngine", OcrStaticsId);
    Com<OcrEngine> engine;
    check(statics->TryCreateFromUserProfileLanguages(engine.put()),
          "Cannot start Windows text recognition.");
    if (!engine)
        throw std::runtime_error(
            "Install an OCR language in Windows Settings > Time & language > Language & region.");
    UINT32 limit = 0;
    check(statics->get_MaxImageDimension(&limit), "Cannot read the text recognition size limit.");
    if (limit < 4 || limit > static_cast<UINT32>(INT_MAX))
        throw std::runtime_error("Windows text recognition returned an invalid image limit.");
    const auto deadline = GetTickCount64() + 15000;
    // Keep the user's original margins: changing line geometry can worsen
    // Windows OCR even with identical letter pixels. Generated padding handles
    // tight selections; clipped neighboring rows have already been removed.
    auto text = recognizePrepared(engine.get(), prepareImage(source, limit, stop), stop, deadline);
    if (!text.empty() || stop.stop_requested())
        return stop.stop_requested() ? L"" : text;
    const auto completeRows = std::count_if(layout.rows.begin(), layout.rows.end(),
                                            [](const TextRow &row) { return !row.clipped; });
    // Retry a few complete rows only when Windows detected nothing. This helps
    // tiny labels inside a large selection without choosing between conflicting
    // readings, correcting identifiers, or multiplying the operation timeout.
    if (layout.reliable && completeRows <= 4)
    {
        for (const auto &row : layout.rows)
        {
            if (stop.stop_requested())
                return {};
            if (row.clipped)
                continue;
            auto line = recognizePrepared(engine.get(),
                prepareImage(source.crop(0, row.top, source.width, row.bottom - row.top), limit, stop, 2.0), stop, deadline);
            if (!line.empty())
            {
                if (!text.empty())
                    text += L"\r\n";
                text += line;
            }
        }
        return stop.stop_requested() ? L"" : text;
    }
    return {};
}
} // namespace snip
