#pragma once

// Fault injection exists only in dedicated test builds.
#ifdef TIGER_SNIP_TESTING
struct IFileSaveDialog;
namespace snip::testing
{
inline void (*graphicsCheckpoint)(unsigned) = nullptr;
inline void (*settingsCheckpoint)(unsigned) = nullptr;
inline void (*callbackCheckpoint)(const char *, unsigned) = nullptr;
inline void (*errorSink)(const char *) = nullptr;
inline void (*fileSaveCheckpoint)(const wchar_t *, const wchar_t *) = nullptr;
inline void (*saveDialogReady)(IFileSaveDialog *) = nullptr;
} // namespace snip::testing
#endif
