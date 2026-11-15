<!-- SPDX-License-Identifier: MIT -->
# ME2 as a continuation of ME1: the plan

Date: 2026-11-14, against main `544067a3`. The why and the evidence are in [ME2 from ME1](le2-from-le1.md) (sections 0 and
4) and [research/le2-renderer-integration.md](../research/le2-renderer-integration.md); this is the work list. Each step
names its files, its check and what it waits for, so a session can pick one up without this conversation.

## The rule

> "For Mass Effect 1, this should really just feel like a continuation of the prior mod, not a radically new
> experience... Everything should really try to use ME1 as default and only deviate where necessary." (the owner,
> 2026-11-14)

- **Port, don't redesign.** Every ME2 feature is ME1's module, settings, defaults and resources (the same menus, panels,
  fonts, sounds, haptics). A step that changes behaviour names the ME2 fact that forces it in its notes.
- **One code path for both games.** No ME2 copy of a host module: where a module reaches into Halcyon's renderer it goes
  through one interface (M2), and where it reads the game it goes through `src/ue3` and `engine/GameAdapter.hpp` (M1).
- **Settings already decided:** no head aim (VRIK fast-tracked), no stick pitch, conversations and cutscenes on a screen
  (the flat cine panel until M5), first person, flat menus, no profile hotkeys, no recenter on R. Pinned in
  `ApplyMevrBaseline` (`src/halcyon/le2/me2_menu.cpp`).

## Where it stands (2026-11-14)

| Done | Where |
|---|---|
| Halcyon's ME2 renderer in our LE2 host: XR session, stereo, our controller actions on its session, Touch as pad 0, its render-thread ProcessEvents queued for the game thread | `src/halcyon/le2`, `plugin/HalcyonLe2*`, research "Imported and linked" |
| ME1's renderer choices on ME2's keys, hotkeys only with the game in front | `ApplyMevrBaseline`, `MevrBridge::HotkeyDown` |
| Upstream's `MELE2VR.ini` from the deploy (first person from the load, 2048x2304 eyes with the borderless window) | `tools/deploy-asi.ps1`, `tools/vr-game-settings.ps1 -Game LE2` |
| The first-gameplay-frame fault (the mod's `scale set` during a load with depth of field on) fixed | `game/SystemSettings`, gotchas/me2-scale-set-during-load |
| Checked on the fake headset only; never in a headset | research "The first run on the fake headset", "Defaults from ME1" |

**The gap, measured:** the mod can use 60 host services (`HostServices` v39, `src/mod/ModApi.hpp`); the LE2 host gives 3
(controllers, the ProcessEvent callback, crash breadcrumbs). And the mod's game code has no reflection layout for LE2. So
almost every ME1 feature stands down on ME2 today.

## Milestones

Sizes: **S** a day or less of one session, **M** a few sessions, **L** a week or more. "Check" is what must be seen on the
fake headset (or offline) before the milestone lands; headset checks are collected in M7.

### M1. The mod's engine access on ME2 (M)

Waits for nothing. Runs beside M2 (different files: `src/engine`, `src/ue3`).

1. Open ME2's decrypted `.text` in Ghidra (`%LOCALAPPDATA%\MEVR\dump\MassEffect-60AEEFA5.text.bin`, `tools/ghidra`,
   `local-tools/splice_text.py`). Never commit it.
2. `ReflectionLayout()` for `MEVR_GAME_LE2` in `engine/GameAdapter.hpp`, each offset read from the native function that
   uses it (research/34 section 2 is the recipe; candidates in research/le2-engine-access-discovery.md).
3. `ue3::memory` for LE2 (GMalloc's alloc and free by pattern), so `OwnedString`, `FreeString` and the console work.
4. LE2 rows in `engine/Addresses.hpp` (patterns from `tools/rva-patterns.py`) and in `engine/ReflectCheck`.
5. Move `engine/Legacy`'s LE2 reads and the diagnostic observers onto `ue3` handles, one ReflectCheck row each.

**Check:** an LE2 development build logs `reflect: N of N checks agree`; `PlayerController.ConsoleCommand` through
`ue3::runtime::Call` returns its text; ctest `mevr-rva-table` holds for LE2.

### M2. The host's services on ME2 (L)

Waits for nothing. Runs beside M1 (files: `src/plugin`, `src/halcyon/le2`).

1. **The test loop first (S):** a `[MEVR]` `WrapSessionFunctions` seam in `me2_xr.cpp`'s function resolution (as LE1's
   `ResolveSessionFunctions`), so `plugin/TestLoop`'s snapshots, recordings and replays work in ME2. Every later check
   uses them instead of desktop captures.
2. **One renderer interface (M):** a header in `src/plugin` declaring what host modules need from the renderer (session
   and spaces, the view of each frame, our composition layers, recenter, the menu key and `KeyDown`, the game window, the
   stereo and cinematic state, swapchains). LE1's implementation forwards to `MELEVR::MevrBridge` with no behaviour
   change; the about 25 modules that call `MELEVR::` today (`HalcyonBridge`, `HalcyonOverlays`, `MainMenu`,
   `MainMenuSettings`, `Calibration`, `RenderLevers`, `GamePointer`, `WorldUi`, `CineScreen`, `TestLoop` ...) move onto
   it. **Check:** LE1 eye snapshots at a recorded pose are identical before and after.
3. **ME2's implementation (M):** `[MEVR]` seams in `me2_xr.cpp` and `calcview_hook.cpp`: the view each frame (head pose,
   eye offsets, units per metre: LE1's `OnViewBuilt`), our layers appended at `xrEndFrame` (LE1's overlay layers), the
   STAGE floor, recenter (`OnRecentered`), the session's stop (`OnSessionStopped`).
4. **The HUD panel (M):** the game's UI drawn onto our body-locked panel quad instead of into the eyes, LE1's way
   (`HudPanel*` seams, `plugin/HalcyonOverlays`). ME2's renderer duplicates the UI into each eye today (`UiDupEnabled`,
   `PcHud`): the panel replaces that.
5. **`gServices` for LE2 from the same list as LE1's** (`plugin/ModLoader.cpp`): one builder, the renderer interface
   behind it, version 39.

**Check:** the LE2 mod's start says none of `older than services v..`, `no view to aim with`, `host state sizes differ`;
a snapshot in ME2 shows both eyes and the HUD panel.

### M3. VRIK on ME2, fast-tracked (L)

Waits for M1 and M2.

1. **Hand aim (M):** `game/HandAim` on LE2 (`GetAdjustedAimFor`), the aim ray to the host, the hand reticle. `HeadAim`
   stays off. **Check:** a replayed right controller turned 30 degrees: shots land along it (`hand aim: shot N`).
2. **Arms and body (M):** `engine/BonePose` (`UpdateSkelPose`'s LE2 RVA from Ghidra; ME2's bone names [verify]; never
   write `Camera` or `God`), `game/ArmIk`, `BodyFollow`, `BodyScale` and the floor lock, fingers. **Check:** a snapshot with
   replayed hands shows Shepard's hands on the controllers; `mevr camera trace` and `tools/skeleton-audit.py` clean.
3. **The gun in the hand (M):** the weapon's frame, sights (red dot, scope picture), gun kick, the support grip.
   **Check:** a scoped shot hits the sight line; `sights:` lines name ME2's sight bones.
4. **Holsters (S):** ME2's weapon switching, "only holsters draw". **Check:** a replayed draw at the hip slot draws the
   pistol.
5. **Reload (M, ME2's own):** ME2 reloads thermal clips; a physical reload gesture through the ME2 validation session's
   `PhysicalReloadRequest` coordinator, with `core/ik/BodySlots` (a clip pouch) and `core/ik/Vent`'s gesture-in-the-gun's
   -frame pattern. **Check:** the Freedom's Progress pistol baseline (12/0/60 -> 11/1/60 -> 12/0/59) reproduced by a
   replayed gesture instead of `R`.

### M4. The menus, as ME1 has them (M)

Waits for M2; the game-side parts for M1.

1. **Our main menu (M):** RmlUi in the LE2 host (`MEVR_MAIN_MENU`, today LE1-only), the same document and styles
   (`assets/menu`), settings rows mapped to ME2's keys (`MainMenuSettings`; Halcyon's rows through the M2 interface).
   Both sticks held or `\` open it, as in ME1.
2. **Calibration (S):** `plugin/Calibration` and `game/BodyCalibration` on LE2.
3. **The omni-tool (M):** the left-wrist tap and the rail (`plugin/OmniToolRail`), ME2's own omni-tool on Shepard's arm
   (`game/OmniTool` with ME2's effect names: the deviation), its entries opening ME2's screens (`game/GameMenuRequest`
   with ME2's functions [verify]), the screens on the wrist (`game/WorldUi` over ME2's GUI panels, `GuiKey`).

**Check:** on the fake headset the menu opens, a setting saves to `LE2MEVR.ini`, a replayed wrist tap opens the rail and
its Squad entry opens ME2's squad screen.

### M5. Conversations and cutscenes on ME1's 3D cinema screen (M)

Waits for M2. Can run beside M3.

1. The cinema screen's seams in `me2_xr.cpp` (research/52: the decision, parked head look, projection, plan, submit, pace,
   the conversation movie at its authored size), `plugin/CineScreen`, the floor and horizon (`CinemaBackdrop`), the dip
   from black (`TransitionFade`). Replaces the flat cine panel; `ApplyMevrBaseline` keeps VR conversations off.
2. ME2's interrupts (ME2's own): `SFXGameModeConversation.InterruptParagon/Renegade`; how a VR player triggers one is the
   owner's question below.

**Check:** a snapshot during Freedom's Progress's first conversation shows the screen in stereo with the floor around it.

### M6. The rest of ME1, one module at a time (L)

Each waits for M1 and M2 and follows its LE1 research note: comfort and the vignette, the HUD pieces (wrist status, gun
panel: ME2's HUD crops), interaction labels, VR cover (ME2's damage rule, research/23 6.3), physical crouch and sprint
(ME2's storm), the power wheel and self-cast gestures (ME2's powers), squad orders, placement and voice, Thespian mode
(ME2's wheels and interrupts), conversations in the world, the galaxy map and scanning, the Hammerhead (the Mako's
controls where they fit). Order by what the owner plays first.

### M7. Ship it (M)

1. **The players' build without LExSDKv2 for LE2** (needs M1's step 5): the CMake guard and
   `tools/package-release.ps1`'s refusal, as for LE1.
2. **MEVR Setup for ME2:** the loader into `Game\ME2\Binaries\Win64`, the borderless window and VR settings,
   `MELE2VR.ini`, uninstall byte for byte (`src/install`, `tools/setup`).
3. **The headset pass:** every `notes/headset-checklist.md` ME2 line, in the owner's headset.

## Who and when

| Lane | Takes | Notes |
|---|---|---|
| A | M1 | Ghidra on ME2; touches `src/engine`, `src/ue3` |
| B | M2, then M5 | the renderer seams; touches `src/plugin`, `src/halcyon/le2` |
| C | M3 once A and B land | VRIK |
| ME2 validation session | reload fixtures and the coordinator (M3.5), saves, scenarios (`tools/qa-le2`) | its protected runner becomes the ME2 test runner once its responder and Halcyon share one LE2 host (le2-from-le1.md 3.3) |

A and B run in parallel; C starts when both have landed. One game run at a time on this machine (all lanes and the
validation session); back up saves and settings once at the start of a stretch of runs and restore once at the end.
Every milestone lands on main only after `build-trilogy`, `build`, `build-mono` and ctest pass.

## Questions for the owner

1. **Interrupts in VR** (ME2 only, M5.2): the pad's triggers as ME2 has them, or a gesture?
2. **Lanes:** run A and B as parallel sessions now, or one after the other in this one?
3. **The Hammerhead:** ME1's Mako controls and cockpit for it (M6), or ME2's own driving first?
4. **When to ship ME2:** after M3 and M4 (aiming and the menus), or wait for M5 and M6?
