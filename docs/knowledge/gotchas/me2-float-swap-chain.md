---
type: Gotcha
title: On an HDR display ME2's swap chain is R16G16B16A16_FLOAT in gameplay, and Halcyon's ME2 renderer copies it into 8-bit eyes with a CopyResource D3D11 drops
description: With Windows HDR on, ME2 renders gameplay into a float swap chain whatever DesiredRange says; Halcyon's ME2 renderer assumes an 8-bit one, so its eye copies do nothing (stale or black eyes), its SFR left eye is never captured and its UI treatment never runs. EyeCopy draws the float frame into the eyes; [Colour] hdrDisplay = -1 keeps the game at 8 bits.
tags: [le2, halcyon, renderer, hdr, d3d11, test-loop]
status: stable
timestamp: 2026-11-14T21:00:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-14T21:00:00Z }
bitten: [2026-11-14]
---

# Symptom

| wording | where |
|---|---|
| "in gameplay the eye images are the main menu's backdrop" (it was ME2's loading screen, the mass relay wireframe) | first lane B agent's snapshots after the main menu's Resume, 2026-11-14 |
| "the eye snapshot is black in both eyes" after `tools/le2-halcyon-run/load.ps1` | lane A's and lane B's runs, 2026-11-14 |
| the desktop mirror shows gameplay; `[SFR] Draw replay #3000 ok=1`; the snapshot JSON lists no layer errors | `Logs\LE2MEVR.log`, `MELE2VR_Log.txt`, `snapshots\<stamp>.json` |
| `[SFRDIAG] ... totalCaps=0`, `[UIGATE] ... ui=0 ... fmt=10` in gameplay | `MELE2VR_Log.txt` with `Diagnostics=1` |

# Cause

D3D11's `CopyResource` copies only within one typeless format family and drops anything else without an error (only the
debug layer would say so). Halcyon's ME2 renderer fills its eye images and its flat panel with `CopyResource` from the swap
chain's buffer 0, and assumes that buffer is R8G8B8A8_UNORM. On a display with Windows HDR on, ME2 makes it
R16G16B16A16_FLOAT (format 10) in gameplay even with `[HDR] DesiredRange = DynamicRange_SDR` (the title is still format 28;
LE1 keeps format 28 on the same machine). The eye images (format 29 swapchains, backed as 27) then keep whatever reached
them while the frame was 8-bit. The same assumption breaks the SFR left-eye capture (no backbuffer draw between the two
passes in the float frame) and the UI classifier (`IsUiDrawNow` wants format 28).

# Check

`tools/snapshot.ps1` in ME2 gameplay: `<stamp>-source-left.png` shows the game, `-copied-left.png` and `-left.png` don't.
The host log says `halcyon le2 bridge: eye copy from 4096x2304 format 10 ... into ... format 27` and `the game's swap chain
is 4096x2304 format 10`. Windows' HDR state: DisplayConfig's advanced colour info (the research note has the query).

# Fix in place

- `plugin/EyeCopy` (`MevrBridge::CopyIntoImage` in `CopyTextureToEyeFullFrame` and the flat panel's copy): a source of
  another format family is drawn through an sRGB view of the image instead of copied. The eyes show the game; both eyes
  are still one picture in a float frame.
- `[Colour] hdrDisplay = -1` in `ASI\LE2MEVR.ini`: the game's `IDXGIOutput6::GetDesc1` is answered SDR, ME2 stays 8-bit,
  and Halcyon's stereo pair and UI treatment work. The default is 0 (the display's answer) until the owner decides.
- Never change Windows' HDR setting to get around it.

# Citations

- [research/le2-renderer-integration.md](../../../research/le2-renderer-integration.md), "The black eyes": the runs, the
  formats, the fixes.
- `src/plugin/EyeCopy.cpp`, `src/plugin/HalcyonLe2Bridge.cpp` (`OnFactory`, `CopyIntoImage`, `OnEyeCopied`),
  `src/halcyon/le2/me2_xr.cpp` (`CopyTextureToEyeFullFrame`), `src/core/d3d11/Blit.cpp`.
- LE1's float frame: [research/27-colour.md](../../../research/27-colour.md) section 7c.
