# FNVVR Reborn

A VR mod for *Fallout: New Vegas*, built into a fork of [DXVK](https://github.com/doitsujin/dxvk). The game is played with motion controllers: a weapon in your hand, a Pip-Boy on your forearm, objects you pick up, throw and reload by hand.

The original FNVVR was Windows-only: it shared frames between the game's DirectX 9Ex surface and a DirectX 11 VR pipeline. That interop does not exist on Linux (Wine/Proton). This project takes a different route: the VR layer lives inside the DXVK fork that translates the game's Direct3D 9 calls to Vulkan, and the eye images are shared with the VR runtime through Vulkan.

The output is a single 32-bit `d3d9.dll`, which the game loads in place of the stock one. It runs under Proton on Linux and under Wine or Windows.

Releases are pre-releases: see the [releases page](https://github.com/df4l/FNVVR/releases) for the latest one, and the [changelog](CHANGELOG.md) for everything that changed.

## What it does

**Seeing the game**
- Stereo rendering: the game draws a full frame per eye, from the headset's own pose and field of view, at the eye's own resolution.
- Menus, dialogues, the launcher, loading screens and terminals on panels in front of you, driven by the controllers' lasers. The HUD is split per element: health, compass, action points and ammunition on the back of your right hand, the interaction prompt above your fingertip, messages on a head panel.
- Real scopes (a magnified lens on the weapon, with its own reticle), binoculars and the camera.
- Optional eye-tracked foveated rendering for headsets with an eye tracker (Steam Frame), and a number of engine and frame-pacing optimisations.

**Using your hands**
- The first-person weapon is held in your right hand and shoots along it. The left grip supports it with both hands.
- Pip-Boy on the left forearm: look at it to open it, size and angle adjustable in Settings > VR; the right hand at the left wrist switches its light.
- Manual reload for every kind of weapon (magazines, cells, slides, shotgun shells, revolvers, heavy weapons, launchers), holsters at the hip and shoulder, a quick wheel for hotkeys, ammunition types.
- Melee: swing to hit, block across your face, charge power attacks. Unarmed: punch with your fists.
- Grenades, mines and thrown weapons are armed by hand and thrown with the motion of your hand.
- Aid items taken from the quick wheel are drunk, eaten, injected or read by hand, with the animations of the B42 Inject Anim Pack if you install it.
- Hold and throw objects, push them, hold the limb of a living NPC, put items away at the chest.
- Finger tracking on controllers that support it.

**Playing**
- Sitting, sneaking (stick click or a real crouch), optional room-scale walking, snap or smooth turning.
- Mini-games in the room: lock, casino games, Caravan, terminals, the vigor tester and the Reflectron, played as on a gamepad (Caravan and the slot machines use your hands). The casino, Caravan and the vigor tester keep the world running around you.
- Name entry on SteamVR's keyboard, the game's console on a terminal in front of you.
- The mod follows the game's language (English and French; other languages through `fnvvr\texts_<language>.txt`), and the game's tutorials describe the VR controls.
- Compatibility work with xNVSE and about 25 of its plugins. No plugin is ever required: without them the mod behaves exactly as before.

Controllers: Oculus Touch (Quest) and Steam Frame, with default SteamVR bindings (see [Steam Frame controls](docs/FRAME_CONTROLS.md)). Other controllers are not supported.

## Status

Developed and played on Linux (Proton), with an RTX 4070 and a Quest 2 over Steam Link; a Steam Frame and Windows testers help on the other setups. Everything that needs a headset is checked by hand before it is merged.

Not done yet:
- Some V.A.T.S. playback cameras, melee power attacks and unarmed balance, weapon spread: see the changelog and the project notes.

Known issues are listed in the notes of each release. NVAC is not supported.

## Requirements

- *Fallout: New Vegas* (Steam, 32-bit D3D9 build). The mod is written against Steam build `1510068`.
- SteamVR with a connected headset (Quest over Steam Link or Air Link, or any SteamVR headset), or an OpenXR runtime with Touch controllers (tested on Linux with WiVRn and GE-Proton), and a Vulkan-capable GPU.
- On Linux: Proton (Proton Experimental is the tested one).

## Install

1. Download the zip of a [release](https://github.com/df4l/FNVVR/releases) and copy `d3d9.dll`, `openvr_api.dll` (the 32-bit OpenVR library), `dxvk.conf` and the `fnvvr/` folder into the game directory, next to `FalloutNV.exe`. If you already have a `dxvk.conf`, merge the lines instead of overwriting it.
2. **Linux only:** set the Steam launch options to `WINEDLLOVERRIDES="d3d9=n,b" %command%`. This is not needed on Windows.
3. Start SteamVR with the headset connected, then start the game from Steam.

Your choices in the game's Settings > VR page are saved in `fnvvr/vr_options.ini`. The release's `INSTALL.txt` describes every gesture in detail.

To check that the fork is in use on Linux, look at the log (`FalloutNV_d3d9.log` next to the executable): if it shows the stock DXVK, the native override is not applied.

## Reporting a problem

The log is written next to `FalloutNV.exe` as `FalloutNV_d3d9.log`. Play for about a minute with the problem visible, quit the game, and send that file with your headset, how it is connected and what looks wrong, on [Discord](https://discord.gg/RgRVQ98jwn) or as a [GitHub issue](https://github.com/df4l/FNVVR/issues). When the game crashes, the log ends with the exception, the module and the offset.

## Community

Questions, bug reports, suggestions and testing happen on the [Discord server](https://discord.gg/RgRVQ98jwn).

## Configuration

Most options are in the game, under Settings > VR. The rest are read from `dxvk.conf` next to the executable; the choices made in the game are saved in `fnvvr/vr_options.ini` and override `dxvk.conf`, which is never rewritten. The main options are below; the full list, with every supported value, is in `dxvk.conf`.

| Option | Default | Purpose |
|--------|---------|---------|
| `d3d9.vrBackend` | `off` | `off`, `emulator`, `openvr` or `openxr`. Overridden by the `DXVK_VR_BACKEND` environment variable. The `dxvk.conf` in the release archive sets `openvr`; the one in the repository has every option commented out. |
| `d3d9.vrControllers` | on | VR controllers act as an Xbox gamepad while tracked. |
| `d3d9.vrSmoothTurn`, `d3d9.vrSnapTurnAngle`, `d3d9.vrSmoothTurnSpeed` | off, 30, 120 | Turning: snap or smooth, step in degrees, speed in degrees per second. |
| `d3d9.vrPipBoyScale`, `d3d9.vrPipBoyAngle` | 1, 0 | Pip-Boy size while open (1 to 2) and its turn about the forearm (0 to 90 degrees). |
| `d3d9.vrPanelDistance`, `d3d9.vrPanelWidth` | 2, 2 | Main menu panel, in metres. |
| `d3d9.vrHudDistance`, `d3d9.vrHudWidth`, `d3d9.vrHudHeight`, `d3d9.vrHudMessagesOffset` | 1, 1.5, -0.15, 0.25 | Head-anchored HUD and menu panel, in metres. |
| `d3d9.vrWeaponInHand` | on | Draws the first-person weapon in the right hand and shoots along it. |
| `d3d9.vrManualReload` | on | Weapons are reloaded with the hands. |
| `d3d9.vrMeleeHitSpeed`, `d3d9.vrMeleeHitsPerSecond` | 2, 3 | How fast a swing must be to hit, and how often a target can be hit. |
| `d3d9.vrScopes`, `d3d9.vrScopeResolution` | on, 768 | Scope lenses and the size of their image. |
| `d3d9.vrRoomscale` | off | Your character follows you when you walk in your room. |
| `d3d9.vrImmersiveMinigames` | on | Mini-games in the room instead of on the menu panel. |
| `d3d9.vrRealTimeCasino`, `d3d9.vrRealTimeCaravan`, `d3d9.vrRealTimeVigorTester` | on | The world keeps running during these mini-games. |
| `d3d9.vrTutorials` | on | The game's tutorials and help with the VR controls. |
| `d3d9.vrHeadsetResolution`, `d3d9.vrEyeAspect` | on | Render at the headset's eye size and shape. |
| `d3d9.vrFoveation`, `d3d9.vrFoveationFullAngle`, `d3d9.vrFoveationHalfAngle` | off, 20, 35 | Eye-tracked foveated rendering and its zones, in degrees. |
| `d3d9.vrHideGamepad` | on | Hides real gamepads from the game while a headset backend is used. |
| `d3d9.vrLauncherOverlay`, `d3d9.vrHideGameWindow` | on | The launcher on a panel in the headset, and the game's window hidden from the desktop. |
| `d3d9.vrForceGameSettings` | on | Replaces some game settings in memory for performance and stutter (values from the Viva New Vegas guide). |
| `d3d9.vrEnginePatches`, `d3d9.vrFastMath` | on | Faster, bit-identical versions of slow game functions. Each one is checked against the original bytes and skipped if a plugin already rewrote it. |
| `d3d9.vrGameHeap` | on | Small memory blocks of the game come from an allocator of the mod, to cut stutters. |
| `d3d9.vrAsyncHandOff` | on | Hands frames to the OpenVR compositor from a thread of its own. Set to `False` if the game stutters or crashes in the compositor. |

The GPU is not spoofed: the `dxvk.conf` of the release archive sets `d3d9.hideNvidiaGpu` to `False`.

Useful environment variables:

- `DXVK_VR_BACKEND=openvr|openxr|emulator|off`: overrides `d3d9.vrBackend`.
- `DXVK_LOG_LEVEL=none|error|warn|info|debug` and `DXVK_LOG_PATH=/some/directory`: logging.

## Architecture

```
IVRBackend            (src/d3d9/vr/vr_backend.h)
├── OpenVRBackend     real headset through OpenVR
├── VrOpenXrBackend   real headset through OpenXR
└── EmulatorBackend   no headset: development and automated tests
```

- The VR code is in `src/d3d9/vr/`, built into `d3d9.dll`. Types are prefixed `Vr` and live in `namespace dxvk`.
- The game-facing code only talks to `IVRBackend`. Game-space conversion, camera patching, input mapping and each feature are separate classes. The backend is chosen once at startup.
- Frames: the game's D3D9 eye textures are backed by Vulkan images on DXVK's device. `VrD3D9Bridge` takes those images through DXVK's public interop interfaces and hands them to the backend in `submitFrame`. `VrStereoRenderer` runs the game's logic once per frame and a full draw per eye.
- Panels: menus, loading screens, HUD groups, terminals and mini-game tiles are drawn on runtime panels (OpenVR overlays). Images the mod draws itself are drawn by the GPU, never by the CPU.
- Input: OpenVR action input; the manifest and the Touch and Steam Frame bindings are in `src/d3d9/vr/openvr/`.
- Game addresses are defined once, in `src/d3d9/vr/vr_game_addresses.h`, and come from the Steam executable. The only other place for addresses is the generated engine patch table.
- Hooks into the upstream DXVK code are kept small and local, so upstream changes can still be merged.

The fork is stripped down to what the 32-bit D3D9 build needs: the D3D8, D3D10, D3D11 and DXGI code is gone.

## Build

Builds run natively on Linux with Meson and Ninja, cross-compiling with mingw-w64. The mingw compiler must use the posix thread model (on Arch: `pacman -S mingw-w64-gcc meson`).

```
git submodule update --init --recursive
meson setup --cross-file build-win32.txt --buildtype release build.32
ninja -C build.32                     # output: build.32/src/d3d9/d3d9.dll
```

Unit tests for the VR layer are built with the host compiler, so they need neither Wine nor a GPU:

```
meson test -C build.32 --print-errorlogs
```

To run a build under Proton, copy `build.32/src/d3d9/d3d9.dll` into the game directory, `src/d3d9/vr/openvr/*.json` and `src/d3d9/vr/texts/*.txt` into `fnvvr/`, and use a `dxvk.conf` with `d3d9.vrBackend = openvr` (or `emulator` to run without a headset), then follow the install steps above.

## Upstream DXVK

The rest of the DXVK documentation (HUD, device filters, shader cache, debugging modes) still applies to the core that remains in this fork, and is maintained upstream at [doitsujin/dxvk](https://github.com/doitsujin/dxvk). This fork does not follow upstream's releases; merge from upstream only deliberately.

## Credits

- [DXVK](https://github.com/doitsujin/dxvk) by Philip Rebohle and contributors, the base of this fork.
- [OpenVR](https://github.com/ValveSoftware/openvr) by Valve (BSD-3-Clause), shipped as `openvr_api.dll`.
- [OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK) by the Khronos Group (Apache-2.0), headers in `include/openxr/`, loader shipped as `fnvvr/openxr_loader.dll`.
- The original FNVVR, the mod this project ports and rewrites.
- lStewieAl's Tweaks by lStewieAl and its contributors: the engine optimisations follow its inline patches.
- NVTF and B42 Optics (lens placement measured from its INI files), studied as references.
- FNV 4GB Patcher: the 4 GB offer changes the executable as it does.
- B42 Inject by Bale (B42) and the B42 Inject Anim Pack - Season 1 by Hit: consumables by hand play the pack's animations, meshes and sounds (installed by the player, not shipped), and follow B42 Inject's design.
- Steam Frame controller bindings and control reference: Madison ([astromaddie/FNVVR](https://github.com/astromaddie/FNVVR)).
- The study of which menu mode tests hold the world still, for the real-time casino: [Real Time Menus by WallSoGB](https://github.com/WallSoGB/Fallout-Real-Time-Menus) (read as a reference, no code copied).
- Testers: JaminaPanAm (Steam Frame: the foveation crash log, profiling builds), LOBO (Quest 3: Pip-Boy angle) and spinfusor_ (TTW: missing scope reticles).

## Licence

The DXVK-derived code keeps its original licence, see [LICENSE](LICENSE).
