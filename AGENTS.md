# MCXRInput project context

MCXRInput is a Minecraft Java VR-input project. Its goal is to make Minecraft
playable with a Quest 2 or PCVR headset while remaining as close to ordinary
vanilla keyboard/mouse behavior as possible, including on multiplayer servers.

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

All server use remains at the user's own risk. Do not describe the project as
guaranteed safe or approved by Hypixel.

## Intended architecture

```text
Quest 2 / PCVR / OpenXR
        -> Windows bridge application
        -> versioned localhost-only UDP messages
        -> client-only Fabric mod
        -> normal Minecraft camera and key mappings
```

Rendering is currently out of scope. ReShade or another external stereoscopic
display method may be used initially. Do not build a custom launcher or a full
Vivecraft-style renderer unless the project scope is explicitly changed.

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
- Converts headset orientation to ordinary player yaw and pitch.
- Uses `R` to recenter and `F8` to enable/disable VR camera control.
- Ignores stale poses after 250 ms and honors the tracking `active` flag.
- Supports remappable controller movement and actions through existing Minecraft
  key mappings only. Defaults are left-stick movement, A jump, B sneak, left
  stick click sprint, right trigger attack, and left trigger use. Physical
  controls use threshold hysteresis and safe re-arming.
- Navigates screens with configurable thumbstick-to-native-arrow-key focus plus
  configurable confirm/back controls. Ordinary menus have no free-moving GUI
  pointer.
- Uses a snapped cursor in container screens for physical pickup/place,
  quick-move, half-stack, and outside-drop actions through vanilla
  `AbstractContainerScreen.slotClicked` and `ContainerInput` behavior.
- Supports built-in and Fabric/mod-added Creative tabs, tab-page cycling, and
  controller scrolling through Minecraft's normal mouse-wheel handler.
- Contains three isolated accessor mixins for container clicks, Creative-tab
  geometry, and mouse movement/scrolling. It has no packet hooks, automation,
  macros, or custom serverbound gameplay packets.

The bridge folder contains:

- `native`: CMake/OpenXR sources for runtime probes, input probes, and the real
  `MCXRInputOpenXRBridge.exe` HMD/controller bridge.
- `MCXRInputBridge.exe`: basic standalone Windows GUI.
- `gui_bridge.py`: editable GUI source.
- `simulate_bridge.py`: command-line protocol simulator.

The GUI currently sends adjustable test poses. It does **not** read a real
OpenXR headset yet. Do not imply otherwise.

## Local protocol

The bridge sends one UTF-8 JSON object per UDP datagram. See
`docs/bridge-protocol.md` for the v1/v2 formats. The receiver must remain bound
to loopback only. Bridge timestamps are informational; freshness must use the
mod's local monotonic receive time.

## Development direction

Work incrementally and keep compatibility-sensitive code isolated:

1. Improve camera updates from client-tick rate toward smooth render-frame
   tracking without creating aim-assist-like smoothing.
2. Add remaining vanilla-equivalent hotbar controls through existing Minecraft
   input mechanisms.
3. Improve native directional-focus and snapped-slot compatibility for modded
   screens without adding a free-moving or controller-ray pointer.
4. Consider conservative comfort options and armswinger movement only later.

Armswinger, if ever implemented, may only hold/release vanilla forward input,
must stop immediately when physical swinging stops, and must never alter speed.

## Working style

- Prefer small, testable changes over broad rewrites.
- Keep the mod client-only and the bridge independent of game memory or packets.
- Add comments around behavior that could be mistaken for cheating or automation.
- Classify proposed features as vanilla-equivalent, potentially risky, or not
  recommended for multiplayer servers.
- Test input changes in singleplayer first.
- Preserve the basic GUI unless a more complex interface is genuinely needed.

Build the Fabric mod with `gradlew.bat build` using a Java 25 JDK. The shortest
named JAR under `build/libs` is the installable mod.
