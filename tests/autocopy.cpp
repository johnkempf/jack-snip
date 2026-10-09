// Exercise the editor's capture/Copy paths on a private Windows clipboard.
// Including the entry point keeps these integration checks out of the shipped executable.
#define TIGER_SNIP_TESTING
#include "../src/main.cpp"
#include <cstring>
#include <iostream>
#include <thread>
#include <fstream>
#include <iterator>

bool recentCopyMenuSeen = false;
int snipMenuChoice = Copy;
std::wstring saveDialogPath, switchedDialogName;
std::wstring initialSaveFolder;
bool saveDialogJpg = false, cancelSaveDialog = false, saveDialogSeen = false;
DWORD initialSaveFilter = 0;
bool saveDialogFiltersValid = false;
IFileSaveDialog *activeSaveDialog = nullptr;
unsigned saveDialogAttempts = 0;
void CALLBACK driveSaveDialog(HWND window, UINT, UINT_PTR id, DWORD)
{
    try
    {
        Com<IOleWindow> nativeWindow;
        check(activeSaveDialog->QueryInterface(__uuidof(IOleWindow),
                    reinterpret_cast<void **>(nativeWindow.put())), "Save As is not a native dialog.");
        HWND dialogWindow = nullptr;
        if (FAILED(nativeWindow->GetWindow(&dialogWindow)) || !IsWindow(dialogWindow))
        {
            if (++saveDialogAttempts >= 50)
                throw std::runtime_error("The modern Save As dialog did not open.");
            return;
        }
        KillTimer(window, id);
        saveDialogSeen = true;
        Com<IShellItem> startingFolder;
        check(activeSaveDialog->GetFolder(startingFolder.put()), "Cannot read the initial save folder.");
        PWSTR folderName = nullptr;
        check(startingFolder->GetDisplayName(SIGDN_FILESYSPATH, &folderName), "Cannot read the initial folder path.");
        initialSaveFolder = folderName;
        CoTaskMemFree(folderName);
        UINT selectedFormat = 0;
        check(activeSaveDialog->GetFileTypeIndex(&selectedFormat), "Cannot read initial save type.");
        initialSaveFilter = selectedFormat;
        HWND formatCombo = nullptr;
        EnumChildWindows(dialogWindow, [](HWND child, LPARAM data) -> BOOL {
            wchar_t className[64]{};
            GetClassNameW(child, className, std::size(className));
            if (wcscmp(className, L"ComboBox") != 0 || SendMessageW(child, CB_GETCOUNT, 0, 0) != 2)
                return TRUE;
            wchar_t first[256]{}, second[256]{};
            if (SendMessageW(child, CB_GETLBTEXTLEN, 0, 0) >= 256 ||
                SendMessageW(child, CB_GETLBTEXTLEN, 1, 0) >= 256)
                return TRUE;
            SendMessageW(child, CB_GETLBTEXT, 0, reinterpret_cast<LPARAM>(first));
            SendMessageW(child, CB_GETLBTEXT, 1, reinterpret_cast<LPARAM>(second));
            if (wcsstr(first, L"PNG") && wcsstr(second, L"JPG") && wcsstr(second, L"no border"))
            {
                *reinterpret_cast<HWND *>(data) = child;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&formatCombo));
        if (!formatCombo)
            throw std::runtime_error("Modern Save As is missing the PNG/JPG dropdown.");
        SendMessageW(formatCombo, CB_SETCURSEL, saveDialogJpg ? 1 : 0, 0);
        SendMessageW(GetParent(formatCombo), WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(formatCombo), CBN_SELENDOK),
                     reinterpret_cast<LPARAM>(formatCombo));
        PWSTR name = nullptr;
        check(activeSaveDialog->GetFileName(&name), "Cannot read the changed filename.");
        switchedDialogName = name;
        CoTaskMemFree(name);
        DWORD options = 0;
        check(activeSaveDialog->GetOptions(&options), "Cannot read Save As options.");
        saveDialogFiltersValid = (options & FOS_STRICTFILETYPES) &&
                                (options & FOS_OVERWRITEPROMPT);
        if (cancelSaveDialog)
            activeSaveDialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
        else
        {
            Com<IShellItem> destinationFolder;
            check(SHCreateItemFromParsingName(std::filesystem::path(saveDialogPath).parent_path().c_str(), nullptr,
                    __uuidof(IShellItem), reinterpret_cast<void **>(destinationFolder.put())),
                  "Cannot read the second save folder.");
            check(activeSaveDialog->SetFolder(destinationFolder.get()), "Cannot choose another save folder.");
            check(activeSaveDialog->SetFileName(std::filesystem::path(saveDialogPath).stem().c_str()),
                  "Cannot enter save filename.");
            PostMessageW(dialogWindow, WM_COMMAND, IDOK, 0);
        }
    }
    catch (const std::exception &exception)
    {
        std::cerr << "Save As test driver: " << exception.what() << '\n';
        KillTimer(window, id);
        activeSaveDialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
    }
}
void prepareSaveDialog(IFileSaveDialog *dialog)
{
    activeSaveDialog = dialog;
    saveDialogAttempts = 0;
    SetTimer(app.window, 54321, 100, driveSaveDialog);
}
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
        // Save must follow Settings even when this capture already has a filename.
        const auto originalSavePath = app.savePath;
        const auto originalRecentPath = app.recent[0].savePath;
        const auto saveRoot = std::filesystem::current_path() / L"save folders";
        const auto firstFolder = saveRoot / L"first";
        const auto secondFolder = saveRoot / L"second";
        std::filesystem::create_directories(firstFolder);
        std::filesystem::create_directories(secondFolder);
        auto readBytes = [](const std::wstring &path) {
            std::ifstream file(std::filesystem::path(path), std::ios::binary);
            return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), {});
        };
        const auto originalBytes = readBytes(originalSavePath);
        require(!originalBytes.empty(), "The previous save fixture is missing.");
        // Direct Save still supports the explicit current-location action.
        auto quickSave = [&] { saveImage(); };
        setSaveFolder(firstFolder.wstring());
        quickSave();
        const auto firstPath =
            (firstFolder / std::filesystem::path(originalSavePath).filename()).wstring();
        require(app.savePath == firstPath && !app.dirty,
                "Save did not redirect an existing filename to the first configured folder.");
        savedMatches(firstPath, renderedExport());
        require(app.status == L"Saved PNG: " + firstPath,
                "Save confirmation did not identify the actual destination.");
        setSaveFolder(secondFolder.wstring());
        quickSave();
        const auto secondPath =
            (secondFolder / std::filesystem::path(originalSavePath).filename()).wstring();
        require(app.savePath == secondPath,
                "Repeated Save did not follow the newly configured folder.");
        savedMatches(secondPath, renderedExport());
        require(readBytes(originalSavePath) == originalBytes,
                "Redirecting Save changed the original file.");
        const auto liveSavePath = app.savePath;
        const auto liveIndex = app.activeRecent;
        const auto recentExport = app.graphics.exportImage(
            app.recent[0].image, app.recent[0].document.items, app.exportOptions);
        saveRecentSnip(0, false);
        const auto recentPath =
            (secondFolder / std::filesystem::path(originalRecentPath).filename()).wstring();
        require(app.recent[0].savePath == recentPath && !app.recent[0].dirty &&
                    app.savePath == liveSavePath && app.activeRecent == liveIndex,
                "Recent Save did not follow the configured folder while preserving the editor.");
        savedMatches(recentPath, recentExport);
        app.saveFolder = (saveRoot / L"unavailable").wstring();
        bool rejectedUnavailable = false;
        try
        {
            saveImage();
        }
        catch (const std::runtime_error &)
        {
            rejectedUnavailable = true;
        }
        require(rejectedUnavailable && app.savePath == liveSavePath,
                "An unavailable save folder silently saved somewhere else.");
        app.saveFolder = secondFolder.wstring();
        app.exportOptions.professionalBorder = true;
        app.exportOptions.professionalBlur = true;
        app.exportOptions.professionalRounded = true;
        command(SaveFormatJpg);
        quickSave();
        const auto jpgPath = std::filesystem::path(secondPath).replace_extension(L".jpg").wstring();
        const auto jpgBytes = readBytes(jpgPath);
        require(app.savePath == jpgPath && app.status == L"Saved JPG (Professional Border disabled): " + jpgPath &&
                    jpgBytes.size() > 2 && jpgBytes[0] == 0xff && jpgBytes[1] == 0xd8,
                "Save did not switch the filename and encoded file to JPG.");
        const auto decodedJpg = app.graphics.decode(jpgBytes);
        require(decodedJpg.width == app.image.width && decodedJpg.height == app.image.height &&
                    app.exportOptions.professionalBorder && app.exportOptions.professionalBlur &&
                    app.exportOptions.professionalRounded &&
                    GetPrivateProfileIntW(L"Settings", L"SaveFormat", 0, app.iniPath.c_str()) == 1,
                "JPG export retained the border or lost remembered options/persistence.");
        savedMatches(secondPath, app.graphics.exportImage(app.image, app.document.items));
        copyImage();
        clipboardMatches(renderedExport());
        saveRecentSnip(0, false);
        const auto recentJpgPath = std::filesystem::path(recentPath).replace_extension(L".jpg").wstring();
        const auto recentJpgBytes = readBytes(recentJpgPath);
        require(app.recent[0].savePath == recentJpgPath && recentJpgBytes.size() > 2 &&
                    recentJpgBytes[0] == 0xff && recentJpgBytes[1] == 0xd8 &&
                    app.graphics.decode(recentJpgBytes).width == app.recent[0].image.width,
                "Recent Save did not produce JPG without Professional Border.");
        command(SaveFormatPng);
        const auto returnFolder = saveRoot / L"back to PNG";
        std::filesystem::create_directory(returnFolder);
        setSaveFolder(returnFolder.wstring());
        quickSave();
        require(std::filesystem::path(app.savePath).extension() == L".png" &&
                    renderedExport().width == app.image.width + 40,
                "Switching back to PNG did not restore the extension and Professional Border.");
        savedMatches(app.savePath, renderedExport());
        require(saveExtensionMatches(L"image.PNG") && !saveExtensionMatches(L"image.jpg"),
                "PNG extension validation failed.");
        auto saveAs = [&](const std::wstring &path, bool jpg, bool cancel, int recent = -1) {
            saveDialogPath = path;
            saveDialogJpg = jpg;
            cancelSaveDialog = cancel;
            saveDialogSeen = saveDialogFiltersValid = false;
            switchedDialogName.clear();
            testing::saveDialogReady = prepareSaveDialog;
            if (recent < 0)
            {
                require(SetKeyboardState(ctrlKeyboard), "Cannot set private Ctrl+S keyboard state.");
                SendMessageW(app.window, WM_KEYDOWN, 'S', 0);
                require(SetKeyboardState(keyboard), "Cannot restore keyboard after Ctrl+S.");
            }
            else
                saveRecentSnip(recent, true);
            testing::saveDialogReady = nullptr;
            activeSaveDialog = nullptr;
            KillTimer(app.window, 54321);
            require(saveDialogSeen && saveDialogFiltersValid,
                    "Ctrl+S did not open the modern Save As dialog with safe save options.");
        };
        const auto pngPreferences = app.exportOptions;
        const auto previewBeforeSaveAs = renderedExport();
        const auto dialogJpgPath = (returnFolder / L"dropdown-export.jpg").wstring();
        saveAs(dialogJpgPath, true, false);
        const auto dialogJpg = app.graphics.decode(readBytes(dialogJpgPath));
        require(initialSaveFilter == 1 &&
                    app.savePath == dialogJpgPath && dialogJpg.width == app.image.width &&
                    dialogJpg.height == app.image.height && app.exportOptions == pngPreferences &&
                    renderedExport().pixels == previewBeforeSaveAs.pixels && initialSaveFolder == returnFolder.wstring(),
                "Save As JPG did not remove the border locally, update the extension or preserve PNG settings/preview.");
        const auto firstCopyBytes = readBytes(dialogJpgPath);
        const auto anotherFolder = saveRoot / L"another copy";
        std::filesystem::create_directory(anotherFolder);
        const auto secondCopyPath = (anotherFolder / L"dropdown-export.jpg").wstring();
        saveAs(secondCopyPath, true, false);
        require(app.savePath == secondCopyPath && readBytes(dialogJpgPath) == firstCopyBytes &&
                    readBytes(secondCopyPath) == firstCopyBytes && initialSaveFolder == returnFolder.wstring(),
                "Repeated Ctrl+S did not save the same snip in a second folder or changed the first copy.");
        const auto pathBeforeCancel = app.savePath;
        const auto cancelledPath = (returnFolder / L"cancelled-dropdown.png").wstring();
        saveAs(cancelledPath, false, true);
        require(app.savePath == pathBeforeCancel && !std::filesystem::exists(cancelledPath) &&
                    app.exportOptions == pngPreferences,
                "Cancelling Save As changed the save path, settings or created a file.");
        const auto dialogRecentJpgPath = (returnFolder / L"recent-dropdown.jpg").wstring();
        saveAs(dialogRecentJpgPath, true, false, 0);
        require(app.recent[0].savePath == dialogRecentJpgPath &&
                    app.graphics.decode(readBytes(dialogRecentJpgPath)).width == app.recent[0].image.width &&
                    app.savePath == pathBeforeCancel && app.exportOptions == pngPreferences,
                "Recent Save As JPG retained the border or changed the active editor/settings.");
        command(SaveFormatJpg);
        const auto jpgPreferences = app.exportOptions;
        const auto dialogPngPath = (returnFolder / L"dropdown-export.png").wstring();
        saveAs(dialogPngPath, false, false);
        auto pngOptions = jpgPreferences;
        pngOptions.jpg = false;
        savedMatches(dialogPngPath, app.graphics.exportImage(app.image, app.document.items, pngOptions));
        require(initialSaveFilter == 2 &&
                    app.exportOptions == jpgPreferences && renderedExport().width == app.image.width,
                "Save As PNG did not restore the border for that file while preserving JPG settings.");
        app.exportOptions.jpg = true;
        require(saveExtensionMatches(L"image.JPG") && saveExtensionMatches(L"image.jpeg") &&
                    !saveExtensionMatches(L"image.png"), "JPG extension validation failed.");
        app.exportOptions = {};
        resetPreview();
        app.saveFolder.clear();
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
                     "retention, redirected existing saves across multiple configured folders, "
                     "Recent save routing, destination feedback and unavailable-folder errors, "
                     "real JPG current/Recent saves, PNG/JPG switching and restored border, "
                     "native Save As format dropdown, extension updates, per-file effects and cancellation, "
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
