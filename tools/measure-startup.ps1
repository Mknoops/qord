# Measure cold start (process launch -> main window shown) and idle memory.
# Launches the client $Runs times with no account, waits $IdleSeconds after the
# window appears, samples the working set and private bytes, then closes it.
param(
    [string]$Exe = "build\acheron.exe",
    [int]$Runs = 5,
    [int]$IdleSeconds = 15,
    [string]$QtDir = "C:\Qt\6.10.2\msvc2022_64"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exePath = (Resolve-Path (Join-Path $root $Exe)).Path
$env:PATH = "$QtDir\bin;$(Join-Path $root 'deps\ffmpeg\bin');$(Split-Path $exePath);$env:PATH"

$results = @()
for ($i = 1; $i -le $Runs; $i++) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $exePath -PassThru
    while ($p.MainWindowHandle -eq 0 -and -not $p.HasExited -and $sw.ElapsedMilliseconds -lt 30000) {
        Start-Sleep -Milliseconds 10
        $p.Refresh()
    }
    $startMs = $sw.ElapsedMilliseconds
    Start-Sleep -Seconds $IdleSeconds
    $p.Refresh()
    $results += [pscustomobject]@{
        Run = $i
        StartMs = $startMs
        WorkingSetMiB = [math]::Round($p.WorkingSet64 / 1MB, 1)
        PrivateMiB = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
    }
    $p.CloseMainWindow() | Out-Null
    if (-not $p.WaitForExit(5000)) { $p.Kill() }
    Start-Sleep -Seconds 1
}
$results | Format-Table -AutoSize | Out-String | Write-Output
function Median($xs) { $s = $xs | Sort-Object; $s[[int][math]::Floor(($s.Count - 1) / 2)] }
[pscustomobject]@{
    MedianStartMs = Median ($results.StartMs)
    MedianWorkingSetMiB = Median ($results.WorkingSetMiB)
    MedianPrivateMiB = Median ($results.PrivateMiB)
    BinaryMiB = [math]::Round((Get-Item $exePath).Length / 1MB, 2)
} | Format-List | Out-String | Write-Output
