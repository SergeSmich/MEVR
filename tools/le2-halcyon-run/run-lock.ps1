# SPDX-License-Identifier: MIT
# One game run at a time on this machine, across parallel sessions: a lock file that names who runs the game.
# prep.ps1 takes it for a stretch of ME2 runs and restore.ps1 gives it back; take it by hand around any other game run
# (an LE1 run with tools/fake-headset-session.ps1, a measurement), so two sessions never start the game, deploy into
# its folder or restore its saves under each other.
#   run-lock.ps1 -Status
#   run-lock.ps1 -Take -Holder <session name> [-Note <what for>]    (fails while someone else holds it)
#   run-lock.ps1 -Release -Holder <session name>
param(
    [switch]$Take,
    [switch]$Release,
    [switch]$Status,
    [string]$Holder,
    [string]$Note = ''
)
$ErrorActionPreference = 'Stop'
$lock = Join-Path $env:LOCALAPPDATA 'MEVR\game-run.json'

function Read-Lock {
    if (-not (Test-Path -LiteralPath $lock)) { return $null }
    return Get-Content -LiteralPath $lock -Raw | ConvertFrom-Json
}

$held = Read-Lock
if ($Status -or -not ($Take -or $Release)) {
    if ($held) { "held by '$($held.holder)' since $($held.since): $($held.note)" } else { 'free' }
    $running = Get-Process MassEffect1, MassEffect2, MassEffect3, MassEffectLauncher -ErrorAction SilentlyContinue
    if ($running) { 'running now: ' + (($running | ForEach-Object { "$($_.ProcessName) ($($_.Id))" }) -join ', ') }
    exit 0
}
if (-not $Holder) { throw '-Holder is required' }

if ($Take) {
    if ($held -and $held.holder -ne $Holder) {
        throw "the game is held by '$($held.holder)' since $($held.since) ($($held.note)): wait for its release"
    }
    if (-not $held) {
        $slot = Join-Path $env:LOCALAPPDATA 'MEVR\game-slot\lock.json'
        if (Test-Path -LiteralPath $slot) { throw 'the game slot is held (tools/game-slot.ps1 status): not ours to take' }
        if (Get-Process MassEffect1, MassEffect2, MassEffect3, MassEffectLauncher -ErrorAction SilentlyContinue) {
            throw 'a game or its launcher runs, and nobody holds the lock: the owner may be playing'
        }
    }
    New-Item -ItemType Directory -Force (Split-Path $lock) | Out-Null
    [pscustomobject]@{ holder = $Holder; since = (Get-Date -Format 's'); note = $Note } |
        ConvertTo-Json | Set-Content -LiteralPath $lock -Encoding utf8
    "taken by '$Holder'"
    exit 0
}

if ($Release) {
    if (-not $held) { 'already free'; exit 0 }
    if ($held.holder -ne $Holder) { throw "held by '$($held.holder)', not '$Holder': not released" }
    Remove-Item -LiteralPath $lock -Confirm:$false
    "released by '$Holder'"
}
