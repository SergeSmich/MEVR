<!-- SPDX-License-Identifier: MIT -->
# Roadmap

Where MEVR is heading, in plain terms. It has no dates: this is a one-person fan project, and priorities move with what
players report. It is updated with each release; what each release actually changed is in the [changelog](CHANGELOG.md).

Want something moved up, or found a problem? Say so in the
[MEVR thread on Discord](https://discord.com/channels/747967102895390741/1568594747591884841) ([join the server](https://discord.gg/flat2vr) first)
or open a [bug report](https://github.com/dnehring/MEVR/issues/new?template=bug.yml).

## Now: making Mass Effect 1 solid

- **Character shadows back in both eyes.** They are off in VR since 0.3.1, because the faster stereo drew them in the
  wrong place for the right eye. The fix is to correct them for that eye, then turn them back on.
- **Standing still without swaying.** While you stand still, the game's idle animation makes Shepard's feet slide under
  you. A fix that keeps the feet planted is built and waits for a headset check before it becomes the default.
- **First-person conversations** (experimental, from the menu's Comfort page): more scenes that feel natural, with
  people walking into a circle around you and scripted walks that wait for you.
- **Performance in the heaviest scenes** (Eden Prime's fires, the Presidium), so they hold the headset's frame rate.
- **Code-signed downloads**, so Windows SmartScreen and antivirus stop warning about MEVR Setup (applied for, through
  SignPath Foundation).

## Next: Mass Effect 2

The ME2 port is under way, starting with proving combat in VR. The plan keeps everything you know from ME1 (your hands,
holsters, two-handed guns, powers, squad orders, the menus) and adds what ME2 needs:

- **Thermal clips reloaded by hand**, while the game still counts the ammo.
- **Cover by crouching behind things**, with the game's own protection, and the vaults and climbs the levels need.
- **Planet scanning and hacking** on a panel you can work in VR first; holograms and a Hammerhead cockpit after that.

Mass Effect 3 comes after ME2 and builds on the same work.

## Later: ideas we want to build

- **Walking around your room**: Shepard following your real steps beyond the 0.75 m you can lean today.
- **The game's own scope reticles** instead of the plain duplex one.

## Needs testing in a headset

These are in the current release, but have been played in a headset only a few times or not at all. A report on any of
them helps, whether it works or not:

- Character shadows are off in both eyes, and nothing else changed with them.
- First-person conversations: your body stays with you, the Council stands in the chamber as you climb the stairs, and
  scripted walks wait when you fall behind.
- Thespian mode: a real microphone in a noisy room, and whether the script panel is easy to read.
- 3D audio through headphones, and the voices of people near you placed where they stand.
- Live controller adjustment and the manual calibration offsets.
- The game's puzzle instructions (the Tower of Hanoi, the Quasar machines, the toxin cure) where you can see them.

## Not planned

- **Aiming with your head.** You aim with your hand; the head only looks.
- **Asking you to change your VR runtime's settings** (ASW, the Oculus Debug Tool, SteamVR or Link options) to get a
  good frame rate. Fixes for that belong in the mod.
- **Anything that needs a controller's menu or system button.** Many headsets (a Quest over Link) don't give one to
  games; everything is reachable from our own menu.
- **Shipping or downloading the game's files or texture packs for you.** Optional packs come from their own pages.
- **Talking to characters in your own words.** Thespian mode takes the game's written replies, read aloud or by their
  label. Free speech would need the characters to answer things they were never written or recorded to say, with
  generated dialogue and generated voices.
- **The original (2007-2012) editions.** MEVR is built for the Legendary Edition.
