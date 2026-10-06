param([switch]$HostTest)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Use PowerShell 7' }
$taskArgs = @((Join-Path $PSScriptRoot 'build.py'))
if ($HostTest) { $taskArgs += '--host' }
& (Join-Path $PSScriptRoot '.venv/Scripts/python.exe') @taskArgs
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
