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

Text recognition uses Windows OCR through WinRT. No third-party OCR engine or
model is linked, downloaded, or packaged on `main`. Windows supplies the recognition
components; the app maintains its own selection preprocessing and clipboard logic.

`Tiger Snip Build.json` records the compiler, build options, source hashes, and executable hash. Packaging checks these hashes. `Tiger Snip Release.txt` records the installer and payload hashes.
