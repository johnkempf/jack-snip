// Real offline OCR and clipboard integration on a private, noninteractive desktop.
#include "../src/main.cpp"
#include <iostream>
#include <cstring>
#include <fstream>
#include <iterator>
#include <tuple>
#include <io.h>
#include <fcntl.h>

Bitmap smallTextSample(const std::wstring &text, int height, bool bold, COLORREF foreground,
                       COLORREF background)
{
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontW(-height, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto previousFont = SelectObject(dc, font);
    SIZE extent{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &extent);
    auto sample = Bitmap::create(extent.cx + 4, extent.cy + 4);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = sample.width;
    info.bmiHeader.biHeight = -sample.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *pixels = nullptr;
    HBITMAP surface = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!surface || !pixels)
    {
        SelectObject(dc, previousFont);
        DeleteObject(font);
        DeleteDC(dc);
        throw std::runtime_error("Cannot render small OCR sample.");
    }
    auto previousSurface = SelectObject(dc, surface);
    HBRUSH brush = CreateSolidBrush(background);
    RECT area{0, 0, sample.width, sample.height};
    FillRect(dc, &area, brush);
    DeleteObject(brush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, foreground);
    TextOutW(dc, 2, 2, text.c_str(), static_cast<int>(text.size()));
    GdiFlush();
    std::memcpy(sample.pixels.data(), pixels, sample.pixels.size());
    for (size_t i = 3; i < sample.pixels.size(); i += 4)
        sample.pixels[i] = 255;
    SelectObject(dc, previousFont);
    SelectObject(dc, previousSurface);
    DeleteObject(surface);
    DeleteObject(font);
    DeleteDC(dc);
    return sample;
}

void checkTextShortcuts()
{
    auto require = [](bool condition, const char *message) {
        if (!condition)
            throw std::runtime_error(message);
    };
    const WORD areaKey = MAKEWORD(VK_F20, HOTKEYF_CONTROL | HOTKEYF_ALT),
               fullKey = MAKEWORD(VK_F21, HOTKEYF_CONTROL | HOTKEYF_ALT),
               textKey = MAKEWORD(VK_F22, HOTKEYF_CONTROL | HOTKEYF_ALT),
               blockedKey = MAKEWORD(VK_F23, HOTKEYF_CONTROL | HOTKEYF_ALT);
    std::wstring failure;
    if (!registerShortcuts(areaKey, fullKey, false, &failure, textKey))
    {
        std::wcerr << failure << L"\n";
        throw std::runtime_error("Cannot register text shortcut.");
    }
    require(!registerShortcuts(areaKey, fullKey, false, nullptr, areaKey),
            "Duplicate text shortcut was accepted.");
    require(RegisterHotKey(app.window, 99, hotkeyModifiers(blockedKey), LOBYTE(blockedKey)),
            "Cannot occupy test shortcut.");
    const bool rollback = !registerShortcuts(areaKey, fullKey, false, nullptr, blockedKey) &&
                          app.textHotkey == textKey && app.textHotkeyRegistered &&
                          app.hotkeyRegistered && app.instantHotkeyRegistered;
    UnregisterHotKey(app.window, 99);
    require(rollback, "Unavailable text shortcut discarded existing shortcuts.");
    require(registerShortcuts(textKey, fullKey, false, nullptr, areaKey),
            "Cannot swap text and image shortcuts.");
    app.shortcutsDirty = true;
    require(saveToolPreferences() &&
                preferenceUInt(app.iniPath, L"Settings", L"TextHotkey", 0) == areaKey,
            "Text shortcut did not persist.");
    require(registerShortcuts(0, 0, false, nullptr, 0) && !app.textHotkeyRegistered,
            "Text shortcut could not be disabled.");
}

int wmain()
{
    _setmode(_fileno(stdout), _O_U8TEXT);
    auto originalStation = GetProcessWindowStation();
    auto originalDesktop = GetThreadDesktop(GetCurrentThreadId());
    HWINSTA station = nullptr;
    HDESK desktop = nullptr;
    bool com = false;
    int exitCode = 0;
    auto require = [](bool condition, const char *message) {
        if (!condition)
            throw std::runtime_error(message);
    };
    try
    {
        station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
        require(station && SetProcessWindowStation(station), "Cannot isolate the clipboard.");
        desktop = CreateDesktopW(L"TigerSnipTextCaptureTest", nullptr, nullptr, 0,
                                 DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU | DESKTOP_READOBJECTS |
                                     DESKTOP_WRITEOBJECTS | DESKTOP_HOOKCONTROL,
                                 nullptr);
        require(desktop && SetThreadDesktop(desktop), "Cannot attach the test desktop.");
        check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Cannot initialize COM.");
        com = true;
        app.instance = GetModuleHandleW(nullptr);
        app.iniPath = (std::filesystem::current_path() / L"text-capture-settings.ini").wstring();
        app.softwareRendering = true;
        app.autoCopy = false;
        registerClasses();
        require(CreateWindowExW(0, MainClass, L"Text capture test", WS_OVERLAPPEDWINDOW, 0, 0, 1000,
                                700, nullptr, createMenu(), app.instance, nullptr),
                "Cannot create the test editor.");
        auto clipboardText = [&] {
            require(OpenClipboard(app.window), "Cannot read the private clipboard.");
            HGLOBAL memory = GetClipboardData(CF_UNICODETEXT);
            auto characters = static_cast<const wchar_t *>(memory ? GlobalLock(memory) : nullptr);
            std::wstring result = characters ? characters : L"";
            if (characters)
                GlobalUnlock(memory);
            CloseClipboard();
            return result;
        };
        require(copyText(app.window, L"Previous clipboard \u03A9\r\nSecond line"),
                "Cannot seed clipboard.");
        require(clipboardText() == L"Previous clipboard \u03A9\r\nSecond line",
                "Unicode clipboard lost text or line breaks.");
        require(!copyText(app.window, L"") &&
                    clipboardText() == L"Previous clipboard \u03A9\r\nSecond line",
                "An empty copy replaced the clipboard.");

        auto image = Bitmap::create(900, 160);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = image.width;
        info.bmiHeader.biHeight = -image.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void *pixels = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP surface = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        require(surface && pixels, "Cannot create OCR sample.");
        auto old = SelectObject(dc, surface);
        RECT area{0, 0, image.width, image.height};
        FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        HFONT font = CreateFontW(-36, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                 DEFAULT_PITCH, L"Segoe UI");
        auto oldFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(0, 0, 0));
        const std::wstring expected = L"Order number 123425-5\r\nSecond line";
        TextOutW(dc, 24, 20, L"Order number 123425-5", 21);
        TextOutW(dc, 24, 84, L"Second line", 11);
        GdiFlush();
        std::memcpy(image.pixels.data(), pixels, image.pixels.size());
        SelectObject(dc, oldFont);
        SelectObject(dc, old);
        DeleteObject(font);
        DeleteObject(surface);
        DeleteDC(dc);

        acceptCapture(Bitmap::create(60, 40));
        app.dirty = true;
        const auto previousPixels = app.image.pixels;
        const auto recentCount = app.recent.size();
        auto waitRecognition = [&] {
            const auto deadline = GetTickCount64() + 20000;
            while (app.textResult.valid() && GetTickCount64() < deadline)
            {
                completeTextRecognition();
                Sleep(20);
            }
            require(!app.textResult.valid(), "OCR completion never arrived.");
        };
        auto noticeTitle = [&] {
            wchar_t label[100]{};
            GetWindowTextW(app.textNotice.window(), label, 100);
            return std::wstring(label);
        };
        ShowWindow(app.window, SW_HIDE);
        app.textCapture = true;
        app.textEditorWasVisible = false;
        app.desktop = image;
        app.selectionStart = {0, 0};
        app.selectionEnd = {image.width, image.height};
        finishCapture();
        require(!IsWindowVisible(app.window), "Text capture opened the editor.");
        waitRecognition();
        if (clipboardText() != expected)
        {
            std::wcerr << L"OCR actual: [" << clipboardText() << L"]\n";
            throw std::runtime_error("OCR did not preserve the order number and separate lines.");
        }
        require(noticeTitle() == L"Text copied",
                "Successful OCR did not show copied confirmation.");
        auto renderNotice = [&](const wchar_t *filename) {
            RECT bounds{};
            GetClientRect(app.textNotice.window(), &bounds);
            auto rendered = Bitmap::create(bounds.right, bounds.bottom);
            BITMAPINFO bitmapInfo{};
            bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmapInfo.bmiHeader.biWidth = rendered.width;
            bitmapInfo.bmiHeader.biHeight = -rendered.height;
            bitmapInfo.bmiHeader.biPlanes = 1;
            bitmapInfo.bmiHeader.biBitCount = 32;
            HDC renderDC = CreateCompatibleDC(nullptr);
            void *renderPixels = nullptr;
            HBITMAP renderSurface =
                CreateDIBSection(renderDC, &bitmapInfo, DIB_RGB_COLORS, &renderPixels, nullptr, 0);
            require(renderSurface && renderPixels, "Cannot render notice for visual verification.");
            auto previous = SelectObject(renderDC, renderSurface);
            SendMessageW(app.textNotice.window(), WM_PRINTCLIENT,
                         reinterpret_cast<WPARAM>(renderDC), PRF_CLIENT);
            GdiFlush();
            std::memcpy(rendered.pixels.data(), renderPixels, rendered.pixels.size());
            for (size_t i = 3; i < rendered.pixels.size(); i += 4)
                rendered.pixels[i] = 255;
            SelectObject(renderDC, previous);
            DeleteObject(renderSurface);
            DeleteDC(renderDC);
            saveBytes(filename, app.graphics.png(rendered));
        };
        renderNotice(L"text-preview-light.png");
        require(GetWindowLongPtrW(app.textNotice.window(), GWL_EXSTYLE) & WS_EX_NOACTIVATE,
                "Text preview can steal focus.");
        require(app.image.pixels == previousPixels && app.recent.size() == recentCount && app.dirty,
                "Text capture replaced the open snip or editing state.");
        // These real 16-28px screen crops all returned no text at native size.
        // Exercise the selection -> OCR -> Unicode clipboard path, not just a
        // preprocessing helper. Exact expectations catch lost punctuation and
        // the O/0, l/1 mistakes that padding alone did not fix.
        for (auto [filename, expectedText] :
             std::array<std::pair<const wchar_t *, const wchar_t *>, 20>{
                 {{L"part-number.png", L"RSP-241492-01"},
                  {L"serialized.png", L"Serialized:"},
                  {L"serial-number.png", L"1301558"},
                  {L"marked-empty.png", L"Marked Empty"},
                  {L"parcels-area.png", L"683: Empty Parcels Area"},
                  {L"quantity-and-zero.png", L"CurrentQuantity"},
                  {L"hyphenated-part.png", L"APF6-037-01-04-RA"},
                  {L"clipped-date-row.png", L"3/31/2025 2:38 PM"},
                  {L"red-part-number.png", L"RSP-241492-01"},
                  {L"due-date-status.png", L"(Friday) 10/16/2026\r\nNot completed yet."},
                  {L"selected-serial.png", L"36808975"},
                  {L"selected-part-margins.png", L"APF6-037-01-04-RA"},
                  {L"serial-in-field.png", L"2007372"},
                  {L"red-part-in-frame.png", L"RSP-241492-01"},
                  {L"serial-with-fragments.png", L"801099"},
                  {L"condensed-dotted-part.png", L"ADM6-100-01.5-4-A"},
                  {L"barcode-number.png", L"39919487"},
                  {L"barcode-number-tight.png", L"36808975"},
                  {L"small-part-number.png", L"RSP-241492-01"},
                  {L"selected-serial-focus-border.png", L"36808975"}}})
        {
            const auto path =
                std::filesystem::path(__FILE__).parent_path() / "fixtures" / "ocr" / filename;
            std::ifstream file(path, std::ios::binary);
            require(file.good(), "Cannot open a tight-crop OCR fixture.");
            const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
            const auto fixture = app.graphics.decode(bytes);
            auto surrounding = Bitmap::create(fixture.width + 40, fixture.height + 40);
            std::fill(surrounding.pixels.begin(), surrounding.pixels.end(), 255);
            for (int y = 0; y < fixture.height; ++y)
                std::memcpy(
                    &surrounding.pixels[(static_cast<size_t>(y + 20) * surrounding.width + 20) * 4],
                    &fixture.pixels[static_cast<size_t>(y) * fixture.width * 4],
                    static_cast<size_t>(fixture.width) * 4);
            // Unselected pixels differ sharply from the label's background.
            // Internal margins must not accidentally include that outside area.
            app.desktop = std::move(surrounding);
            app.textCapture = true;
            app.textEditorWasVisible = false;
            app.selectionStart = {20, 20};
            app.selectionEnd = {20 + fixture.width, 20 + fixture.height};
            finishCapture();
            waitRecognition();
            if (clipboardText() != expectedText || noticeTitle() != L"Text copied")
            {
                std::cerr << "OCR character codes: ";
                for (wchar_t c : clipboardText())
                    std::cerr << std::hex << static_cast<unsigned>(c) << ' ';
                std::cerr << std::dec << '\n';
                std::wcerr << filename << L": expected [" << expectedText << L"], got ["
                           << clipboardText() << L"]\n";
                throw std::runtime_error("Tight-crop OCR did not copy the exact label.");
            }
            std::wcout << L"PASS: tight crop " << filename << L" -> " << expectedText << L"\n";
        }
        require(app.image.pixels == previousPixels && app.recent.size() == recentCount && app.dirty,
                "Repeated text captures changed the open snip.");
        auto loadFixture = [&](const wchar_t *filename) {
            const auto path =
                std::filesystem::path(__FILE__).parent_path() / "fixtures" / "ocr" / filename;
            std::ifstream file(path, std::ios::binary);
            require(file.good(), "Cannot open boundary OCR fixture.");
            return app.graphics.decode(
                std::vector<uint8_t>{std::istreambuf_iterator<char>(file), {}});
        };
        const auto dateFixture = loadFixture(L"clipped-date-row.png");
        const auto partFixture = loadFixture(L"hyphenated-part.png");
        const auto selectedSerial = loadFixture(L"selected-serial.png");
        // Selection boundaries vary from drag to drag. Borders/noise must not
        // intermittently invalidate a complete line or enter the clipboard.
        for (auto [filename, expectedText, maximumInset] :
             std::array<std::tuple<const wchar_t *, const wchar_t *, int>, 9>{
                 {{L"selected-part-margins.png", L"APF6-037-01-04-RA", 3},
                  {L"serial-in-field.png", L"2007372", 3},
                  {L"red-part-in-frame.png", L"RSP-241492-01", 3},
                  {L"serial-with-fragments.png", L"801099", 3},
                  {L"condensed-dotted-part.png", L"ADM6-100-01.5-4-A", 2},
                  {L"barcode-number.png", L"39919487", 2},
                  {L"barcode-number-tight.png", L"36808975", 2},
                  {L"small-part-number.png", L"RSP-241492-01", 3},
                  {L"selected-serial-focus-border.png", L"36808975", 3}}})
        {
            const auto sample = loadFixture(filename);
            std::vector<std::array<int, 4>> edges{{0, 0, 0, 0}};
            for (int inset = 1; inset <= maximumInset; ++inset)
            {
                edges.push_back({inset, inset, inset, inset});
                edges.push_back({inset, 0, 0, 0});
                edges.push_back({0, inset, 0, 0});
                edges.push_back({0, 0, inset, 0});
                edges.push_back({0, 0, 0, inset});
            }
            for (auto [left, top, right, bottom] : edges)
            {
                beginTextRecognition(sample.crop(left, top, sample.width - left - right,
                                                 sample.height - top - bottom));
                waitRecognition();
                if (clipboardText() != expectedText || noticeTitle() != L"Text copied")
                {
                    std::wcerr << filename << L", edges " << left << L"," << top << L"," << right
                               << L"," << bottom << L": [" << clipboardText() << L"], notice ["
                               << noticeTitle() << L"]\n";
                    throw std::runtime_error(
                        "A boundary adjustment changed complete text recognition.");
                }
            }
        }
        const auto focusedSerial = loadFixture(L"selected-serial-focus-border.png");
        for (const auto &tight : {selectedSerial.crop(3, 6, selectedSerial.width - 6, 19),
                                  selectedSerial.crop(4, 7, selectedSerial.width - 8, 17),
                                  focusedSerial.crop(0, 7, focusedSerial.width, 20),
                                  focusedSerial.crop(4, 8, focusedSerial.width - 8, 18),
                                  focusedSerial.crop(4, 7, focusedSerial.width - 8, 20),
                                  focusedSerial.crop(4, 6, focusedSerial.width - 8, 22)})
        {
            beginTextRecognition(tight);
            waitRecognition();
            require(clipboardText() == L"36808975" && noticeTitle() == L"Text copied",
                    "A tighter blue selection lost the serial number.");
        }
        // Windows still confuses the quotes/parentheses in this low-resolution
        // unit label. Report it honestly instead of accepting a fabricated fix.
        const auto quotedText =
            std::async(std::launch::async, [sample = loadFixture(L"quoted-unit.png")] {
                return recognizeText(sample);
            }).get();
        std::wcout << L"Windows OCR quality diagnostic: quoted-unit.png -> [" << quotedText
                   << L"], target [Each\u2019 (\u2018EA\u2019)]\n";
        const auto clippedSequence = GetClipboardSequenceNumber();
        const auto barcode = loadFixture(L"barcode-number.png");
        const auto narrowPart = loadFixture(L"condensed-dotted-part.png");
        int partialIndex = 0;
        for (const auto &partial :
             {dateFixture.crop(0, 0, dateFixture.width, 4),
              partFixture.crop(0, 0, partFixture.width, 13),
              partFixture.crop(13, 0, partFixture.width - 13, partFixture.height),
              partFixture.crop(0, 0, 107, partFixture.height),
              barcode.crop(10, 0, barcode.width - 10, barcode.height),
              barcode.crop(0, 0, 72, barcode.height),
              narrowPart.crop(8, 0, narrowPart.width - 8, narrowPart.height),
              narrowPart.crop(0, 0, 113, narrowPart.height),
              focusedSerial.crop(0, 12, focusedSerial.width, 15)})
        {
            beginTextRecognition(partial);
            waitRecognition();
            if (noticeTitle() != L"No text found" ||
                GetClipboardSequenceNumber() != clippedSequence)
                std::wcerr << L"Cut row " << partialIndex << L" -> [" << clipboardText()
                           << L"], notice [" << noticeTitle() << L"]\n";
            require(noticeTitle() == L"No text found" &&
                        GetClipboardSequenceNumber() == clippedSequence,
                    "A cut-off text row produced invented characters or changed the clipboard.");
            ++partialIndex;
        }
        // A larger selection around one row must preserve the same part number.
        auto roomy = Bitmap::create(partFixture.width + 40, 200);
        std::fill(roomy.pixels.begin(), roomy.pixels.end(), 255);
        for (int y = 0; y < partFixture.height; ++y)
            std::memcpy(&roomy.pixels[(static_cast<size_t>(y + 80) * roomy.width + 20) * 4],
                        &partFixture.pixels[static_cast<size_t>(y) * partFixture.width * 4],
                        static_cast<size_t>(partFixture.width) * 4);
        beginTextRecognition(roomy);
        waitRecognition();
        if (clipboardText() != L"APF6-037-01-04-RA" || noticeTitle() != L"Text copied")
            std::wcerr << L"Roomy single-line actual: [" << clipboardText() << L"]\n";
        require(clipboardText() == L"APF6-037-01-04-RA" && noticeTitle() == L"Text copied",
                "A larger margin changed single-line part-number recognition.");
        std::wcout << L"PASS: clipped top/bottom/left/right rows omitted; roomy single-line "
                      L"identifier preserved\n";
        auto roomyNarrow = Bitmap::create(narrowPart.width + 40, 200);
        std::fill(roomyNarrow.pixels.begin(), roomyNarrow.pixels.end(), 255);
        for (int y = 0; y < narrowPart.height; ++y)
            std::memcpy(
                &roomyNarrow.pixels[(static_cast<size_t>(y + 80) * roomyNarrow.width + 20) * 4],
                &narrowPart.pixels[static_cast<size_t>(y) * narrowPart.width * 4],
                static_cast<size_t>(narrowPart.width) * 4);
        beginTextRecognition(roomyNarrow);
        waitRecognition();
        require(clipboardText() == L"ADM6-100-01.5-4-A" && noticeTitle() == L"Text copied",
                "A larger selection lost the narrow identifier's punctuation.");
        // Cover other fonts/weights and panel colors rather than tuning only the
        // supplied screenshots. Thin light text must not lose punctuation/digits
        // when the bold-text edge adjustment is applied.
        for (int fontHeight : {12, 16})
            for (bool bold : {false, true})
                for (bool lightText : {false, true})
                {
                    for (const std::wstring label : {L"Order number 123425-5", L"123425-5"})
                    {
                        beginTextRecognition(smallTextSample(
                            label, fontHeight, bold, lightText ? RGB(255, 255, 255) : RGB(0, 0, 0),
                            lightText ? RGB(139, 84, 24) : RGB(255, 255, 255)));
                        waitRecognition();
                        if (clipboardText() != label || noticeTitle() != L"Text copied")
                        {
                            std::wcerr << L"Small font " << fontHeight << L", bold " << bold
                                       << L", light text " << lightText << L": [" << clipboardText()
                                       << L"]\n";
                            throw std::runtime_error("Small-font OCR changed the order number.");
                        }
                    }
                }
        auto oversized = Bitmap::create(3600, 220);
        std::fill(oversized.pixels.begin(), oversized.pixels.end(), 255);
        for (int y = 0; y < image.height; ++y)
            std::memcpy(
                &oversized.pixels[(static_cast<size_t>(y + 60) * oversized.width + 2700) * 4],
                &image.pixels[static_cast<size_t>(y) * image.width * 4],
                static_cast<size_t>(image.width) * 4);
        beginTextRecognition(std::move(oversized));
        waitRecognition();
        require(clipboardText() == expected && noticeTitle() == L"Text copied",
                "An oversized selection lost text at its right or bottom edge.");
        std::stop_source canceled;
        canceled.request_stop();
        require(recognizeText(image, canceled.get_token()).empty() && recognizeText({}).empty(),
                "Canceled or empty OCR input produced a result.");
        const DWORD sequence = GetClipboardSequenceNumber();
        auto blank = Bitmap::create(240, 100);
        std::fill(blank.pixels.begin(), blank.pixels.end(), 255);
        beginTextRecognition(blank);
        waitRecognition();
        require(noticeTitle() == L"No text found" && GetClipboardSequenceNumber() == sequence,
                "A blank selection changed the clipboard or showed success.");
        renderNotice(L"text-preview-empty.png");
        // A newer intentional copy must win over an OCR operation still in flight.
        beginTextRecognition(image);
        require(copyText(app.window, L"Newer clipboard"), "Cannot simulate a newer copy.");
        waitRecognition();
        require(noticeTitle() == L"Clipboard changed" && clipboardText() == L"Newer clipboard",
                "OCR replaced a more recent copy.");
        require(copyText(app.window, expected), "Cannot reset the private clipboard.");
        const DWORD resetSequence = GetClipboardSequenceNumber();
        // Hold the private clipboard from another thread through OCR completion.
        std::promise<bool> opened;
        auto clipboardOpened = opened.get_future();
        std::jthread holder([&](std::stop_token stop) {
            const bool ready = SetThreadDesktop(desktop) && OpenClipboard(nullptr);
            opened.set_value(ready);
            if (ready)
            {
                while (!stop.stop_requested())
                    Sleep(10);
                CloseClipboard();
            }
        });
        require(clipboardOpened.get(), "Cannot hold private clipboard for busy test.");
        beginTextRecognition(image);
        waitRecognition();
        const bool busy = noticeTitle() == L"Could not copy text";
        holder.request_stop();
        holder.join();
        require(busy && GetClipboardSequenceNumber() == resetSequence &&
                    clipboardText() == expected,
                "Busy text clipboard lost the old contents or showed success.");
        app.textCapture = true;
        app.textEditorWasVisible = false;
        cancelCapture();
        require(!IsWindowVisible(app.window) && !app.textCapture &&
                    GetClipboardSequenceNumber() == resetSequence,
                "Canceling text capture opened the editor or changed the clipboard.");

        app.textNotice.show(L"Text copied", L"Order number 123425-5\r\nSecond line", {0, 0}, true,
                            Accent);
        renderNotice(L"text-preview-dark.png");
        const auto longText =
            L"Line one\r\nLine two\r\nLine three\r\nLine four\r\nLine five\r\nLine six";
        app.textNotice.show(L"Text copied", longText, {0, 0}, false, Accent);
        renderNotice(L"text-preview-long.png");
        app.textNotice.show(L"Text copied", std::wstring(2000, L'x'), {0, 0}, false, Accent);
        renderNotice(L"text-preview-long-word.png");
        SendMessageW(app.textNotice.window(), WM_MOUSEMOVE, 0, 0);
        SendMessageW(app.textNotice.window(), WM_TIMER, 1, 0);
        require(IsWindow(app.textNotice.window()), "Hover closed the preview.");
        SendMessageW(app.textNotice.window(), WM_MOUSELEAVE, 0, 0);
        SendMessageW(app.textNotice.window(), WM_TIMER, 1, 0);
        require(!app.textNotice.window(), "The text preview failed to dismiss.");
    }
    catch (const std::exception &failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        exitCode = 1;
    }
    if (app.window && IsWindow(app.window))
        DestroyWindow(app.window);
    app.textNotice.close();
    app.textWorker.request_stop();
    if (app.textWorker.joinable())
        app.textWorker.join();
    app.target.reset();
    app.workspaceBrush.reset();
    if (com)
        CoUninitialize();
    SetThreadDesktop(originalDesktop);
    if (desktop)
        CloseDesktop(desktop);
    SetProcessWindowStation(originalStation);
    if (station)
        CloseWindowStation(station);
    if (!exitCode)
    {
        // Global shortcuts require the interactive station. This fresh thread
        // creates only a hidden owner and never accesses the user's clipboard.
        std::thread shortcuts([&] {
            bool initialized = false;
            try
            {
                require(SetThreadDesktop(originalDesktop),
                        "Cannot attach the shortcut test thread.");
                check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED),
                      "Cannot initialize shortcut test COM.");
                initialized = true;
                app.window = CreateWindowExW(0, L"STATIC", L"Text shortcut test", WS_POPUP, 0, 0, 1,
                                             1, nullptr, nullptr, app.instance, nullptr);
                require(app.window != nullptr, "Cannot create hidden shortcut test owner.");
                checkTextShortcuts();
            }
            catch (const std::exception &failure)
            {
                std::cerr << "FAIL: " << failure.what() << '\n';
                exitCode = 1;
            }
            if (app.window)
                DestroyWindow(std::exchange(app.window, nullptr));
            if (initialized)
                CoUninitialize();
        });
        shortcuts.join();
    }
    if (!exitCode)
        std::wcout << L"PASS: built-in Windows OCR, twenty exact screen crops (isolated zero "
                      L"excluded), 129 border/fragment selection variants, tight blue serial "
                      L"selections, sixteen small-font/color/weight "
                      "cases, oversized/canceled inputs, "
                      "exact order number, Unicode and line breaks, no editor "
                      "opening, "
                      "snip retention, blank/cancel/busy/newer-copy clipboard retention, "
                      "nonactivating preview and dismissal, "
                      "three shortcut conflicts/rollback/swap/disable/persistence. User clipboard "
                      "untouched.\n";
    return exitCode;
}
