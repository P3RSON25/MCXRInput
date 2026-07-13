# MCXRInput project context

MCXRInput is a Minecraft Java VR-input project. Its goal is to make Minecraft
playable with a Quest 2 or PCVR headset while remaining as close to ordinary
vanilla keyboard/mouse behavior as possible, including multiplayer servers that
explicitly permit it.

## Core principle

The server should see normal Minecraft Java input and normal player behavior.
This is an accessibility/control-layer mod, not a hacked client, anticheat
bypass, automation tool, or full VR renderer.

Never add:

- Packet spoofing, anticheat evasion, mod hiding, reach or speed changes.
- Aim assist, attack automation, auto-clicking, macros, inventory automation,
  AFK behavior, farming/mining automation, or repeated perfect-timing inputs.
- Any behavior that continues playing without active physical user input.
- Custom serverbound gameplay packets when an existing Minecraft input or
  key-mapping mechanism can perform the action normally.

Vanilla input mechanisms do not imply server permission. Under Hypixel's current
official allowed-modification categories, MCXRInput must be presumed disallowed.
Documentation must tell users not to use it on Hypixel and to keep a separate
clean Hypixel profile. Never imply that Hypixel permits this project.

## Intended architecture

```text
Quest 2 / PCVR / OpenXR
        -> Windows bridge application
        -> versioned localhost-only UDP messages
        -> client-only Fabric mod
        -> normal Minecraft camera and key mappings

ReShade half-SBS Minecraft window
        -> the same Windows bridge/OpenXR session
        -> external roll-stabilized per-eye projection
```

The display scope has been expanded only to an external OpenXR virtual-screen
path for an existing ReShade half-SBS Minecraft window. Do not build a custom
launcher or a full Vivecraft-style renderer unless the project scope is
explicitly changed again.

## Current baseline

- Minecraft Java 26.2
- Java 25
- Fabric Loader 0.19.3
- Fabric API 0.154.2+26.2
- Fabric Loom 1.17
- Minecraft 26.1+ is unobfuscated, so use Mojang's official names rather than
  Yarn mappings.

## What currently exists

The Fabric prototype:

- Receives HMD orientation over UDP on `127.0.0.1:28771`.
- Validates and normalizes OpenXR-style `[x, y, z, w]` quaternions.
- Applies only physical HMD orientation deltas to the player's current yaw and
  pitch; a stationary headset never restores an older camera target.
- Starts VR input disabled on every launch/world change. `F8` manually enables
  or disables it for that world, and `R` resets the HMD reference.
- Ignores stale poses after 250 ms and honors the tracking `active` flag.
- Supports remappable controller movement and actions through existing Minecraft
  key mappings only. Defaults are left-stick movement, A jump, B sneak, left
  stick click sprint, right trigger attack, and left trigger use. Key mappings
  change only on physical transitions, with threshold hysteresis and safe
  re-arming. Toggle Crouch/Sprint/Attack/Use fail closed while enabled.
- Selects hotbar slots with configurable right-stick left/right input by changing
  Minecraft's normal local selected-slot state once per neutral-to-deflected
  gesture. Gameplay-affecting sticks have no timed repeat.
- Navigates screens with configurable thumbstick-to-native-arrow-key focus plus
  configurable confirm/back controls. Ordinary menus have no free-moving GUI
  pointer.
- Uses a snapped cursor in container screens for physical pickup/place,
  quick-move, half-stack, and outside-drop actions through vanilla
  `AbstractContainerScreen.slotClicked` and `ContainerInput` behavior. Each
  physical edge can perform at most one action, and the controller is disabled
  by default on multiplayer unless explicitly opted in.
- Supports built-in and Fabric/mod-added Creative tabs, tab-page cycling, and
  controller scrolling through Minecraft's normal mouse-wheel handler.
- Provides a remappable hold-and-release utility wheel for pause, chat,
  hold-to-view player list, and perspective behavior. The wheel is
  client-only/non-pausing, blocks gameplay controls while held, and keeps HMD
  camera tracking active without persistent post-release key state.
- Contains three isolated accessor mixins for container clicks, Creative-tab
  geometry, and mouse movement/scrolling, plus isolated render hooks for HMD
  camera deltas and temporary offered FOV. It has no packet hooks, gameplay
  automation, macros, or custom serverbound gameplay packets.
- Provides config-v9 HUD controls. A fresh unified-display offer defaults to an
  automatic safe-area recommendation derived from the frozen eye crop; the
  default-off manual safe area overrides it. Both translate supported vanilla
  in-world HUD groups without scaling. Full screens, the crosshair, full-screen
  overlays, and unknown mod HUD elements are unchanged.

The bridge folder contains:

- `native`: CMake/OpenXR sources for runtime probes, input probes, the standalone
  `MCXRInputCaptureProbe.exe` half-SBS window-capture diagnostic, the synthetic
  eye-routing/roll-stabilization `MCXRInputOpenXRStereoScreenProbe.exe`, and the
  bounded live `MCXRInputOpenXRCaptureScreenProbe.exe` GPU capture/display
  diagnostic, the full-FOV `MCXRInputOpenXRImmersiveCaptureProbe.exe` hardware
  checkpoint, plus the real `MCXRInputOpenXRBridge.exe`. The real bridge retains
  controls-only mode and optionally combines half-SBS capture, automatic
  immersive-world/finite-menu presentation, HMD/controller actions, and
  protocol-v2 UDP in one OpenXR session.
- `MCXRInputBridge.exe`: stale synthetic-test GUI binary without a reviewed,
  reproducible packaging recipe; do not distribute or use it for multiplayer.
- `gui_bridge.py`: editable GUI source.
- `simulate_bridge.py`: command-line protocol simulator.

The Python GUI sends synthetic protocol-v1 test poses and does **not** read a
real OpenXR headset. Its sweep defaults off and it is development/singleplayer
only. The tracked `MCXRInputBridge.exe` has no reproducible reviewed packaging
recipe, is stale relative to the Python source, and must not be distributed or
used for multiplayer.

## Local protocol

Gameplay input uses one UTF-8 JSON object per UDP datagram. See
`docs/bridge-protocol.md` for the v1/v2 formats. Production accepts v2 only; v1
requires the explicit development JVM property and is runtime-rejected in remote
multiplayer and published LAN worlds. Unified display mode additionally uses the
strict display-only `MCXRD1` offer/state grammar over the same UDP socket. It may
coordinate temporary rendered FOV, automatic HUD inset, and
world/screen/overlay/no-world presentation, but must never carry gameplay input.
The receiver must remain bound to loopback only. Bridge timestamps are
informational; freshness must use the mod's local monotonic receive time.

## Development direction

Work incrementally and keep compatibility-sensitive code isolated:

1. Hardware-validate automatic immersive-world/finite-screen switching, exact
   FOV coordination, and HUD visibility while preserving controls-only mode.
2. Add remaining one-physical-input-to-one-Minecraft-input hotbar controls
   through existing Minecraft mechanisms; this describes mechanism, not policy.
3. Improve native directional-focus and snapped-slot compatibility for modded
   screens without adding a free-moving or controller-ray pointer.
4. Consider conservative comfort options and armswinger movement only later.

Keep the finite-quad menu comfort mode isolated to display coordination. Do not
expand it into a custom renderer or launcher.

Armswinger, if ever implemented, may only hold/release vanilla forward input,
must stop immediately when physical swinging stops, and must never alter speed.

## Working style

- Prefer small, testable changes over broad rewrites.
- Keep the mod client-only and the bridge independent of game memory or packets.
- Add comments around behavior that could be mistaken for cheating or automation.
- Classify proposed features as one-to-one mechanism (without implying server
  permission), potentially risky, or not recommended for multiplayer servers.
- Test input changes in singleplayer first.
- Preserve the basic GUI unless a more complex interface is genuinely needed.

Build the Fabric mod with `gradlew.bat build` using a Java 25 JDK. The shortest
named JAR under `build/libs` is the installable mod.
