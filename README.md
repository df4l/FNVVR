# FNVVR Reborn

A VR mod for *Fallout: New Vegas*, built into a fork of [DXVK](https://github.com/doitsujin/dxvk).

The original FNVVR was Windows-only: it shared frames between the game's DirectX 9Ex surface and a DirectX 11 VR pipeline. That interop does not exist on Linux (Wine/Proton). This project takes a different route: the VR layer lives inside the DXVK fork that translates the game's Direct3D 9 calls to Vulkan, and the eye images are shared with the VR runtime through Vulkan instead.

The output is a single 32-bit `d3d9.dll`, which the game loads in place of the stock one. It runs under Proton on Linux and under Wine or Windows.

## Status

- Stereo rendering with the game drawing one frame per eye.
- Main menu, in-game menus and subtitles on panels in front of the head.
- Mouse-style menu input through the controllers, with a laser pointer for the menu panels.
- Motion controls: move, snap or smooth turn, buttons and sticks mapped to the game's gamepad, first-person weapon held in the right hand.
- Not yet done: the compass and health bar on the back of the hand, the hand-anchored interaction prompt, the immersive forearm Pip-Boy, optics and scopes, and culling and cloud behaviour with head rotation.

Work is tested with the emulator backend and unit tests first. Anything that needs a headset is verified by hand and is listed as confirmed or pending in the project notes.

## Architecture

```
IVRBackend            (src/d3d9/vr/vr_backend.h)
├── OpenVRBackend     real headset through OpenVR
└── EmulatorBackend   no headset: development and automated tests
```

- The VR code is in `src/d3d9/vr/`, built into `d3d9.dll`. Types are prefixed `Vr` and live in `namespace dxvk`.
- The game-facing code only talks to `IVRBackend`. Game-space conversion, camera patching and input mapping are separate classes.
- Frames: the game's D3D9 eye textures are backed by Vulkan images on DXVK's device. `VrD3D9Bridge` takes those images through DXVK's public interop interfaces and hands them to the backend in `submitFrame`.
- Game addresses are defined once, in `src/d3d9/vr/vr_game_addresses.h`. Their origin is recorded in the project's `findings/` notes.
- Hooks into the upstream DXVK code are kept small and local, so upstream changes can still be merged.

The fork is being stripped down to what the 32-bit D3D9 build needs. The D3D8, D3D10, D3D11 and DXGI code is already gone; the rest of the cleanup is tracked in the commit history.

## Build

Builds run natively on Linux with Meson and Ninja, cross-compiling with mingw-w64. The mingw compiler must use the posix thread model.

```
git submodule update --init --recursive
meson setup --cross-file build-win32.txt --buildtype release build.32
ninja -C build.32                     # output: build.32/src/d3d9/d3d9.dll
```

Unit tests for the VR layer are built with the host compiler, so they need neither Wine nor a GPU:

```
meson test -C build.32 --print-errorlogs
```

## Running with Proton

1. Copy `build.32/src/d3d9/d3d9.dll` into the game directory, next to `FalloutNV.exe`.
2. Copy `dxvk.conf` next to the executable and set `d3d9.vrBackend` (`off`, `emulator` or `openvr`).
3. Copy `src/d3d9/vr/openvr/*.json` into `fnvvr/` in the game directory. These are the SteamVR action manifest and default bindings.
4. Set the Steam launch options to `WINEDLLOVERRIDES="d3d9=n,b" %command%`.
5. Start SteamVR with the headset connected before starting the game.

To check that the fork is in use, look at the DXVK log: if it shows the stock DXVK, the native override is not applied.

## Configuration

Settings are read from `dxvk.conf` next to the executable. The main VR options are below; the full list is in `dxvk.conf`.

| Option | Default | Purpose |
|--------|---------|---------|
| `d3d9.vrBackend` | `off` | `off`, `emulator` or `openvr`. Overridden by the `DXVK_VR_BACKEND` environment variable. |
| `d3d9.vrPanelDistance`, `d3d9.vrPanelWidth` | 2, 2 | Main menu panel, in metres. |
| `d3d9.vrHudDistance`, `d3d9.vrHudWidth`, `d3d9.vrHudHeight` | 1, 1.5, -0.15 | Head-anchored HUD and menu panel, in metres. |
| `d3d9.vrHudMessagesOffset` | 0.25 | Moves messages and objectives towards the panel centre, in metres. |
| `d3d9.vrControllers` | on | VR controllers act as an Xbox gamepad while tracked. |
| `d3d9.vrSmoothTurn` | off | Smooth turning instead of snap turning. |
| `d3d9.vrSnapTurnAngle` | 30 | Snap turn step, in degrees. |
| `d3d9.vrSmoothTurnSpeed` | 120 | Smooth turn speed, in degrees per second. |
| `d3d9.vrWeaponInHand` | on | Draws the first-person weapon in the right hand and shoots along it. |
| `d3d9.vrHeadsetResolution` | on | Renders at the headset's eye height and at least 4:3 width. |
| `d3d9.vrHideGamepad` | on | Hides real gamepads from the game while a headset backend is used. |

The GPU is not spoofed: the deployed `dxvk.conf` sets `d3d9.hideNvidiaGpu` to `False`.

Useful environment variables:

- `DXVK_VR_BACKEND=openvr|emulator|off`: overrides `d3d9.vrBackend`.
- `DXVK_LOG_LEVEL=none|error|warn|info|debug` and `DXVK_LOG_PATH=/some/directory`: logging.

## Upstream DXVK

The rest of the DXVK documentation (HUD, device filters, shader cache, debugging modes) still applies to the core that remains in this fork, and is maintained upstream at [doitsujin/dxvk](https://github.com/doitsujin/dxvk). This fork does not follow upstream's releases; merge from upstream only deliberately.

## Licence

The DXVK-derived code keeps its original licence, see [LICENSE](LICENSE).
