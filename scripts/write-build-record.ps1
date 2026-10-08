param([Parameter(Mandatory)][string]$CompilerDirectory,
      [Parameter(Mandatory)][string]$OutputName,
      [Parameter(Mandatory)][string[]]$Arguments)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskTools = [IO.Path]::GetFullPath($CompilerDirectory)
$taskExe = Join-Path $taskRoot ('dist\' + $OutputName)
$taskVersion = @(& (Join-Path $taskTools 'clang++.exe') --version)
if ($LASTEXITCODE -ne 0) { throw 'Cannot record compiler version.' }
$taskCommit = $null
$taskDirty = $null
if ((Test-Path -LiteralPath (Join-Path $taskRoot '.git')) -and (Get-Command git -ErrorAction SilentlyContinue)) {
    Push-Location $taskRoot
    try {
        $taskCommit = & git rev-parse HEAD
        if ($LASTEXITCODE -ne 0) { throw 'Cannot record source base commit.' }
        $taskStatus = @(& git status --porcelain)
        if ($LASTEXITCODE -ne 0) { throw 'Cannot record working-tree state.' }
        $taskDirty = [bool]$taskStatus.Count
    } finally { Pop-Location }
}
$taskInputs = @('build.ps1', 'package.ps1', 'CMakeLists.txt', 'scripts\write-build-record.ps1', 'scripts\make-icon.ps1', 'scripts\run-test.ps1')
$taskInputs += Get-ChildItem -LiteralPath (Join-Path $taskRoot 'src') -File | ForEach-Object { 'src\' + $_.Name }
$taskInputs += Get-ChildItem -LiteralPath (Join-Path $taskRoot 'tests') -File -Recurse |
    ForEach-Object { $_.FullName.Substring($taskRoot.Length + 1) }
$taskInputs += Get-ChildItem -LiteralPath (Join-Path $taskRoot 'resources') -File -Recurse |
    ForEach-Object { $_.FullName.Substring($taskRoot.Length + 1) }
$taskHashes = @($taskInputs | Sort-Object -Unique | ForEach-Object {
    [ordered]@{ path = $_; sha256 = (Get-FileHash -LiteralPath (Join-Path $taskRoot $_) -Algorithm SHA256).Hash }
})
$taskToolHashes = @('clang++.exe', 'clang.exe', 'llvm-windres.exe', 'ld.lld.exe') | ForEach-Object {
    $taskToolPath = Join-Path $taskTools $_
    if (Test-Path -LiteralPath $taskToolPath) {
        [ordered]@{ name = $_; sha256 = (Get-FileHash -LiteralPath $taskToolPath -Algorithm SHA256).Hash }
    }
}
$taskRecord = [ordered]@{
    formatVersion = 1
    builtAtUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    sourceBaseCommit = $taskCommit
    workingTreeHasUncommittedChanges = $taskDirty
    compilerVersion = $taskVersion
    compilerFiles = @($taskToolHashes)
    buildArguments = @($Arguments | ForEach-Object { $_.Replace($taskRoot, '<repo>') })
    powershellVersion = $PSVersionTable.PSVersion.ToString()
    output = [ordered]@{ name = $OutputName; sha256 = (Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash }
    inputs = $taskHashes
}
$taskRecordName = if ($OutputName -eq 'Tiger Snip.exe') { 'Tiger Snip Build.json' } else { $OutputName + '.build.json' }
$taskRecord | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskRoot ('dist\' + $taskRecordName)) -Encoding UTF8
