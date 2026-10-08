param([string]$CompilerDirectory, [switch]$Test, [string]$OutputName = 'Tiger Snip.exe')
$ErrorActionPreference = 'Stop'
if ($OutputName -notmatch '^[A-Za-z0-9][A-Za-z0-9._ -]*\.exe$') { throw 'OutputName must be an executable filename.' }
$taskRoot = $PSScriptRoot
if (-not $CompilerDirectory) {
    # Keep ordinary rebuilds on the verified toolchain; newer downloads are not selected silently.
    $CompilerDirectory = Join-Path $taskRoot '.tools\llvm-mingw-20260922-ucrt-x86_64\bin'
}
if (-not $CompilerDirectory -or -not (Test-Path -LiteralPath (Join-Path $CompilerDirectory 'clang++.exe'))) {
    throw 'Provide -CompilerDirectory with an LLVM-MinGW bin folder, or use CMake with Visual Studio C++ Build Tools. See README.md.'
}
$taskCompiler = Join-Path $CompilerDirectory 'clang++.exe'
$taskResourceCompiler = Join-Path $CompilerDirectory 'llvm-windres.exe'
& (Join-Path $taskRoot 'scripts\prepare-ocr.ps1') -CompilerDirectory $CompilerDirectory
$taskOcrLibraries = @((Join-Path $taskRoot '.tools\ocr\lib\libtesseract.a'), (Join-Path $taskRoot '.tools\ocr\lib\libleptonica.a'))
$taskSources = @('model', 'graphics', 'windows_support', 'settings', 'color_picker', 'capture', 'clipboard', 'file_io', 'test_reports', 'ocr', 'ocr_layout', 'ocr_local', 'text_notice') |
    ForEach-Object { Join-Path $taskRoot "src\$_.cpp" }
$taskBuild = Join-Path $taskRoot 'build'
$taskDist = Join-Path $taskRoot 'dist'
New-Item -ItemType Directory -Force -Path $taskBuild, $taskDist | Out-Null
$taskIcon = Join-Path $taskRoot 'resources\tiger-snip.ico'
if (-not (Test-Path -LiteralPath $taskIcon)) { & (Join-Path $taskRoot 'scripts\make-icon.ps1') }
Push-Location (Join-Path $taskRoot 'resources')
try {
    & $taskResourceCompiler -i app.rc -o (Join-Path $taskBuild 'app.res.o') -O coff
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
} finally { Pop-Location }
$taskArguments = @('-std=c++20','-Os','-Wall','-Wextra','-Wpedantic','-DUNICODE','-D_UNICODE','-DWIN32_LEAN_AND_MEAN','-DNOMINMAX','-D_WIN32_WINNT=0x0A00',
    (Join-Path $taskRoot 'src\main.cpp')) + $taskSources + @((Join-Path $taskBuild 'app.res.o')) + $taskOcrLibraries + @(
    '-o',(Join-Path $taskDist $OutputName),'-municode','-mwindows','-static','-Wl,--nxcompat','-Wl,--dynamicbase','-Wl,--high-entropy-va','-s',
    '-ld2d1','-ldwrite','-lwindowscodecs','-lole32','-luser32','-lgdi32','-lcomdlg32','-lcomctl32','-lshell32','-ladvapi32','-ldwmapi','-luuid','-lruntimeobject','-lws2_32')
& $taskCompiler @taskArguments
if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed.' }
& (Join-Path $taskRoot 'scripts\write-build-record.ps1') -CompilerDirectory $CompilerDirectory -OutputName $OutputName -Arguments $taskArguments
Get-Item -LiteralPath (Join-Path $taskDist $OutputName) | Select-Object FullName, Length, LastWriteTime
Copy-Item -Path (Join-Path $taskRoot 'resources\licenses\*.txt') -Destination $taskDist
if ($Test) {
    $taskClipboardArguments = @('-std=c++20','-Os','-Wall','-Wextra','-Wpedantic','-DUNICODE','-D_UNICODE','-DWIN32_LEAN_AND_MEAN','-DNOMINMAX','-D_WIN32_WINNT=0x0A00',
        '-I',(Join-Path $taskRoot 'src'),(Join-Path $taskRoot 'tests\clipboard.cpp')) + $taskSources + @((Join-Path $taskBuild 'app.res.o')) + $taskOcrLibraries + @(
        '-o',(Join-Path $taskBuild 'clipboard_test.exe'),'-municode','-static','-s','-ld2d1','-ldwrite','-lwindowscodecs','-lole32','-luser32','-lgdi32','-ladvapi32','-lcomctl32','-lshell32','-luuid','-lruntimeobject','-lws2_32')
    & $taskCompiler @taskClipboardArguments
    if ($LASTEXITCODE -ne 0) { throw 'Clipboard test compilation failed.' }
    $taskAutoCopyArguments = @($taskArguments)
    $taskAutoCopyArguments[$taskAutoCopyArguments.IndexOf((Join-Path $taskRoot 'src\main.cpp'))] = Join-Path $taskRoot 'tests\autocopy.cpp'
    $taskAutoCopyArguments[$taskAutoCopyArguments.IndexOf((Join-Path $taskDist $OutputName))] = Join-Path $taskBuild 'autocopy_test.exe'
    $taskAutoCopyArguments = @($taskAutoCopyArguments | Where-Object { $_ -ne '-mwindows' })
    & $taskCompiler @taskAutoCopyArguments
    if ($LASTEXITCODE -ne 0) { throw 'Auto copy test compilation failed.' }
    $taskFileSaveArguments = @($taskClipboardArguments) + '-DTIGER_SNIP_TESTING'
    $taskFileSaveArguments[$taskFileSaveArguments.IndexOf((Join-Path $taskRoot 'tests\clipboard.cpp'))] = Join-Path $taskRoot 'tests\file_save.cpp'
    $taskFileSaveArguments[$taskFileSaveArguments.IndexOf((Join-Path $taskBuild 'clipboard_test.exe'))] = Join-Path $taskBuild 'file_save_test.exe'
    & $taskCompiler @taskFileSaveArguments
    if ($LASTEXITCODE -ne 0) { throw 'File save test compilation failed.' }
    $taskSettingsArguments = @($taskAutoCopyArguments)
    $taskSettingsArguments[$taskSettingsArguments.IndexOf((Join-Path $taskRoot 'tests\autocopy.cpp'))] = Join-Path $taskRoot 'tests\settings.cpp'
    $taskSettingsArguments[$taskSettingsArguments.IndexOf((Join-Path $taskBuild 'autocopy_test.exe'))] = Join-Path $taskBuild 'settings_test.exe'
    & $taskCompiler @taskSettingsArguments
    if ($LASTEXITCODE -ne 0) { throw 'Settings test compilation failed.' }
    $taskRobustnessArguments = @($taskSettingsArguments) + '-DTIGER_SNIP_TESTING'
    $taskRobustnessArguments[$taskRobustnessArguments.IndexOf((Join-Path $taskRoot 'tests\settings.cpp'))] = Join-Path $taskRoot 'tests\robustness.cpp'
    $taskRobustnessArguments[$taskRobustnessArguments.IndexOf((Join-Path $taskBuild 'settings_test.exe'))] = Join-Path $taskBuild 'robustness_test.exe'
    & $taskCompiler @taskRobustnessArguments
    if ($LASTEXITCODE -ne 0) { throw 'Robustness test compilation failed.' }
    $taskTextArguments = @($taskAutoCopyArguments)
    $taskTextArguments[$taskTextArguments.IndexOf((Join-Path $taskRoot 'tests\autocopy.cpp'))] = Join-Path $taskRoot 'tests\text_edit.cpp'
    $taskTextArguments[$taskTextArguments.IndexOf((Join-Path $taskBuild 'autocopy_test.exe'))] = Join-Path $taskBuild 'text_edit_test.exe'
    & $taskCompiler @taskTextArguments
    if ($LASTEXITCODE -ne 0) { throw 'Text editing test compilation failed.' }
    $taskTextCaptureArguments = @($taskAutoCopyArguments)
    $taskTextCaptureArguments[$taskTextCaptureArguments.IndexOf((Join-Path $taskRoot 'tests\autocopy.cpp'))] = Join-Path $taskRoot 'tests\text_capture.cpp'
    $taskTextCaptureArguments[$taskTextCaptureArguments.IndexOf((Join-Path $taskBuild 'autocopy_test.exe'))] = Join-Path $taskBuild 'text_capture_test.exe'
    & $taskCompiler @taskTextCaptureArguments
    if ($LASTEXITCODE -ne 0) { throw 'Text capture test compilation failed.' }
    $taskUIArguments = @($taskAutoCopyArguments)
    $taskUIArguments[$taskUIArguments.IndexOf((Join-Path $taskRoot 'tests\autocopy.cpp'))] = Join-Path $taskRoot 'tests\ui.cpp'
    $taskUIArguments[$taskUIArguments.IndexOf((Join-Path $taskBuild 'autocopy_test.exe'))] = Join-Path $taskBuild 'ui_test.exe'
    & $taskCompiler @taskUIArguments
    if ($LASTEXITCODE -ne 0) { throw 'UI test compilation failed.' }
    $taskMenuArguments = @($taskUIArguments)
    $taskMenuArguments[$taskMenuArguments.IndexOf((Join-Path $taskRoot 'tests\ui.cpp'))] = Join-Path $taskRoot 'tests\native_menu.cpp'
    $taskMenuArguments[$taskMenuArguments.IndexOf((Join-Path $taskBuild 'ui_test.exe'))] = Join-Path $taskBuild 'native_menu_test.exe'
    & $taskCompiler @taskMenuArguments
    if ($LASTEXITCODE -ne 0) { throw 'Native menu test compilation failed.' }
    $taskInstanceArguments = @($taskClipboardArguments)
    $taskInstanceArguments[$taskInstanceArguments.IndexOf((Join-Path $taskRoot 'tests\clipboard.cpp'))] = Join-Path $taskRoot 'tests\single_instance.cpp'
    $taskInstanceArguments[$taskInstanceArguments.IndexOf((Join-Path $taskBuild 'clipboard_test.exe'))] = Join-Path $taskBuild 'single_instance_test.exe'
    & $taskCompiler @taskInstanceArguments
    if ($LASTEXITCODE -ne 0) { throw 'Single-instance test compilation failed.' }
    # Short IDs leave room for transactional settings filenames on deep checkouts.
    $taskRunRoot = Join-Path $taskBuild ('t\' + [guid]::NewGuid().ToString('N').Substring(0, 12))
    New-Item -ItemType Directory -Path $taskRunRoot | Out-Null
    Push-Location $taskRunRoot
    try {
        $taskProcess = Start-Process -FilePath (Join-Path $taskDist $OutputName) -ArgumentList '--self-test' -WindowStyle Hidden -Wait -PassThru
        if ($taskProcess.ExitCode -ne 0) { throw 'Native self-test failed.' }
        $taskReports = @(Get-ChildItem -LiteralPath 'test-output' -Filter 'self-test-results.txt' -Recurse)
        if ($taskReports.Count -ne 1) { throw 'This invocation did not produce exactly one native test report.' }
        Get-Content -LiteralPath $taskReports[0].FullName
        & (Join-Path $taskBuild 'clipboard_test.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Isolated clipboard test failed.' }
        & (Join-Path $taskBuild 'autocopy_test.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Isolated auto copy test failed.' }
        & (Join-Path $taskBuild 'file_save_test.exe')
        if ($LASTEXITCODE -ne 0) { throw 'File save test failed.' }
        & (Join-Path $taskBuild 'settings_test.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Settings test failed.' }
        & (Join-Path $taskBuild 'robustness_test.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Robustness test failed.' }
        & (Join-Path $taskBuild 'text_edit_test.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Text editing test failed.' }
        & (Join-Path $taskBuild 'text_capture_test.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Text capture test failed.' }
        & (Join-Path $taskBuild 'ui_test.exe')
        if ($LASTEXITCODE -ne 0) { throw 'UI test failed.' }
        & (Join-Path $taskBuild 'native_menu_test.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Native menu frame test failed.' }
        & (Join-Path $taskBuild 'single_instance_test.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Single-instance test failed.' }
        Write-Host "Test files: $taskRunRoot"
    } finally { Pop-Location }
}
