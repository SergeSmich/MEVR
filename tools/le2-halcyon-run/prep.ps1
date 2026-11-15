# SPDX-License-Identifier: MIT
# Prepares a fake-headset run of ME2 with Halcyon's renderer in our LE2 host (tools/qa-le2 can't drive this host yet: its
# responder is only in the diagnostic build). The first prep of a stretch of runs takes the run lock (run-lock.ps1) and
# backs up the three roots ME2 writes (the profile's saves and settings, ME2's Config, ME2's Win64) with hash manifests;
# a later prep by the same holder only redeploys. Then: LEBinkProxy into ME2's Win64 (copied from ME1's, where ME3Tweaks
# put it), this checkout's build-trilogy host with upstream's MELE2VR.ini, the borderless window, SteamVR's runtime pinned
# in ASI\LE2MEVR.ini, the fake headset on. restore.ps1 ends the stretch and puts everything back. See README.md.
param(
    [Parameter(Mandatory)][string]$Holder,
    [string]$Tag = 'le2-halcyon',
    [string]$BuildDir,
    # MELE2VR.ini [VR] Diagnostics=1 for this stretch: Halcyon's [ME2FP]/[ME2DISC]... lines and the F3 camera dump.
    [switch]$Diagnostics
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\common.ps1"
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $BuildDir) { $BuildDir = Join-Path $W 'build-trilogy' }
$G = Join-Path (Find-MELEInstall).Root 'Game'
$b = Get-MELEBinDir (Find-MELEInstall).Root 'LE2'
$docs = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BioWare\Mass Effect Legendary Edition'
$current = Join-Path $env:LOCALAPPDATA 'MEVR\sessions\le2-current.json'

if (Get-Process MassEffect1, MassEffect2, MassEffect3, MassEffectLauncher -ErrorAction SilentlyContinue) {
    throw 'a game or its launcher runs: close it first (launch.ps1 -Stop closes ME2)'
}
& powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\run-lock.ps1" -Take -Holder $Holder -Note "ME2 Halcyon runs ($Tag)"
if ($LASTEXITCODE -ne 0) { throw 'the run lock is not ours' }

$open = $null
if (Test-Path -LiteralPath $current) { $open = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json }
if ($open -and $open.holder -ne $Holder) { throw "an ME2 stretch by '$($open.holder)' was never restored ($($open.session)): restore it first" }

if (-not $open) {
    if (Test-Path "$b\bink2w64_original.dll") { throw "ME2 already has a loader but no stretch is open: someone else's install, not ours to touch" }
    $S = Join-Path $env:LOCALAPPDATA ("MEVR\sessions\" + (Get-Date -Format 'yyyyMMdd-HHmmss') + "-$Tag")
    New-Item -ItemType Directory -Force "$S\backup" | Out-Null
    robocopy $docs "$S\backup\profile" /E /COPY:DAT /DCOPY:T /R:2 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    robocopy "$G\ME2\BioGame\Config" "$S\backup\ME2Config" /E /COPY:DAT /R:2 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    robocopy $b "$S\backup\ME2Win64" /E /COPY:DAT /R:2 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    New-Item -ItemType Directory -Force "$S\backup\ME1ASI" | Out-Null
    Copy-Item "$G\ME1\Binaries\Win64\ASI\*.ini" "$S\backup\ME1ASI\" -ErrorAction SilentlyContinue
    foreach ($r in @(@($docs, 'profile'), @("$G\ME2\BioGame\Config", 'ME2Config'), @($b, 'ME2Win64'))) {
        Get-ChildItem $r[0] -Recurse -File | ForEach-Object { [pscustomobject]@{ rel = $_.FullName.Substring($r[0].Length); hash = (Get-FileHash $_.FullName).Hash } } |
            ConvertTo-Json -Depth 3 | Set-Content -Encoding utf8 "$S\before-$($r[1]).json"
    }
    [pscustomobject]@{ holder = $Holder; session = $S; since = (Get-Date -Format 's') } | ConvertTo-Json |
        Set-Content -LiteralPath $current -Encoding utf8
    "backed up into $S"

    $proxy = Get-Item "$G\ME1\Binaries\Win64\bink2w64.dll"
    if ($proxy.VersionInfo.ProductName -notmatch 'LEBinkProxy') { throw "ME1's bink2w64.dll is not LEBinkProxy: nothing to copy" }
    if ((Get-Item "$b\bink2w64.dll").VersionInfo.ProductName -ne 'Bink and Smacker') { throw "ME2's bink2w64.dll is not the game's own" }
    Rename-Item "$b\bink2w64.dll" 'bink2w64_original.dll'
    Copy-Item $proxy.FullName "$b\bink2w64.dll"
} else {
    "the stretch in $($open.session) is open: redeploying only"
}

& powershell -NoProfile -ExecutionPolicy Bypass -File "$W\tools\deploy-asi.ps1" -Game LE2 -BuildDir $BuildDir
if ($LASTEXITCODE -ne 0) { throw 'deploy failed' }
& powershell -NoProfile -ExecutionPolicy Bypass -File "$W\tools\vr-game-settings.ps1" -Game LE2 | Select-String 'Borderless|Fullscreen|Applied'
$steamvr = 'C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json'
[IO.File]::WriteAllText("$b\ASI\LE2MEVR.ini", "[Debug]`r`nopenxrRuntime = $steamvr`r`n")
$ini = Join-Path $b 'MELE2VR.ini'
if ($Diagnostics -and (Test-Path $ini)) {
    $t = [IO.File]::ReadAllText($ini) -replace '(?m)^Diagnostics=0', 'Diagnostics=1'
    [IO.File]::WriteAllText($ini, $t); 'MELE2VR.ini: Diagnostics=1 for this stretch'
}
& powershell -NoProfile -ExecutionPolicy Bypass -File "$W\tools\fake-headset.ps1" -On | Select-Object -Last 1
