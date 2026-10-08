#pragma once
#include "ocr_layout.h"
#include <memory>

namespace snip
{
class LocalTextEngine
{
    struct State;
    std::unique_ptr<State> state_;

  public:
    LocalTextEngine();
    ~LocalTextEngine();
    std::wstring readLine(const Bitmap &image, std::stop_token stop);
};
} // namespace snip
