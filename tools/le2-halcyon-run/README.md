<!-- SPDX-License-Identifier: MIT -->
# ME2 runs with Halcyon's renderer, on the fake headset

Until `tools/qa-le2`'s responder and Halcyon's renderer share one LE2 host (docs/le2-from-le1.md section 3.3), these
scripts run ME2 as the players' host does: our `halcyon-le2` host from `build-trilogy`, loaded by LEBinkProxy, on
SteamVR's null driver. They are what the ME2 lanes of [the continuation plan](../../docs/le2-continuation-plan.md) use.

One game run at a time on this machine, across every session: **`run-lock.ps1`** names who runs the game. `prep.ps1`
takes it and `restore.ps1` gives it back; take it by hand around any other run (an LE1 run with
`tools/fake-headset-session.ps1`). A game running while nobody holds the lock is someone else's (the owner, the ME2
validation session's `tools/qa-le2`): leave it alone and wait.

```powershell
tools\le2-halcyon-run\run-lock.ps1 -Status
tools\le2-halcyon-run\prep.ps1 -Holder <session name> [-Tag what] [-Diagnostics]   # once per stretch: backups, loader, deploy, fake headset
tools\le2-halcyon-run\launch.ps1 [-Kill]                                           # each run: Steam's -game 2, waits for the XR session
# ... the run: logs in ME2\Binaries\Win64\Logs\LE2MEVR.log and %LOCALAPPDATA%\MELE2VR\MELE2VR_Log.txt
tools\le2-halcyon-run\launch.ps1 -Kill                                             # the next run (rebuild, then prep.ps1 again to redeploy)
tools\le2-halcyon-run\restore.ps1 -Holder <session name>                          # once at the end of the task
```

- **Backups once per task, not per run** (the owner, 2026-11-14): the first `prep.ps1` backs up the profile's saves and
  settings, ME2's `BioGame\Config` and `Binaries\Win64` with hash manifests into `%LOCALAPPDATA%\MEVR\sessions\<stamp>-<tag>`;
  later preps by the same holder only redeploy. `restore.ps1` copies changed files back, moves files the runs added aside
  into the session folder (never deletes them), checks every file hash for hash, and only then releases the lock.
- The Freedom's Progress save is the VR Shepard career's Save 4; the game loads it from its own Load menu (computer use:
  bring `MassEffect2` to the front and click into it first, or the keys land in another window).
- `-Diagnostics` turns `MELE2VR.ini`'s `[VR] Diagnostics` on for the stretch (Halcyon's `[ME2FP]`, `[ME2DISC]` lines; F3
  held 0.3 s dumps the camera graph).
- Never run anything that takes the game slot (`tools/game-slot.ps1`) while a stretch is open, and never `return` a
  slot without asking the owner.
