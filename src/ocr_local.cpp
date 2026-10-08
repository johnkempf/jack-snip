#include "ocr_local.h"
#include "windows_support.h"
#include <windows.h>
#include <stdexcept>

// The C ABI keeps the engine's C++ headers and types out of the app. The exact
// declarations are from Tesseract 5.5.3 include/tesseract/capi.h.
extern "C"
{
    void *TessBaseAPICreate();
    void TessBaseAPIDelete(void *);
    int TessBaseAPIInit5(void *, const char *, int, const char *, int, char **, int, char **,
                         char **, size_t, int);
    int TessBaseAPISetVariable(void *, const char *, const char *);
    void TessBaseAPISetPageSegMode(void *, int);
    void TessBaseAPISetImage(void *, const unsigned char *, int, int, int, int);
    int TessBaseAPIRecognize(void *, void *);
    char *TessBaseAPIGetUTF8Text(void *);
    void TessDeleteText(char *);
    void *TessMonitorCreate();
    void TessMonitorDelete(void *);
    void TessMonitorSetCancelFunc(void *, bool (*)(void *, int));
    void TessMonitorSetCancelThis(void *, void *);
    void TessMonitorSetDeadlineMSecs(void *, int);
}
namespace snip
{
struct LocalTextEngine::State
{
    void *engine = TessBaseAPICreate();
    ~State()
    {
        if (engine)
            TessBaseAPIDelete(engine);
    }
};
LocalTextEngine::LocalTextEngine() : state_(std::make_unique<State>())
{
    auto module = GetModuleHandleW(nullptr);
    auto resource = FindResourceW(module, MAKEINTRESOURCEW(204), RT_RCDATA);
    auto loaded = resource ? LoadResource(module, resource) : nullptr;
    const auto data = loaded ? static_cast<const char *>(LockResource(loaded)) : nullptr;
    const auto size = resource ? SizeofResource(module, resource) : 0;
    if (!state_->engine || !data || !size ||
        TessBaseAPIInit5(state_->engine, data, static_cast<int>(size), "eng", 1, nullptr, 0,
                         nullptr, nullptr, 0, FALSE))
        throw std::runtime_error("Cannot load the bundled text recognition model.");
    TessBaseAPISetVariable(state_->engine, "debug_file", "NUL");
    TessBaseAPISetPageSegMode(state_->engine,
                              7); // A single complete text row, including one glyph.
}
LocalTextEngine::~LocalTextEngine() = default;
std::wstring LocalTextEngine::readLine(const Bitmap &image, std::stop_token stop)
{
    if (image.empty() || stop.stop_requested())
        return {};
    std::vector<uint8_t> rgb(static_cast<size_t>(image.width) * image.height * 3);
    for (size_t i = 0, j = 0; i < image.pixels.size(); i += 4, j += 3)
    {
        rgb[j] = image.pixels[i + 2];
        rgb[j + 1] = image.pixels[i + 1];
        rgb[j + 2] = image.pixels[i];
    }
    TessBaseAPISetImage(state_->engine, rgb.data(), image.width, image.height, 3, image.width * 3);
    void *monitor = TessMonitorCreate();
    if (!monitor)
        throw std::runtime_error("Cannot monitor text recognition.");
    auto cancel = [](void *context, int) {
        return static_cast<std::stop_token *>(context)->stop_requested();
    };
    TessMonitorSetCancelFunc(monitor, cancel);
    TessMonitorSetCancelThis(monitor, &stop);
    TessMonitorSetDeadlineMSecs(monitor, 15000);
    const int status = TessBaseAPIRecognize(state_->engine, monitor);
    TessMonitorDelete(monitor);
    if (stop.stop_requested())
        return {};
    if (status)
        throw std::runtime_error(
            "Text recognition timed out. Select a smaller area and try again.");
    char *characters = TessBaseAPIGetUTF8Text(state_->engine);
    if (!characters)
        return {};
    const int length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, characters, -1, nullptr, 0);
    std::wstring text(length ? length - 1 : 0, L'\0');
    if (length)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, characters, -1, text.data(), length);
    TessDeleteText(characters);
    const auto last = text.find_last_not_of(L" \t\r\n");
    if (last == std::wstring::npos)
        return {};
    text.resize(last + 1);
    return text;
}
} // namespace snip
