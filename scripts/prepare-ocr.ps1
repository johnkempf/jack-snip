param([Parameter(Mandatory)][string]$CompilerDirectory)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskOcr = Join-Path $taskRoot '.tools\ocr'
New-Item -ItemType Directory -Force -Path $taskOcr | Out-Null
$taskCmakeArchive = Join-Path $taskOcr 'cmake.zip'
$taskCmake = Join-Path $taskOcr 'cmake-3.31.6-windows-x86_64\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $taskCmake)) {
    if (-not (Test-Path -LiteralPath $taskCmakeArchive)) {
        Invoke-WebRequest 'https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-windows-x86_64.zip' -OutFile $taskCmakeArchive
    }
    if ((Get-FileHash -LiteralPath $taskCmakeArchive).Hash -ne 'D163CD3AB4959B0A53FA8988F2DDBD2E6C501658201E6A154386BAD9DBE4F836') { throw 'CMake archive checksum mismatch.' }
    Expand-Archive -LiteralPath $taskCmakeArchive -DestinationPath $taskOcr -Force
}
$taskLibraryDirectory = Join-Path $taskOcr 'lib'
$taskBuild = Join-Path $taskOcr 'static-build'
$taskFlags = @('-S',(Join-Path $PSScriptRoot 'ocr'),'-B',$taskBuild,'-G','MinGW Makefiles',
    ('-DCMAKE_MAKE_PROGRAM=' + (Join-Path $CompilerDirectory 'mingw32-make.exe')),
    ('-DCMAKE_C_COMPILER=' + (Join-Path $CompilerDirectory 'clang.exe')),
    ('-DCMAKE_CXX_COMPILER=' + (Join-Path $CompilerDirectory 'clang++.exe')),
    '-DCMAKE_BUILD_TYPE=Release',('-DSNIP_OCR_ARCHIVE_DIRECTORY=' + $taskLibraryDirectory))
# Reuse previously extracted pinned archives when present; fresh builds use the
# hash-verified FetchContent downloads in OcrDependencies.cmake.
foreach ($taskSource in @(@('LEPTONICA','leptonica','1.87.0'),@('TESSERACT','tesseract','5.5.3'))) {
    $taskSourcePath = Join-Path $taskOcr ($taskSource[1] + '-' + $taskSource[2])
    $taskSourceArchive = Join-Path $taskOcr ($taskSource[1] + '.zip')
    $taskExpectedHash = if ($taskSource[1] -eq 'leptonica') { '2CFB7EBE6036F3D017280FA9A000C9699AB28BE8DFF8BE8E2E948E2C79917EDA' } else { '697D7BF55B53A6C90F5041FFA548F7085AEE921DA45342C42D57B7CBCB2FA16D' }
    if ((Test-Path -LiteralPath $taskSourceArchive) -and (Test-Path -LiteralPath $taskSourcePath)) {
        if ((Get-FileHash -LiteralPath $taskSourceArchive).Hash -ne $taskExpectedHash) { throw 'OCR source checksum mismatch.' }
        $taskFlags += '-DFETCHCONTENT_SOURCE_DIR_SNIP_' + $taskSource[0] + '=' + $taskSourcePath
    }
}
$taskLog = Join-Path $taskRoot 'build\ocr-engine'
New-Item -ItemType Directory -Force -Path $taskLog | Out-Null
& $taskCmake @taskFlags *> (Join-Path $taskLog 'configure.log')
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $taskLog 'configure.log') -Tail 40; throw 'OCR dependency configuration failed.' }
& $taskCmake --build $taskBuild --target libtesseract --parallel 8 *> (Join-Path $taskLog 'build.log')
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $taskLog 'build.log') -Tail 40; throw 'OCR dependency compilation failed.' }
