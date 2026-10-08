// Exercise the editor's capture/Copy paths on a private Windows clipboard.
// Including the entry point keeps these integration checks out of the shipped executable.
#include "../src/main.cpp"
#include <cstring>
#include <iostream>
#include <thread>
#include <fstream>
#include <iterator>

bool recentCopyMenuSeen = false;
int snipMenuChoice = Copy;
LRESULT CALLBACK driveRecentCopyMenu(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0)
    {
        const auto message = reinterpret_cast<const CWPSTRUCT *>(lp);
        if (message->message == WM_INITMENUPOPUP)
        {
            const auto menu = reinterpret_cast<HMENU>(message->wParam);
            recentCopyMenuSeen = GetMenuItemCount(menu) == 3 && GetMenuItemID(menu, 0) == Copy &&
                                 GetMenuItemID(menu, 1) == Save && GetMenuItemID(menu, 2) == SaveAs;
            PostMessageW(message->hwnd, WM_CHAR, snipMenuChoice == Copy ? L'c' : L's', 0);
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
void CALLBACK cancelStalledRecentMenu(HWND window, UINT, UINT_PTR id, DWORD)
{
    KillTimer(window, id);
    EndMenu();
}

int wmain()
{
    const auto originalStation = GetProcessWindowStation();
    const auto originalDesktop = GetThreadDesktop(GetCurrentThreadId());
    HWINSTA station = nullptr;
    HDESK desktop = nullptr;
    bool com = false;
    int result = 0;
    auto require = [](bool ok, const char *message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    try
    {
        station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
        require(station && SetProcessWindowStation(station), "Cannot isolate test clipboard.");
        desktop = CreateDesktopW(L"TigerSnipAutoCopyTest", nullptr, nullptr, 0,
                                 DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU | DESKTOP_READOBJECTS |
                                     DESKTOP_WRITEOBJECTS | DESKTOP_HOOKCONTROL,
                                 nullptr);
        require(desktop && SetThreadDesktop(desktop), "Cannot attach private test desktop.");
        check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Cannot initialize COM.");
        com = true;
        app.instance = GetModuleHandleW(nullptr);
        app.iniPath = (std::filesystem::current_path() / L"auto-copy-settings.ini").wstring();
        require(WritePrivateProfileStringW(L"Settings", L"AutoCopy", nullptr, app.iniPath.c_str()),
                "Cannot reset isolated settings.");
        loadToolPreferences();
        require(app.autoCopy, "Auto copy must default on when no preference exists.");
        app.softwareRendering = true;
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&controls);
        registerClasses();
        require(CreateWindowExW(0, MainClass, L"Auto copy test", WS_OVERLAPPEDWINDOW, 0, 0, 1000,
                                700, nullptr, createMenu(), app.instance, nullptr),
                "Cannot create editor.");
        auto checked = [&] {
            require(GetMenu(app.window) != nullptr, "Private editor is missing its Settings menu.");
            updateMenus();
            return (GetMenuState(GetMenu(app.window), AutoCopy, MF_BYCOMMAND) & MF_CHECKED) != 0;
        };
        require(checked(), "Settings menu must show auto copy checked by default.");
        auto image = Bitmap::create(96, 64);
        for (size_t i = 0; i < image.pixels.size(); i += 4)
        {
            image.pixels[i] = static_cast<uint8_t>(i);
            image.pixels[i + 1] = 75;
            image.pixels[i + 2] = 140;
            image.pixels[i + 3] = 255;
        }
        auto clipboardMatches = [&](const Bitmap &expected) {
            require(OpenClipboard(app.window), "Cannot read private clipboard.");
            const auto memory = GetClipboardData(CF_DIB);
            const auto data = static_cast<const uint8_t *>(GlobalLock(memory));
            bool match = false;
            if (data)
            {
                const auto header = reinterpret_cast<const BITMAPINFOHEADER *>(data);
                match = header->biWidth == expected.width && header->biHeight == -expected.height &&
                        GlobalSize(memory) >= sizeof(*header) + expected.pixels.size() &&
                        std::memcmp(data + sizeof(*header), expected.pixels.data(),
                                    expected.pixels.size()) == 0;
                GlobalUnlock(memory);
            }
            const auto png = GetClipboardData(RegisterClipboardFormatW(L"PNG"));
            const auto pngData = static_cast<const uint8_t *>(GlobalLock(png));
            if (pngData)
            {
                const auto bytes = app.graphics.png(expected);
                match = match && GlobalSize(png) >= bytes.size() &&
                        std::memcmp(pngData, bytes.data(), bytes.size()) == 0;
                GlobalUnlock(png);
            }
            else
                match = false;
            CloseClipboard();
            require(match, "Clipboard pixels/PNG do not match the current export.");
        };
        auto contextAction = [&](POINT point, int choice) {
            snipMenuChoice = choice;
            recentCopyMenuSeen = false;
            const auto hook = SetWindowsHookExW(WH_CALLWNDPROC, driveRecentCopyMenu, nullptr,
                                                GetCurrentThreadId());
            require(hook != nullptr, "Cannot drive snip context menu.");
            SetTimer(app.window, 12345, 2000, cancelStalledRecentMenu);
            SendMessageW(app.window, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(app.window),
                         MAKELPARAM(point.x, point.y));
            KillTimer(app.window, 12345);
            UnhookWindowsHookEx(hook);
            require(recentCopyMenuSeen, "Snip right-click did not offer Copy, Save and Save As.");
        };
        auto savedMatches = [&](const std::wstring &path, const Bitmap &expected) {
            std::ifstream file(std::filesystem::path(path), std::ios::binary);
            require(file.good(), "Context-menu Save did not create a PNG.");
            const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
            require(bytes == app.graphics.png(expected),
                    "Context-menu Save lost annotations or export effects.");
        };
        acceptCapture(image);
        clipboardMatches(renderedExport());
        require(!app.copyFlashStarted, "Auto copy should leave the manual Copy flash available.");
        BYTE keyboard[256]{};
        GetKeyboardState(keyboard);
        BYTE ctrlKeyboard[256];
        std::copy(std::begin(keyboard), std::end(keyboard), std::begin(ctrlKeyboard));
        ctrlKeyboard[VK_CONTROL] = 0x80;
        // Plain letters, including a late/repeated C after Ctrl is released, must not
        // change the editor mode or start crop/eyedropper while copying an image.
        auto plainLetters = [&] {
            const auto tool = app.tool;
            const auto items = app.document.items;
            const auto selection = app.document.selected;
            const auto clipboardSequence = GetClipboardSequenceNumber();
            BYTE plainKeyboard[256]{};
            for (bool shift : {false, true})
            {
                plainKeyboard[VK_SHIFT] = shift ? 0x80 : 0;
                require(SetKeyboardState(plainKeyboard), "Cannot set plain keyboard state.");
                for (WPARAM key = 'A'; key <= 'Z'; ++key)
                {
                    SendMessageW(app.window, WM_KEYDOWN, key, 0);
                    require(app.tool == tool && !app.cropping && !app.erasing &&
                                !app.pickingColor && !app.textEdit &&
                                app.document.selected == selection && app.document.items == items,
                            "A plain letter changed tools or editor state.");
                }
            }
            require(SetKeyboardState(keyboard), "Cannot restore keyboard state.");
            require(GetClipboardSequenceNumber() == clipboardSequence,
                    "Plain letters changed the clipboard.");
        };
        plainLetters();
        auto ctrlC = [&] {
            require(SetKeyboardState(ctrlKeyboard), "Cannot set private keyboard state.");
            require(GetKeyState(VK_CONTROL) & 0x8000, "Private desktop did not retain Ctrl state.");
            const auto before = GetClipboardSequenceNumber();
            SendMessageW(app.window, WM_KEYDOWN, 'C', 0);
            SetKeyboardState(keyboard);
            require(GetClipboardSequenceNumber() != before,
                    "Ctrl+C must copy again even after auto copy.");
            require(app.status.find(L"Copied image and annotations") == 0,
                    "Ctrl+C must confirm Copy success.");
            // Windows suppresses visibility on noninteractive stations. The desktop
            // smoke suite verifies the visible flash; this suite verifies actual copying.
            require(!IsWindowVisible(app.window) || app.copyFlashStarted,
                    "Visible manual Copy must start feedback.");
            clipboardMatches(renderedExport());
            plainLetters();
        };
        ctrlC();
        ctrlC();
        Annotation arrow;
        arrow.kind = Tool::Arrow;
        arrow.a = {10, 10};
        arrow.b = {70, 40};
        app.document.items.push_back(arrow);
        app.dirty = true;
        ctrlC();
        require(!app.dirty, "Manual Copy must include annotations and clear dirty state.");
        const auto copiedEdits = renderedExport();
        command(AutoCopy);
        require(!app.autoCopy && !checked(), "Auto copy menu toggle must turn it off.");
        loadToolPreferences();
        require(!app.autoCopy, "Disabled auto copy must persist on reload.");
        const auto beforeCapture = GetClipboardSequenceNumber();
        acceptCapture(image.crop(0, 0, 30, 20));
        require(GetClipboardSequenceNumber() == beforeCapture,
                "Disabled auto copy changed clipboard.");
        clipboardMatches(copiedEdits);
        ctrlC();
        command(AutoCopy);
        loadToolPreferences();
        require(app.autoCopy && checked(), "Enabled auto copy must persist on reload.");
        // Selection capture uses the same completion path as instant/full-monitor capture.
        app.desktop = image;
        app.selectionStart = {3, 4};
        app.selectionEnd = {33, 24};
        finishCapture();
        clipboardMatches(image.crop(3, 4, 30, 20));
        const auto beforeCancel = GetClipboardSequenceNumber();
        app.capturePending = true;
        cancelCapture();
        restoreRecentSnip(0);
        require(GetClipboardSequenceNumber() == beforeCancel,
                "Cancel or reopening Recent copied a snip.");
        app.smoke = true;
        acceptCapture(image);
        require(GetClipboardSequenceNumber() == beforeCancel,
                "Diagnostic captures changed clipboard.");
        app.smoke = false;
        // Copy an inactive recent capture, including its annotations and current export effects,
        // while retaining the live capture, selection, undo history and viewport.
        for (int layout : {InterfaceClassic, InterfaceOrange})
        {
            command(layout);
            const auto liveExport = renderedExport();
            const auto liveItems = app.document.items;
            const auto liveTool = app.tool;
            const auto canvas = canvasRect();
            const auto point = app.view.toScreen({app.image.width / 2.f, app.image.height / 2.f});
            require(canvas.contains(point), "Snip is not visible for context-menu test.");
            POINT canvasPoint{static_cast<LONG>(point.x * app.dpi),
                              static_cast<LONG>(point.y * app.dpi)};
            ClientToScreen(app.window, &canvasPoint);
            contextAction(canvasPoint, Copy);
            clipboardMatches(liveExport);
            app.savePath = (std::filesystem::current_path() /
                            (L"context-current-" + std::to_wstring(layout) + L".png"))
                               .wstring();
            app.dirty = true;
            contextAction({-1, -1}, Save);
            savedMatches(app.savePath, liveExport);
            require(!app.dirty && app.document.items == liveItems && app.tool == liveTool,
                    "Snip context actions changed the document or drawing tool.");
            command(RecentSnips);
            require(app.recentOpen, "Recent snips did not open.");
            app.document.begin();
            app.document.items.push_back(arrow);
            app.document.commit();
            app.document.selected = 0;
            app.dirty = true;
            const auto currentImage = app.image;
            const auto currentItems = app.document.items;
            const auto currentSelection = app.document.selected;
            const auto currentView = app.view;
            const int current = app.activeRecent;
            app.exportOptions.professionalBorder = true;
            app.exportOptions.professionalBlur = true;
            app.exportOptions.professionalRounded = true;
            resetPreview();
            const auto &saved = app.recent[0];
            const auto expected =
                app.graphics.exportImage(saved.image, saved.document.items, app.exportOptions);
            const auto recentButton =
                std::find_if(app.buttons.begin(), app.buttons.end(),
                             [](const Button &b) { return b.command == RecentChoiceFirst; });
            require(recentButton != app.buttons.end(), "Saved Recent thumbnail is missing.");
            POINT menuPoint{static_cast<LONG>((recentButton->rect.left + 12) * app.dpi),
                            static_cast<LONG>((recentButton->rect.top + 12) * app.dpi)};
            ClientToScreen(app.window, &menuPoint);
            contextAction(menuPoint, Copy);
            clipboardMatches(expected);
            app.recent[0].savePath = (std::filesystem::current_path() /
                                      (L"context-recent-" + std::to_wstring(layout) + L".png"))
                                         .wstring();
            app.recent[0].dirty = true;
            contextAction(menuPoint, Save);
            savedMatches(app.recent[0].savePath, expected);
            require(!app.recent[0].dirty, "Saving Recent did not remember its saved state.");
            require(
                app.recentOpen && app.activeRecent == current && app.dirty &&
                    app.image.pixels == currentImage.pixels && app.document.items == currentItems &&
                    app.document.selected == currentSelection && app.document.canUndo() &&
                    app.view.origin == currentView.origin && app.view.scale == currentView.scale,
                "Copying a recent capture altered the live capture or closed Recent.");
            copyRecentSnip(current);
            clipboardMatches(renderedExport());
            require(app.recentOpen && app.activeRecent == current && !app.dirty,
                    "Copying the current Recent entry used stale data or closed Recent.");
            command(RecentSnips);
        }
        app.exportOptions = {};
        resetPreview();
        // Another thread owns the clipboard so both automatic and manual failure paths run.
        const auto held = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const auto release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        bool clipboardHeld = false;
        std::thread blocker([&] {
            if (SetThreadDesktop(desktop))
                clipboardHeld = OpenClipboard(nullptr) != FALSE;
            SetEvent(held);
            WaitForSingleObject(release, 10000);
            if (clipboardHeld)
                CloseClipboard();
        });
        WaitForSingleObject(held, 10000);
        bool failureHandled = false;
        try
        {
            require(clipboardHeld, "Cannot hold private clipboard for failure test.");
            acceptCapture(image);
            require(
                hasImage() && app.image.pixels == image.pixels && !app.copyFlashStarted &&
                    app.status.find(L"Clipboard is busy") == 0,
                "Automatic Copy failure must retain the capture and report the busy clipboard.");
            command(Copy);
            failureHandled = hasImage() && app.image.pixels == image.pixels &&
                             !app.copyFlashStarted && app.status.find(L"Clipboard is busy") == 0;
            copyRecentSnip(0);
            require(app.status.find(L"Clipboard is busy") == 0 && !app.copyNoticeStarted,
                    "Failed Recent copy showed a success confirmation.");
        }
        catch (...)
        {
            SetEvent(release);
            blocker.join();
            CloseHandle(held);
            CloseHandle(release);
            throw;
        }
        SetEvent(release);
        blocker.join();
        CloseHandle(held);
        CloseHandle(release);
        require(failureHandled, "Busy clipboard must retain the new snip and avoid a false flash.");
        command(Copy);
        clipboardMatches(renderedExport());
        require(!IsWindowVisible(app.window) || app.copyFlashStarted,
                "Visible retry Copy must flash.");
        auto large = Bitmap::create(3840, 2160);
        for (int y = 0; y < large.height; ++y)
            for (int x = 0; x < large.width; ++x)
            {
                const auto offset = (static_cast<size_t>(y) * large.width + x) * 4;
                large.pixels[offset] = static_cast<uint8_t>(x / 32);
                large.pixels[offset + 1] = static_cast<uint8_t>(y / 24);
                large.pixels[offset + 2] = static_cast<uint8_t>((x + y) / 16);
                large.pixels[offset + 3] = 255;
            }
        const auto started = std::chrono::steady_clock::now();
        acceptCapture(std::move(large));
        const auto elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
        clipboardMatches(renderedExport());
        std::cout << "4K capture completion including auto copy: " << elapsed << " ms\n";
        std::cout << "PASS: default-on auto copy, Settings checkmark, persisted OFF/ON, new and "
                     "selection "
                     "captures, PNG/DIB export, already-copied snip Ctrl+C and repeated copy, "
                     "inert plain letters, annotations, disabled capture, "
                     "right-click canvas Copy/Save and keyboard context menu, direct Recent "
                     "Copy/Save with annotations and export effects, live edit "
                     "retention, "
                     "cancel/Recent/diagnostic isolation, busy clipboard retention and retry. User "
                     "clipboard untouched.\n";
    }
    catch (const std::exception &exception)
    {
        std::cout << "FAIL: " << exception.what() << " Windows error=" << GetLastError() << '\n';
        result = 1;
    }
    if (app.window && IsWindow(app.window))
        DestroyWindow(app.window);
    resetPreview();
    app.workspaceBrush.reset();
    app.target.reset();
    if (com)
        CoUninitialize();
    SetThreadDesktop(originalDesktop);
    if (desktop)
        CloseDesktop(desktop);
    SetProcessWindowStation(originalStation);
    if (station)
        CloseWindowStation(station);
    return result;
}
