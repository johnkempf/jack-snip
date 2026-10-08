#pragma once
#include "model.h"
#include <windows.h>
namespace snip
{
struct ClipboardFailure
{
    const char *operation = "Cannot publish the image to the Windows clipboard.";
    DWORD code = ERROR_GEN_FAILURE;
    bool unavailable = false;
};
bool copyBitmap(HWND owner, const Bitmap &bitmap, const std::vector<uint8_t> &png,
                ClipboardFailure *failure = nullptr);
bool copyText(HWND owner, const std::wstring &text, ClipboardFailure *failure = nullptr);
} // namespace snip
