#pragma once
#include "model.h"
#include <stop_token>

namespace snip
{
// Call on a worker thread. An empty result means no text was recognized.
std::wstring recognizeText(const Bitmap &image, std::stop_token stop = {});
} // namespace snip
