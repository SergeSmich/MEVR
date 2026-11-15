# SPDX-License-Identifier: MIT
# Loads an ME2 save by script, no menus: the mod's `mevr load` (game/ConsoleRequest) asks the engine to load the save's
# descriptor through SFXEngine.QueueSaveGameCommand(SaveGame_Load), the call ME2's own Load screen and its QuickLoad make
# (research/le2-engine-access-discovery.md "A save loaded by script"). Reads the save, never writes one; the file's hash
# is checked before and after. Then waits for gameplay (`mevr state`: a world that isn't the title's, a pawn, game mode 0).
#
#   tools\le2-halcyon-run\load.ps1                                   # the VR Shepard career's Save 4 (Freedom's Progress)
#   tools\le2-halcyon-run\load.ps1 -Save VR_31_Soldier_141126_336072f\Save_0004.pcsav
#   tools\le2-halcyon-run\load.ps1 -Save <full path to a .pcsav>
#
# Needs an open stretch (prep.ps1, launch.ps1) and a development build of the mod (the console request). From the title
# screen or in game; the game's own autosave after a load is put back by restore.ps1 like every other change of the run.
param(
    [string]$Save,
    [int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
$console = Join-Path $PSScriptRoot '..\console.ps1'
$current = Join-Path $env:LOCALAPPDATA 'MEVR\sessions\le2-current.json'
if (-not (Test-Path -LiteralPath $current)) { throw 'no ME2 stretch is open: run prep.ps1 and launch.ps1 first' }
if (-not (Get-Process MassEffect2 -ErrorAction SilentlyContinue)) { throw 'ME2 is not running: launch.ps1 first' }

# The save: a full path, a path under ME2's save folder, or the VR Shepard career's Save 4.
$saves = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BioWare\Mass Effect Legendary Edition\Save\ME2'
if (-not $Save) {
    $found = @(Get-ChildItem $saves -Directory -Filter 'VR_*' | Where-Object { Test-Path (Join-Path $_.FullName 'Save_0004.pcsav') })
    if ($found.Count -ne 1) { throw "the VR Shepard career's Save 4: $($found.Count) careers VR_* with a Save_0004.pcsav in $saves (name one with -Save)" }
    $Save = Join-Path $found[0].FullName 'Save_0004.pcsav'
} elseif (-not [IO.Path]::IsPathRooted($Save)) {
    $Save = Join-Path $saves $Save
}
if (-not (Test-Path -LiteralPath $Save)) { throw "no save at $Save" }
$file = Get-Item -LiteralPath $Save
$career = $file.Directory.Name
$name = [IO.Path]::GetFileNameWithoutExtension($file.Name)
# The descriptor as ME2's Load screen lists it (research/le2-validation-evidence.md: Save_0099 index 99 manual, AutoSave and
# ChapterSave index -1): Save_NNNN manual (0) with its number, QuickSave 1, AutoSave 2, ChapterSave 3, each -1.
switch -Regex ($name) {
    '^Save_(\d+)$' { $index = [int]$Matches[1]; $type = 0 }
    '^QuickSave$' { $index = -1; $type = 1 }
    '^AutoSave$' { $index = -1; $type = 2 }
    '^ChapterSave$' { $index = -1; $type = 3 }
    default { throw "$($file.Name): not a save name ME2 lists (Save_NNNN, QuickSave, AutoSave, ChapterSave)" }
}
$hash = (Get-FileHash -LiteralPath $Save).Hash

function Ask([string]$command, [int]$timeout = 10) {
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $console $command -TimeoutSeconds $timeout 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    return ($out | Where-Object { $_ -and $_ -notmatch '^> ' }) -join "`n"
}

# The mod's console must answer first (the title screen has a player controller once the menus are up).
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$state = $null
while ((Get-Date) -lt $deadline -and -not $state) {
    $state = Ask 'mevr state'
    if (-not $state -or $state -notmatch '^mevr state:') { $state = $null; Start-Sleep 2 }
}
if (-not $state) { throw "the mod's console didn't answer within $TimeoutSeconds s (a development build? the title screen up?)" }
"before: $state"
$before = $state

$queued = Ask "mevr load $career $name $index $type"
"load: $queued"
if ($queued -notmatch 'queued') { throw "the load wasn't queued: $queued" }

# Gameplay: another world than before, a pawn, on foot.
$done = $false
while ((Get-Date) -lt $deadline -and -not $done) {
    Start-Sleep 2
    $state = Ask 'mevr state'
    if ($state -and $state -match 'world (\S+ \S+);' -and $before -match 'world (\S+ \S+);') {
        $world = ($state | Select-String 'world ([^;]+);').Matches[0].Groups[1].Value
        $was = ($before | Select-String 'world ([^;]+);').Matches[0].Groups[1].Value
        $done = $world -ne $was -and $state -notmatch 'pawn None' -and $state -match 'game mode 0$'
    }
}
"after: $state"
if ((Get-FileHash -LiteralPath $Save).Hash -ne $hash) { throw "the save file changed: $Save" }
"the save file is unchanged ($($file.Name), SHA-256 $($hash.Substring(0, 12))...)"
if (-not $done) { throw "no gameplay within $TimeoutSeconds s" }
"gameplay: $career\$($file.Name) loaded"
