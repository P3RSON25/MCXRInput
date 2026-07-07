# MCXRInput

MCXRInput is an early, client-only Fabric prototype that turns real headset
orientation into ordinary Minecraft camera rotation. It is an input/accessibility
layer, not a gameplay automation mod or a full VR renderer.

## Phase 1 scope

- Minecraft Java 26.1.2, Fabric Loader 0.19.3, Fabric API 0.154.0
- Java 25 and Fabric Loom 1.17
- Versioned JSON-over-UDP bridge protocol bound only to `127.0.0.1`
- Head quaternion to player yaw/pitch
- `R` recenter and `F8` enable/disable key mappings (both configurable in Controls)
- 250 ms stale-input cutoff and tracking-active gate
- No controller buttons, GUI pointer, armswinger, custom gameplay packets, or mixins

Minecraft 26.1 and newer are unobfuscated, so Fabric now uses Mojang's official
class/member names rather than Yarn mappings.

## Build

Install a Java 25 JDK, then run:

```powershell
.\gradlew.bat build
```

The installable mod is the shortest-named JAR in `build/libs`. Put it and the
matching Fabric API JAR in a separate Fabric 26.1.2 instance.

## Try the input path

1. Start Minecraft through a Fabric development run or install the built JAR.
2. Enter a singleplayer world.
3. Start a gentle test stream:

   ```powershell
   python .\bridge\simulate_bridge.py --sweep
   ```

4. Press `R` once. The view should follow the simulated yaw.
5. Press `F8` to give camera control back to normal mouse input.

The simulator is only a transport test. The next bridge milestone is a Windows
OpenXR process that replaces its generated quaternion with the runtime's real
`XrSpace` HMD pose.

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

## Server-safety boundary

This phase is vanilla-equivalent in mechanism: it changes the local player's
normal yaw/pitch fields, and vanilla decides when to send its normal rotation
updates. MCXRInput contains no server packet code and no automated actions.

Use of any client mod on a multiplayer server can still be restricted or
"use at your own risk." Test in singleplayer first and check the current rules
of any server before joining with the mod.

See [docs/bridge-protocol.md](docs/bridge-protocol.md) for the wire format.
