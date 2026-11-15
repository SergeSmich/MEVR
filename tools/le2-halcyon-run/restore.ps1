# SPDX-License-Identifier: MIT
# Ends a stretch of ME2 runs that prep.ps1 began: logs kept, the fake headset off, the three backed-up roots put back to
# their manifests (changed files copied back, files the runs added moved aside into the session folder, never deleted),
# every file checked hash for hash, the run lock released. Once per task, not after every run.
param([Parameter(Mandatory)][string]$Holder)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\common.ps1"
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
$current = Join-Path $env:LOCALAPPDATA 'MEVR\sessions\le2-current.json'
if (-not (Test-Path -LiteralPath $current)) { throw 'no ME2 stretch is open' }
$open = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
if ($open.holder -ne $Holder) { throw "the open stretch is '$($open.holder)'s, not '$Holder''s" }
$S = $open.session
$G = Join-Path (Find-MELEInstall).Root 'Game'
$docs = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BioWare\Mass Effect Legendary Edition'
if (Get-Process MassEffect1, MassEffect2, MassEffect3, MassEffectLauncher -ErrorAction SilentlyContinue) { throw 'a game is running: launch.ps1 -Stop closes ME2' }

New-Item -ItemType Directory -Force "$S\logs" | Out-Null
Copy-Item "$G\ME2\Binaries\Win64\Logs\LE2MEVR.log" "$S\logs\" -ErrorAction SilentlyContinue
Copy-Item "$env:LOCALAPPDATA\MELE2VR\MELE2VR_Log.txt" "$S\logs\" -ErrorAction SilentlyContinue
Copy-Item "$env:LOCALAPPDATA\MEVR\crash-breadcrumbs.txt" "$S\logs\" -ErrorAction SilentlyContinue
"logs kept in $S\logs"

& powershell -NoProfile -ExecutionPolicy Bypass -File "$W\tools\fake-headset.ps1" -Off

$roots = @(@($docs, 'profile'), @("$G\ME2\BioGame\Config", 'ME2Config'), @("$G\ME2\Binaries\Win64", 'ME2Win64'))
foreach ($r in $roots) {
    $live = $r[0]; $tag = $r[1]
    $before = @{}; foreach ($e in (Get-Content "$S\before-$tag.json" -Raw | ConvertFrom-Json)) { $before[$e.rel] = $e.hash }
    $aside = "$S\added\$tag"
    $changed = 0; $added = 0
    foreach ($f in Get-ChildItem $live -Recurse -File) {
        $rel = $f.FullName.Substring($live.Length)
        if ($before.ContainsKey($rel)) {
            if ((Get-FileHash $f.FullName).Hash -ne $before[$rel]) {
                Copy-Item "$S\backup\$tag$rel" $f.FullName -Force; $changed++; "restored ($tag): $rel"
            }
        } else {
            $to = Join-Path $aside $rel; New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
            Move-Item $f.FullName $to -Force; $added++; "moved aside ($tag): $rel"
        }
    }
    foreach ($rel in $before.Keys) {
        if (-not (Test-Path ($live + $rel))) {
            New-Item -ItemType Directory -Force (Split-Path ($live + $rel)) | Out-Null
            Copy-Item "$S\backup\$tag$rel" ($live + $rel) -Force; $changed++; "put back missing ($tag): $rel"
        }
    }
    Get-ChildItem $live -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending | Where-Object { -not (Get-ChildItem $_.FullName -Force) } | ForEach-Object {
        $rel = $_.FullName.Substring($live.Length)
        if (-not (Test-Path "$S\backup\$tag$rel")) { Remove-Item $_.FullName -Confirm:$false; "removed empty folder ($tag): $rel" }
    }
    "${tag}: $changed restored, $added moved aside"
}

$bad = 0
foreach ($r in $roots) {
    $live = $r[0]; $tag = $r[1]
    $before = Get-Content "$S\before-$tag.json" -Raw | ConvertFrom-Json
    $now = @{}; foreach ($f in Get-ChildItem $live -Recurse -File) { $now[$f.FullName.Substring($live.Length)] = (Get-FileHash $f.FullName).Hash }
    foreach ($e in $before) { if ($now[$e.rel] -ne $e.hash) { $bad++; "DIFFERS ($tag): $($e.rel)" } }
    if ($now.Count -ne @($before).Count) { $bad++; "COUNT ($tag): $($now.Count) now, $(@($before).Count) before" }
}
"verification: $bad differences"
$i = "$G\ME1\Binaries\Win64\ASI\LE1MEVR.ini"
if ((Test-Path $i) -and (Test-Path "$S\backup\ME1ASI\LE1MEVR.ini")) {
    "LE1MEVR.ini as before: $((Get-FileHash $i).Hash -eq (Get-FileHash "$S\backup\ME1ASI\LE1MEVR.ini").Hash)"
}
if ($bad -ne 0) { throw "the stretch is left open: $bad differences (the backup is in $S\backup)" }
Remove-Item -LiteralPath $current -Confirm:$false
& powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\run-lock.ps1" -Release -Holder $Holder
