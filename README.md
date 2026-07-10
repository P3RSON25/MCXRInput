# MCXRInput

MCXRInput is an early, client-only Fabric prototype that turns real headset
orientation into ordinary Minecraft camera rotation. It is an input/accessibility
layer, not a gameplay automation mod or a full VR renderer.

## Phase 1 scope

- Minecraft Java 26.2, Fabric Loader 0.19.3, Fabric API 0.154.2
- Java 25 and Fabric Loom 1.17
- Versioned JSON-over-UDP bridge protocol bound only to `127.0.0.1`
- Head quaternion to player yaw/pitch
- Optional `config/mcxrinput.json` settings and controller remapping, with a
  Mod Menu config screen when Mod Menu is installed
- Controller movement and physical attack/use triggers through ordinary
  Minecraft key mappings
- `R` recenter and `F8` enable/disable key mappings (both configurable in Controls)
- 250 ms stale-input cutoff and tracking-active gate
- Camera updates pause while Minecraft screens/overlays are open, then re-anchor
  when returning to gameplay to avoid menu-induced camera snaps
- Thumbstick menu navigation through Minecraft's native directional focus,
  with configurable confirm/back controls and no GUI pointer
- No armswinger, custom gameplay packets, or mixins

Minecraft 26.1 and newer are unobfuscated, so Fabric now uses Mojang's official
class/member names rather than Yarn mappings.

## Build

Install a Java 25 JDK, then run:

```powershell
.\gradlew.bat build
```

The installable mod is the shortest-named JAR in `build/libs`. Put it and the
matching Fabric API JAR in a separate Fabric 26.2 instance.

## Configuration

MCXRInput writes its config to `config/mcxrinput.json` inside the Minecraft
instance folder:

```json
{
  "configVersion": 2,
  "hmdYawSensitivity": 1.0,
  "hmdPitchSensitivity": 1.0,
  "controllerDeadzone": 0.35,
  "triggerThreshold": 0.55,
  "movementStick": "left",
  "jumpBinding": "right_a",
  "sneakBinding": "right_b",
  "sprintBinding": "left_stick_click",
  "attackBinding": "right_trigger",
  "useBinding": "left_trigger",
  "menuNavigationStick": "left",
  "menuConfirmBinding": "right_a",
  "menuBackBinding": "right_b"
}
```

The default HMD sensitivity is 1:1. If Mod Menu is installed, MCXRInput exposes a
config button there that edits the same file, including separate gameplay and
menu binding pages. Each binding button cycles through the physical OpenXR
controls; `Unbound` disables that action. Existing v1 configs migrate to v2
without losing their numeric settings. Mod Menu is optional and is not required
to run MCXRInput.

## Try the input path

1. Start Minecraft through a Fabric development run or install the built JAR.
2. Enter a singleplayer world.
3. Start a gentle test stream:

   ```powershell
   python .\bridge\simulate_bridge.py --sweep
   ```

4. Press `R` once. The view should follow the simulated yaw.
5. Press `F8` to give camera control back to normal mouse input.

The simulator is only a transport test. To use the headset's real OpenXR
orientation, build and run the native bridge described below.

### Basic Windows GUI

Run `bridge/gui_bridge.py`, or launch `MCXRInputBridge.exe` when using a packaged
build. Set the same local port as the mod, press **Start**, enter a world, and
press `R` to recenter. This GUI sends test poses; it does not read a headset yet.

## Native OpenXR runtime probe

The first native bridge milestone is a console probe that verifies Windows can
reach the active OpenXR runtime and discover a connected headset. It does not
read live poses or send UDP messages yet.

Open an **x64 Native Tools Command Prompt for Visual Studio**, then run:

```bat
cmake -S bridge\native -B bridge\native\build -A x64
cmake --build bridge\native\build --config Release
bridge\native\build\Release\MCXRInputOpenXRProbe.exe
```

CMake fetches the pinned official Khronos OpenXR SDK/loader during the first
configure. For a successful probe, start SteamVR, connect the Quest through
Steam Link, Quest Link, or Air Link, and confirm SteamVR sees the headset before
running the executable.

### Live OpenXR pose/controller probe

The second native milestone creates a passive OpenXR session through SteamVR and
prints live HMD pose plus read-only controller action status. It avoids beginning
a rendering session, so it should not take over the headset display. It still
does not send UDP, drive Minecraft input, or modify the Python GUI.

After building the native project, run:

```bat
bridge\native\build\Release\MCXRInputOpenXRPoseProbe.exe
```

Expected output should update for about 15 seconds. Move the headset; the HMD
position/quaternion should change. Wake the controllers; their lines may report
inactive in this passive probe until the later D3D11 session milestone.

### Focused D3D11 OpenXR input probe

The third native milestone creates a minimal D3D11-backed OpenXR session and
submits a blank dark stereo layer. This is still a diagnostic console program,
but SteamVR should treat it as a real focused VR app so controller actions can
be read.

After building the native project, run:

```bat
bridge\native\build\Release\MCXRInputOpenXRD3D11InputProbe.exe
```

Expected output should update for about 20 seconds. Move the headset and press
controller trigger/grip/buttons/sticks. The headset may briefly show a blank
dark MCXRInput app while the probe has focus. Button values marked with `*`
were pressed at least once since the previous console line; trigger/squeeze
`peak` and stick `peakMag`/`maxAbs` summarize movement between printed lines.

### Real OpenXR HMD/controller bridge

The first real bridge executable uses the same focused D3D11 OpenXR session path
as the successful input probe, reads live HMD orientation and controller actions,
and sends protocol v2 UDP datagrams to the Fabric mod on `127.0.0.1:28771`.

After building the native project, start Minecraft with the mod installed, enter
a singleplayer world, start SteamVR with the headset awake, then run:

```bat
bridge\native\build\Release\MCXRInputOpenXRBridge.exe
```

To use a development port that matches `-Dmcxrinput.port=...`, pass `--port`:

```bat
bridge\native\build\Release\MCXRInputOpenXRBridge.exe --port 28772
```

Press `R` in Minecraft to recenter. The desktop Minecraft camera should follow
headset yaw and pitch. Head roll is intentionally stabilized away because
ordinary Minecraft camera control has no roll axis. The headset may show a blank
dark MCXRInput app while SteamVR focuses the bridge; in-headset display of
Minecraft is a later rendering/viewing milestone.

Default controller mapping is intentionally conservative and can be changed in
the Mod Menu settings or `config/mcxrinput.json`:

- Left stick maps to vanilla forward/back/left/right key mappings.
- Right `A` maps to jump.
- Right `B` maps to sneak.
- Left stick click maps to sprint when SteamVR exposes it.
- Right trigger maps to vanilla attack/destroy.
- Left trigger maps to vanilla use/place.
- Controller input releases while screens/overlays are open or if bridge input
  goes stale.

While a Minecraft screen is open, the configured menu thumbstick behaves like
the keyboard arrow keys: one dominant direction at a time, followed by a
controlled key-like repeat while held. Right `A` confirms and right `B` goes
back by default. This uses Minecraft's native focus navigation rather than a
virtual mouse, so ordinary menus and focusable mod screens work; inventory slot
cursor control is not part of this milestone.

Trigger pulls use the configured threshold and a small release hysteresis. A
trigger held through a menu, F8 disable, stale frame, or tracking loss must be
released before it can act again. Right-stick turning, inventory-slot controls,
and hotbar controls are deferred. A GUI pointer is deliberately not planned for
the current controller-navigation design.

## Server-safety boundary

This phase is vanilla-equivalent in mechanism: it changes the local player's
normal yaw/pitch fields and holds/releases existing Minecraft key mappings.
Vanilla decides when to send its normal movement/rotation updates. MCXRInput
contains no server packet code and no automated actions.

Use of any client mod on a multiplayer server can still be restricted or
"use at your own risk." Test in singleplayer first and check the current rules
of any server before joining with the mod.

See [docs/bridge-protocol.md](docs/bridge-protocol.md) for the wire format.
