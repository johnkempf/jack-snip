# Tiger Snip build toolchain

The PowerShell build uses **LLVM-MinGW 20260922, x64 UCRT**, with Clang 23.1.2.

Download the [toolchain archive](https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-x86_64.zip) and extract it into `.tools/llvm-mingw-20260922-ucrt-x86_64`.

Archive SHA256:

```text
E3AD77D117A4BEA19A7A3B333341824D79A5A371004A10E25B8504E7B3047666
```

Build, test, and package:

```powershell
.\build.ps1 -Test
.\package.ps1
```

The output is `dist/Tiger Snip.exe` and `dist/Tiger Snip Setup.msi`. The C++ runtime is statically linked; Windows provides the system graphics libraries. Runtime notices are included in the installer.

`scripts/prepare-ocr.ps1` prepares CMake 3.31.6 and builds the pinned Tesseract
5.5.3 and Leptonica 1.87.0 source archives in `.tools/ocr`. The first build needs
internet access; later builds reuse that cache. Archive hashes and options are
in `cmake/OcrDependencies.cmake` and the preparation script. Both engines are
static libraries, with networking, training tools, legacy OCR, and external
image codecs disabled. The checked-in English model is embedded in the executable.
The build record includes the static library hashes and all model/license inputs.

`Tiger Snip Build.json` records the compiler, build options, source hashes, and executable hash. Packaging checks these hashes. `Tiger Snip Release.txt` records the installer and payload hashes.
