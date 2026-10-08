param([Parameter(Mandatory)][string]$Executable, [Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference = 'Stop'
$taskRun = Join-Path $OutputRoot ([guid]::NewGuid().ToString('N').Substring(0, 12))
New-Item -ItemType Directory -Path $taskRun -Force | Out-Null
Push-Location $taskRun
try {
    & $Executable
    if ($LASTEXITCODE -ne 0) { throw "Test failed with exit code $LASTEXITCODE. Output: $taskRun" }
    Write-Host "Test output: $taskRun"
} finally { Pop-Location }
