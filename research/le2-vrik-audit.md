<!-- SPDX-License-Identifier: MIT -->
# ME1's VRIK on ME2: what carries over, by name

Date: 2026-11-14, main `4d2b592b`. Asked by the owner: "Did you already audit me1 for how it does vrik? Is it applicable
here?" It feeds M3 of [the continuation plan](../docs/le2-continuation-plan.md) (lane C).

**How:** `python tools/le2-name-audit.py` reads every engine path our modules name (`"Pkg.Class"`, `"Pkg.Class.Function"`,
`FieldRef{"Pkg.Class", "Field"}`) and looks each up in LExSDKv2's generated LE1 and LE2 headers, used only as name lists.
The default module list is ME1's VRIK and combat code: `game/HandAim`, `ArmIk`, `BodyFollow`, `BodyScale`,
`CombatStance`, `Fingers`, `GunKick`, `Holsters`, `Player`, `Sights`, `SupportGrip`, `ViewLevel`, `Weapon`,
`ScopeParts`, `BodyCalibration`, `Throwables`, `Vent`, `Posture`, `engine/BonePose`, `GameAdapter`, `call_function`,
`BodyBounds`. Any other module: `python tools/le2-name-audit.py game/<Module> ...`.

**Result: 197 engine names that ME1 has; ME2 has 146 of them, lacks 51.**

## What carries over as it is

- **The IK and its math:** `core/ik` (arm two-bone IK, body follow, fingers, the gun kick's spring, sights' math, body
  slots) is game-independent and unit-tested. It applies to ME2 unchanged.
- **The game glue that is engine-generic:** every name `game/ArmIk` (11), `game/Player` (28), `game/ViewLevel` (29),
  `game/BodyFollow`, `engine/BonePose` and `engine/BodyBounds` use exists in ME2. So does all of Posture's but one, and
  hand aim's controller-side query (`Engine.Controller.GetAdjustedAimFor`, `Engine.Pawn.GetWeaponStartTraceLocation`).
- **The engine layout itself** matches ME1's except UFunction's fields and ProcessEvent's vtable slot (lane A, M1,
  research/le2-engine-access-discovery.md "M1: what ME2's code showed").

## What ME2 names differently (the weapon family)

ME1's weapon classes `BioWeapon`, `BioWeaponRanged` and `BioEquipment` don't exist in ME2. ME2's are `SFXWeapon` (on
`Engine.Weapon`) and `SFXInventoryManager`. Each missing name, its likely ME2 counterpart (by name and signature in the
SDK; **[verify]** each in Ghidra and the game before relying on it):

| ME1 name | Used by | ME2 counterpart |
|---|---|---|
| `BioWeaponRanged.GetAdjustedAim` | HandAim | `SFXWeapon.GetAdjustedAim` |
| `BioWeaponRanged.GetMuzzleLoc` | Weapon | `SFXWeapon.GetMuzzleLoc`, `GetPhysicalFireStartLoc` |
| `BioWeaponRanged:m_MuzzleSocketName` | Weapon | `SFXWeapon:MuzzleSocketName` (0x740) |
| `BioWeapon:SkelMeshComp` | Weapon | `Engine.Weapon`'s mesh component [verify the field] |
| `BioWeaponRanged.GetRangedWeaponType` | Holsters, Weapon | the weapon's class (ME2 types are classes: pistols, SMGs, shotguns, rifles, heavy) |
| `BioWeaponRanged.TickFire` | Weapon | `SFXWeapon.CalcWeaponFire`, `InstantFire`, `ProjectileFire` |
| `BioWeaponRanged.GetPlayerAimError` | Sights, Weapon | `SFXWeapon.GetPlayerAimError` |
| `BioWeaponRanged.GetRecoil` | GunKick | `SFXWeapon.GetWeaponRecoil`, `SetWeaponRecoil` (also `SFXInventoryManager.SetWeaponRecoil`); fields `Recoil`, `ZoomRecoil` |
| `BioWeaponRanged:Stats`, `SFXWeaponStatsBase:AimModes` | Sights (scope zoom steps) | `SFXWeapon:ZoomFOV`, `GetZoomFOV`, `SetZoomed`, `ZoomSnapList` |
| `SFXGameModeDefault.SwitchToWeapon` | Holsters (the draw) | `BioPawn.SetWeaponFromSlot`, `SFXInventoryManager.SetWeaponFromSlot` / `SetWeaponIfAvailable` / `GetWeaponInSlot`, `SFXGameModeDefault.TrySwapWeapon` |
| `SFXGameModeDefault.TryHolster` | Holsters | the same name exists in ME2 |
| `SFXPawn_Player.DrawPrimaryWeapon` | Holsters ("only holsters draw") | none of that name: ME2's draw routes [verify] (`BioPawnBehavior.TEMP_DrawWeapon`, `SetWeaponFromSlot`, `SFXGameModeBase.FireWeapon`'s draw) |
| `BioPawn.IsInCombatActionState` | CombatStance, Holsters, Throwables | `BioPawn.IsCombatActionState` |
| `BioPawn.SetDesiredSpeed` | CombatStance | `Engine.Pawn.SetDesiredSpeed` |
| `BioPawnBehavior:m_fMaxDesSpd{,Tac}{Accel,Decel}Rate` | CombatStance (linear walking) | not in ME2's `BioPawnBehavior`: ME2's speed ramps live elsewhere [verify] (`BioPawn.InitializeSpeeds`) |
| `BioPawn.ShouldCrouch` | Posture | `Engine.Pawn.ShouldCrouch`, `BioPawn.SetShouldCrouch` |
| `BioPawn.IsDead` | Throwables | `Engine.Pawn.IsDead` |

## What ME2 doesn't have (the deviations the game forces)

- **Grenades:** ME2 has none. `game/Throwables`' grenade half (`BioGrenade*`, `UseGrenade`, `GetGrenades`) doesn't apply.
  Its melee half maps to `SFXGameModeDefault.TryMelee` / `TryHeavyMelee`.
- **Weapon heat:** ME2 reloads thermal clips. `game/Vent` (`m_fCurrentHeat`, `DoCoolDownEffect`, `BioWeaponVFXAppearance`)
  doesn't apply; M3.5's reload gesture takes its place (`SFXWeapon.TryReload`, `CanReload`, `DoReload`; the ME2 validation
  session's `PhysicalReloadRequest`).
- **ME1's medi-gel and mapped-power callers** that holsters let draw (`UseFirstAid`, `TryUseMappedPower`): ME2's
  equivalents are to be found when holsters are ported.

## What the name check can't see

- **Bone names and the camera's bones.** ME1's camera hangs off `Camera` and `God` through SpaceBases, so arm IK never writes
  them. ME2's player skeleton and how Halcyon's ME2 first person places the eye (`[ME2FP]`, `fp_*` in `src/halcyon/le2`)
  must be read before `engine/BonePose` writes anything on ME2.
- **Native RVAs** other than those lane A found: UpdateSkelPose's candidate is `0x79B8A0` (`execForceSkelUpdate` calls it
  as LE1's does), UpdateBounds `0x798350` (both unverified in the game).
- **Behaviour:** whether ME2's game also puts each shot's recoil into the controller's rotation, as ME1's game does
  (research/24), aim assist's names and effect, and the weapon's own first-person camera (`SFXWeapon.UseFirstPersonCamera`,
  `Engine.Weapon.WeaponCalcCamera`).

## No camera recoil on ME2 either

The mod removes the camera's recoil in ME1 (the owner, 2026-11-14: camera recoil "wasn't popular"): `game/ViewLevel` holds
the view's pitch and roll at 0 each tick on foot while the hand aims (the recoil becomes a hand pulse), `game/GunKick`
kicks the gun model in the hand instead, and `game/CameraShake` turns the guns' screen shakes off (`[Comfort]
screenShake = 0`). On ME2 the same must hold from the first shot hand aim fires (M3):

- `game/ViewLevel`: all 29 engine names it uses exist in ME2 (`python tools/le2-name-audit.py game/ViewLevel`): it ports
  as it is.
- `game/CameraShake`: ME1's shake sources (`SFXWeaponStatsBase:FiringShake`/`TightAimFiringShake`, `SFXCameraShakeBase`,
  the Mako's `BoostShake`/`JumpShake`) don't exist in ME2, which plays the engine's own shakes (`Engine.Camera.PlayCameraShake`,
  `CameraModifier_CameraShake.AddCameraShake`, `PlayerController.ClientPlayCameraShake`, `AnimNotify_ViewShake`). ME2's
  firing shakes are to be found there [verify] and scaled to `[Comfort] screenShake` at the source, as ME1's are.
- `game/GunKick`'s recoil source: `SFXWeapon.GetWeaponRecoil` / `SetWeaponRecoil` (the table above).

## Answer

Yes, it applies. The IK, the body and the engine-generic glue port as they are; the weapon modules port by swapping ME1's
`BioWeapon*` names for ME2's `SFXWeapon`/`SFXInventoryManager` ones (table above); grenades and heat venting don't exist
in ME2, and reload replaces the vent. The open risks are the skeleton (which bones the eye hangs off in ME2) and the
behaviours a name list can't show. Other modules: run the tool on them before porting (it already shows the menu's pause:
ME2 has no `Engine.WorldInfo.PauseGame`, lane D's concern).
