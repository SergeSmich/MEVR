# SPDX-License-Identifier: MIT
# Starts ME2 through Steam (LEBinkProxy's -game 2) once SteamVR has been up 16 s and waits for Halcyon's XR session;
# with -Kill first closes a running (or hung) ME2 and its launcher, then starts it again; -Stop only closes them. Run
# prep.ps1 first.
param([switch]$Kill, [switch]$Stop)
if ($Stop) {
    Get-Process MassEffect2 -ErrorAction SilentlyContinue | Stop-Process -Force -Confirm:$false
    Start-Sleep 3
    Get-Process MassEffectLauncher -ErrorAction SilentlyContinue | Stop-Process -Confirm:$false
    'ME2 closed'
    exit 0
}
$current = Join-Path $env:LOCALAPPDATA 'MEVR\sessions\le2-current.json'
if (-not (Test-Path -LiteralPath $current)) { throw 'no ME2 stretch is open: run prep.ps1 first' }
if ($Kill) {
    Get-Process MassEffect2 -ErrorAction SilentlyContinue | Stop-Process -Force -Confirm:$false
    Start-Sleep 3
    Get-Process MassEffectLauncher -ErrorAction SilentlyContinue | Stop-Process -Confirm:$false
}
if (Get-Process MassEffect2 -ErrorAction SilentlyContinue) { throw 'ME2 is running' }
$vr = $null
for ($i = 0; $i -lt 30 -and -not $vr; $i++) { $vr = Get-Process vrserver -ErrorAction SilentlyContinue; if (-not $vr) { Start-Sleep 1 } }
if (-not $vr) { throw 'SteamVR is not running (prep.ps1 turns the fake headset on)' }
$wait = 16 - ((Get-Date) - $vr.StartTime).TotalSeconds
if ($wait -gt 0) { Start-Sleep -Seconds ([int][math]::Ceiling($wait)) }
$log = Join-Path $env:LOCALAPPDATA 'MELE2VR\MELE2VR_Log.txt'
$mark = (Get-Content $log -ErrorAction SilentlyContinue | Measure-Object).Count
Start-Process -FilePath 'C:\Program Files (x86)\Steam\steam.exe' -ArgumentList '-applaunch', '1328670', '-game', '2', '-autoterminate'
"asked Steam at $(Get-Date -Format HH:mm:ss)"
$p = $null
for ($i = 0; $i -lt 120 -and -not $p; $i++) { $p = Get-Process MassEffect2 -ErrorAction SilentlyContinue; if (-not $p) { Start-Sleep 2 } }
if (-not $p) { 'no MassEffect2 within 240 s (the EA app can sit on its splash for minutes: run again)'; exit 1 }
"MassEffect2 pid $($p.Id) started $($p.StartTime.ToString('HH:mm:ss'))"
$new = @()
for ($i = 0; $i -lt 40; $i++) {
    $new = Get-Content $log -ErrorAction SilentlyContinue | Select-Object -Skip $mark
    if ($new -match 'VR session ready') { break }
    Start-Sleep 2
}
$new | Select-String 'MEVR-BASELINE|Rendering |VR session ready' | ForEach-Object { $_.Line }
