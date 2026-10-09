// Verify per-profile storage and one-time migration using synthetic profiles.
#include "../src/main.cpp"
#include <iostream>

int wmain()
{
    const auto root = std::filesystem::current_path() /
        (L"settings-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto portable = root / L"portable.ini";
    const auto missing = root / L"missing.ini";
    const auto first = root / L"first" / L"Tiger Snip" / L"TigerSnip.ini";
    const auto second = root / L"second" / L"Tiger Snip" / L"TigerSnip.ini";
    const auto third = root / L"third" / L"Tiger Snip" / L"TigerSnip.ini";
    auto require = [](bool ok, const char *message) {
        if (!ok) throw std::runtime_error(std::string(message) +
                                         (app.preferenceError.empty() ? "" : " " + app.preferenceError));
    };
    int result = 0;
    try
    {
        std::filesystem::create_directory(root);
        require(WritePrivateProfileStringW(L"Settings", L"SoftwareRendering", L"1", portable.c_str()),
                "Cannot create portable preferences.");
        require(WritePrivateProfileStringW(L"Settings", L"AutoCopy", L"0", portable.c_str()),
                "Cannot create portable auto-copy preference.");
        require(WritePrivateProfileStringW(L"Settings", L"ToolbarLayout", L"0", portable.c_str()),
                "Cannot create an existing classic UI preference.");
        require(WritePrivateProfileStringW(L"Settings", L"ColorTheme", L"1", portable.c_str()),
                "Cannot create an existing Orange theme preference.");
        require(WritePrivateProfileStringW(L"ToolPreferences", L"StrokeWidth", L"13", portable.c_str()),
                "Cannot create portable stroke preference.");
        require(SetFileAttributesW(portable.c_str(), FILE_ATTRIBUTE_READONLY),
                "Cannot make the migration source read-only.");
        require(settingsPathForProfile(root / L"first", portable) == first.wstring(),
                "Preferences must be under the requested user's profile.");
        app.iniPath = first.wstring();
        loadToolPreferences();
        require(app.softwareRendering && !app.autoCopy && app.thickness == 13 && app.classicUI,
                "Migration must preserve this PC's renderer and personal tool preferences.");
        require(app.colorTheme == 0 && app.appearancePreferencesDirty,
                "The retired Orange preset must migrate to Purple.");
        app.thickness = 17;
        app.toolPreferencesDirty = true;
        require(saveToolPreferences(), "Migrated personal preferences must be writable.");
        require(GetPrivateProfileIntW(L"Settings", L"ColorTheme", 99, first.c_str()) == 0 &&
                    GetPrivateProfileIntW(L"Settings", L"ColorTheme", 99, portable.c_str()) == 1,
                "Orange migration must persist without changing the read-only source.");
        settingsPathForProfile(root / L"first", portable);
        require(GetPrivateProfileIntW(L"ToolPreferences", L"StrokeWidth", 0, first.c_str()) == 17,
                "A later launch must not overwrite existing personal preferences.");

        require(settingsPathForProfile(root / L"second", missing) == second.wstring(),
                "Another user must have a separate destination.");
        app.iniPath = second.wstring();
        loadToolPreferences();
        require(!app.softwareRendering && app.autoCopy && app.thickness == 4,
                "A fresh user must retain the ordinary renderer and tool defaults.");
        require(!app.classicUI && app.colorTheme == 0 && !app.darkTheme,
                "A fresh installation must default to the new UI with the Purple light theme.");
        require(!app.exportOptions.jpg, "New users must default to PNG exports.");
        commitPreferences(app.iniPath, {{L"Settings", L"SaveFormat", L"1"},
                                       {L"Settings", L"ProfessionalBorder", L"1"},
                                       {L"Settings", L"ProfessionalBlur", L"1"},
                                       {L"Settings", L"ProfessionalRounded", L"1"}});
        loadToolPreferences();
        require(app.exportOptions.jpg && app.exportOptions.professionalBorder &&
                    app.exportOptions.professionalBlur && app.exportOptions.professionalRounded,
                "JPG did not reload while retaining the previous PNG border choices.");
        commitPreferences(app.iniPath, {{L"Settings", L"SaveFormat", L"99"}});
        loadToolPreferences();
        require(!app.exportOptions.jpg, "An invalid save format must fall back to PNG.");
        for (int theme : {2, 3, 4})
        {
            require(WritePrivateProfileStringW(L"Settings", L"ColorTheme",
                                               std::to_wstring(theme).c_str(), second.c_str()) &&
                        WritePrivateProfileStringW(L"Settings", L"CustomUIAccent", L"123456", second.c_str()),
                    "Cannot create a retained color preference.");
            loadToolPreferences();
            require(app.colorTheme == static_cast<unsigned>(theme) && app.customUIAccent == 123456,
                    "Removing Orange changed the stored Blue, Teal or Custom preference.");
        }
        app.thickness = 23;
        app.toolPreferencesDirty = true;
        require(saveToolPreferences(), "The second profile must be writable.");
        require(GetPrivateProfileIntW(L"ToolPreferences", L"StrokeWidth", 0, second.c_str()) == 23 &&
                GetPrivateProfileIntW(L"ToolPreferences", L"StrokeWidth", 0, first.c_str()) == 17 &&
                GetPrivateProfileIntW(L"ToolPreferences", L"StrokeWidth", 0, portable.c_str()) == 13,
                "Saving one user's preferences must not change another user or the release folder.");
        require(settingsPathForProfile(root / L"third", missing) == third.wstring() &&
                !std::filesystem::exists(third), "Locating settings must not distribute someone else's INI.");
        std::cout << "PASS: separate profile settings, read-only portable migration, no repeated overwrite, "
                     "ordinary defaults for new users, independent writes, no release-folder writes.\n";
    }
    catch (const std::exception &failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        result = 1;
    }
    // Only the known synthetic files and empty directories belong to this test.
    SetFileAttributesW(portable.c_str(), FILE_ATTRIBUTE_NORMAL);
    std::error_code ignored;
    std::filesystem::remove(portable, ignored);
    for (const auto &file : {first, second, third})
    {
        SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
        std::filesystem::remove(file, ignored);
        std::filesystem::remove(file.parent_path(), ignored);
        std::filesystem::remove(file.parent_path().parent_path(), ignored);
    }
    std::filesystem::remove(root, ignored);
    return result;
}
