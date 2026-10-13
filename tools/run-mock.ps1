# Runs the Qord client against the local mock Discord server
# (tools/mock_discord.py). Uses a separate "Qord-Mock" profile; no real
# account, token or network access to Discord is involved.
#
#   .\tools\run-mock.ps1              # start server (if needed) and client
#   .\tools\run-mock.ps1 -Reset       # wipe the mock profile first
param(
    [string]$Exe = "build\qord.exe",
    [string]$QtDir = "C:\Qt\6.10.2\msvc2022_64",
    [int]$Port = 8765,
    [switch]$Reset,
    [switch]$NoClient
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exePath = (Resolve-Path (Join-Path $root $Exe)).Path

if ($Reset) {
    $profileDir = Join-Path $env:APPDATA "ouwou\Qord-Mock"
    if (Test-Path $profileDir) { Remove-Item -Recurse -Force $profileDir }
    $localDir = Join-Path $env:LOCALAPPDATA "ouwou\Qord-Mock"
    if (Test-Path $localDir) { Remove-Item -Recurse -Force $localDir }
}

$listening = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
if (-not $listening) {
    $log = Join-Path $env:TEMP "qord-mock-server.log"
    Start-Process -FilePath python -ArgumentList @((Join-Path $root "tools\mock_discord.py"), "--port", $Port) `
        -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError "$log.err" | Out-Null
    Start-Sleep -Milliseconds 800
    Write-Host "mock server log: $log"
}

if ($NoClient) { return }

$env:PATH = "$QtDir\bin;$(Join-Path $root 'deps\ffmpeg\bin');$(Split-Path $exePath);$env:PATH"
$env:QORD_MOCK_DISCORD = "http://127.0.0.1:$Port"
$p = Start-Process -FilePath $exePath -WorkingDirectory (Split-Path $exePath) -PassThru `
    -RedirectStandardOutput (Join-Path $env:TEMP "qord-mock-client.log") `
    -RedirectStandardError (Join-Path $env:TEMP "qord-mock-client.err.log")
Write-Host "client pid $($p.Id)"
