#include "clipboard.h"
#include "windows_support.h"
#include <cstring>
#include <stdexcept>

namespace snip
{
bool copyText(HWND owner, const std::wstring &text, ClipboardFailure *failure)
{
    if (text.empty())
        return false;
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory)
        throw std::runtime_error("Not enough memory to copy the text.");
    void *data = GlobalLock(memory);
    if (!data)
    {
        GlobalFree(memory);
        throwWindowsError("Cannot prepare the text clipboard.");
    }
    std::memcpy(data, text.c_str(), bytes);
    GlobalUnlock(memory);
    if (!OpenClipboard(owner))
    {
        if (failure)
            *failure = {"Clipboard is busy. Try the text snip again.", GetLastError(), true};
        GlobalFree(memory);
        return false;
    }
    const bool success = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
    if (!success && failure)
        *failure = {"Cannot copy text to the clipboard.", GetLastError(), false};
    CloseClipboard();
    if (!success)
        GlobalFree(memory);
    return success;
}

bool copyBitmap(HWND owner, const Bitmap &bitmap, const std::vector<uint8_t> &png,
                ClipboardFailure *failure)
{
    const size_t imageSize = bitmap.pixels.size();
    auto allocate = [&](const void *header, size_t headerSize, const uint8_t *pixels,
                        size_t count) -> HGLOBAL {
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, headerSize + count);
        if (!memory)
            return nullptr;
        auto data = static_cast<uint8_t *>(GlobalLock(memory));
        if (!data)
        {
            GlobalFree(memory);
            return nullptr;
        }
        if (headerSize)
            std::memcpy(data, header, headerSize);
        std::memcpy(data + headerSize, pixels, count);
        GlobalUnlock(memory);
        return memory;
    };
    BITMAPV5HEADER v5{};
    v5.bV5Size = sizeof(v5);
    v5.bV5Width = bitmap.width;
    v5.bV5Height = -bitmap.height;
    v5.bV5Planes = 1;
    v5.bV5BitCount = 32;
    v5.bV5Compression = BI_BITFIELDS;
    v5.bV5SizeImage = static_cast<DWORD>(imageSize);
    v5.bV5RedMask = 0x00ff0000;
    v5.bV5GreenMask = 0x0000ff00;
    v5.bV5BlueMask = 0x000000ff;
    v5.bV5AlphaMask = 0xff000000;
    v5.bV5CSType = LCS_sRGB;
    v5.bV5Intent = LCS_GM_IMAGES;
    BITMAPINFOHEADER dib{};
    dib.biSize = sizeof(dib);
    dib.biWidth = bitmap.width;
    dib.biHeight = -bitmap.height;
    dib.biPlanes = 1;
    dib.biBitCount = 32;
    dib.biCompression = BI_RGB;
    dib.biSizeImage = static_cast<DWORD>(imageSize);
    HGLOBAL hV5 = allocate(&v5, sizeof(v5), bitmap.pixels.data(), imageSize),
            hDib = allocate(&dib, sizeof(dib), bitmap.pixels.data(), imageSize),
            hPng = allocate(nullptr, 0, png.data(), png.size());
    if (!hV5 || !hDib || !hPng)
    {
        if (hV5)
            GlobalFree(hV5);
        if (hDib)
            GlobalFree(hDib);
        if (hPng)
            GlobalFree(hPng);
        throw std::runtime_error("Not enough memory to copy the image.");
    }
    UINT pngFormat = RegisterClipboardFormatW(L"PNG");
    if (!OpenClipboard(owner))
    {
        if (failure)
            *failure = {"Cannot open the Windows clipboard. Another program may be using it.",
                        GetLastError(), true};
        GlobalFree(hV5);
        GlobalFree(hDib);
        GlobalFree(hPng);
        return false;
    }
    if (!EmptyClipboard())
    {
        if (failure)
            *failure = {"Cannot prepare the Windows clipboard for the image.", GetLastError()};
        CloseClipboard();
        GlobalFree(hV5);
        GlobalFree(hDib);
        GlobalFree(hPng);
        return false;
    }
    bool success = false;
    // Advertise the lossless alpha-preserving format first to clipboard consumers.
    if (pngFormat && SetClipboardData(pngFormat, hPng))
    {
        hPng = nullptr;
        success = true;
    }
    if (SetClipboardData(CF_DIBV5, hV5))
    {
        hV5 = nullptr;
        success = true;
    }
    if (SetClipboardData(CF_DIB, hDib))
    {
        hDib = nullptr;
        success = true;
    }
    if (!success && failure)
        failure->code = GetLastError();
    CloseClipboard();
    if (hV5)
        GlobalFree(hV5);
    if (hDib)
        GlobalFree(hDib);
    if (hPng)
        GlobalFree(hPng);
    return success;
}
} // namespace snip
