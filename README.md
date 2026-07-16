# MCXRInput
# The only human written part of this repo
this is vr minecraft 3Dof, this means its like VR video but you can also control minecraft. The point of it is its supposed to be basically vanilla but you can use your HMD as a controller. The goal was to play Hypixel Skyblock in VR, but Im too cowardly to do it because I don't want to get wiped.

![Donkey slaying](https://files.catbox.moe/9lrajg.gif)![Menu Navigation](https://files.catbox.moe/tjc61x.gif)

features:
  - controls for moving, attacking, mining, building
  - controls for menus, like how controlify does it
  - cosmetic vr arms, and rendered torso and legs
  - Looking around

idk how this project works but to use it you need to have minecraft 26.2 rendering SBS stereoscopic 3d. I used reshade superdepth3d but theres sbs mods that might work idk. I have in the releases the preset i used for superdepth3d. Also there already is a minecraft vr mod called MCXR so imma have this whole shabang renamed when I get more codex usage. Also in the settings of the mod theres some controls config, you can view it with the mod menu mc mod. 

Now these are the step by step instructions to use:
1. Install mcxr0.1.16.jar and fabric api
2. Put in this java flag -Dmcxrinput.development.trackedAvatar=true
3. Load into a world
4. Connect to your pc with your HMD, i used steamvr cause im too broke for virtual desktop
3. Run this powershell script

& "[ Replace with path to MCXRInputOpenXRBridge.exe]" `
  --executable "[Replace with path to your Minecraft's javaw.exe]" `
  --port 28771 `
  --fit cover `
  --source-vfov-deg 150 `
  --world-view-scale 0.40 `
  --roll-coverage-deg 15 `
  --menu-width-m 1.6 `
  --menu-distance-m 1.5 `
  --eye-order lr
  
4. Press R to recenter, and f8 to start input

# How CHATGPT 5.6 Was used for this Project
Everything. I vibecoded every single line of code in this repo. I just said what I wanted and ChatGPT 5.6 Ultra wrote it.

# Everything CHATGPT wrote
MCXRInput is an early, client-only Fabric prototype that turns real headset
orientation into ordinary Minecraft camera rotation. It is an input/accessibility
layer, not a gameplay automation mod or a full VR renderer.

## Multiplayer and Hypixel warning

MCXRInput is not an anticheat bypass and does not attempt to hide itself or alter
server packets. That does **not** make it permitted by a server. Hypixel's official
rules say modifications outside its listed allowed categories should be assumed
disallowed; MCXRInput changes live gameplay input and is therefore presumed
disallowed on Hypixel. **Do not use MCXRInput on Hypixel.** Keep a separate clean
Minecraft profile for Hypixel with this mod and its bridge absent.

Test every build in singleplayer first. Vanilla key mappings and packets generated
by vanilla Minecraft do not guarantee permission on any multiplayer server. Obtain
explicit permission from a server's current rules or staff before using this mod.
See [Hypixel's official Allowed Modifications policy](https://support.hypixel.net/hc/en-us/articles/6472550754962-Hypixel-Allowed-Modifications).

## Phase 1 scope

- Minecraft Java 26.2, Fabric Loader 0.19.3, Fabric API 0.154.2
- Java 25 and Fabric Loom 1.17
- Versioned JSON-over-UDP bridge protocol bound only to `127.0.0.1`
- Optional one-process OpenXR presentation of an existing ReShade half-SBS
  Minecraft window; controls-only mode remains available
- Optional bounded `0.30..1.0` tangent-space world view scale fed by up to a
  160-degree captured render; the calibrated default remains `1.0`
- Movement-only HMD orientation deltas added to the current player yaw/pitch
- Optional `config/mcxrinput.json` settings and controller remapping, with a
  Mod Menu config screen when Mod Menu is installed
- Controller movement and physical attack/use triggers through ordinary
  Minecraft key mappings
- Optional validated OpenXR grip-pose telemetry for default-off client-only
  cosmetic arms and rigid held-item visuals; it is independent of gameplay
  buttons and sends no server packet
- One right-stick neutral-to-deflected gesture selects one adjacent hotbar slot
- Hold-and-release utility wheel for pause, chat, player list, and perspective
- VR input starts disabled on launch and every world/server change; `F8` manually
  enables/disables it and `R` resets the HMD reference
- 250 ms stale-input cutoff and tracking-active gate
- Camera updates pause while Minecraft screens/overlays are open, then re-anchor
  when returning to gameplay to avoid menu-induced camera snaps. The non-pausing
  utility wheel is the sole exception, so HMD camera tracking continues behind it.
- Thumbstick menu navigation through Minecraft's native directional focus,
  with configurable confirm/back controls and no GUI pointer
- Single-gesture snapped inventory navigation and single-press pickup, quick-move,
  half-stack, and outside-drop behavior; all inventory controller input defaults
  off on multiplayer
- Default-on automatic safe-area placement of supported vanilla HUD groups while
  the unified immersive bridge is fresh, plus a default-off manual override. The
  bottom-center hotbar/status cluster is uniformly reduced when translation
  cannot keep the hotbar, health, armor, hunger, air, mount-health, and XP layers
  inside the visible crop.
- Automatic finite, gravity-level comfort-quad presentation for pause, chat,
  inventory, title, loading-overlay, and other full Minecraft screens
- No armswinger or custom gameplay packets; three isolated accessor mixins expose
  Minecraft's existing container-screen, Creative-tab, and mouse-position methods,
  while isolated render hooks cover camera/FOV timing and contextual-bar alignment
- Separate default-off singleplayer checkpoints provide either tracked grip
  alignment markers or skin-aware two-segment arms with rigid held items

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
  "configVersion": 9,
  "hmdYawSensitivity": 1.0,
  "hmdPitchSensitivity": 1.0,
  "controllerDeadzone": 0.35,
  "triggerThreshold": 0.55,
  "allowInventoryInputInMultiplayer": false,
  "hudSafeAreaEnabled": false,
  "automaticImmersiveHudSafeArea": true,
  "hudSafeAreaHorizontalInset": 0.31,
  "hudSafeAreaVerticalInset": 0.09,
  "movementStick": "left",
  "hotbarStick": "right",
  "jumpBinding": "right_a",
  "sneakBinding": "right_b",
  "sprintBinding": "left_stick_click",
  "attackBinding": "right_trigger",
  "useBinding": "left_trigger",
  "inventoryBinding": "left_y",
  "menuNavigationStick": "left",
  "menuConfirmBinding": "right_a",
  "menuBackBinding": "right_b",
  "inventorySelectBinding": "right_a",
  "inventoryQuickMoveBinding": "left_y",
  "inventoryTakeHalfBinding": "left_x",
  "inventoryDropBinding": "left_y",
  "inventoryScrollStick": "right",
  "creativeNextTabBinding": "right_grip",
  "creativePreviousTabBinding": "left_grip",
  "utilityWheelBinding": "right_stick_click"
}
```

The default and maximum HMD sensitivity is 1:1; v8 conservatively clamps older
values above `1.0` instead of amplifying physical head movement. If Mod Menu is
installed, MCXRInput exposes a config button there that edits the same file,
including separate gameplay, menu, inventory, utility, and HUD-safe-area pages.
While a fresh unified-display offer exists, the default automatic HUD safe area
uses the native bridge's frozen aligned physical-eye crop to move selected
vanilla HUD groups inward. Fixed margins remain conservative during ordinary
head roll; they are not a formal guarantee for every corner at maximum roll.
Enabling the manual HUD safe area overrides that recommendation.
The vanilla bottom-center hotbar/status cluster is also scaled uniformly around
its shared anchor only when the complete offhand-inclusive hotbar width would
otherwise cross that safe area. This keeps health, armor, hunger, air, mount
health, the contextual XP/jump bar, and the XP level proportional to the hotbar.
An exact Minecraft 26.2 render hook keeps locator-bar marker details aligned with
the contextual background; it does not add or alter waypoint information.
Neither mode transforms screens, containers, the crosshair, full-screen overlays,
or unknown mod-added elements. Each binding button cycles through the physical
OpenXR controls; `Unbound` disables that action. Older configs migrate to v9 while
preserving bindings and in-range numeric values; yaw/pitch sensitivities above
`1.0` are intentionally reduced to `1.0`. Mod Menu is optional and is not required
to run MCXRInput.

## Try the input path

1. Start Minecraft through a Fabric development run or install the built JAR.
2. Enter a singleplayer world.
3. Add this **development-only** JVM option to that singleplayer instance:

   ```text
   -Dmcxrinput.development.allowProtocolV1TestPoses=true
   ```

4. Start the synthetic test stream:

   ```powershell
   python .\bridge\simulate_bridge.py --sweep
   ```

5. Press `F8` to enable VR input, then press `R` once. The view should follow
   physical changes in the simulated yaw.
6. Press `F8` again to disable and release all MCXRInput-owned input.

Protocol v1 and both Python senders are synthetic development tests for a
dedicated singleplayer profile only. Production defaults reject v1, and the
runtime rejects v1 in remote multiplayer or published LAN worlds even when the
development property is set. To use the
headset's real OpenXR orientation, build and run the native bridge described below.

### Basic Windows GUI

`bridge/gui_bridge.py` is a synthetic protocol-v1 development tool. Its sweep is
off by default, it requires the explicit v1 JVM option above, and it must be used
only in singleplayer. It does **not** read a headset.

The tracked `bridge/MCXRInputBridge.exe` cannot currently be reproduced from a
reviewed packaging recipe and is stale relative to `gui_bridge.py`. Do not
distribute it or use it for multiplayer. It was intentionally not modified or
deleted during this hardening pass.

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

### Minecraft half-SBS window capture probe

`MCXRInputCaptureProbe.exe` is a bounded Windows Graphics Capture diagnostic.
It captures one visible Minecraft window into a D3D11 texture, verifies that the
width can be divided into equal half-SBS eyes, reports frame/resize statistics,
and saves the newest valid frame as a PNG. It does not start OpenXR, take over
the headset, read game memory, send UDP, or generate Minecraft input.

Minecraft should be windowed or borderless-windowed with ReShade half-SBS
enabled. First list windows belonging to the launcher's exact Java runtime:

```bat
bridge\native\build\Release\MCXRInputCaptureProbe.exe --list-windows --executable "C:\path\to\javaw.exe"
```

If exactly one window matches, capture it for the default ten seconds:

```bat
bridge\native\build\Release\MCXRInputCaptureProbe.exe --executable "C:\path\to\javaw.exe" --snapshot bridge\native\build\minecraft-half-sbs.png
```

Use `--seconds 1..300` to change the bounded duration. If more than one window
uses that Java runtime, copy the listed hexadecimal handle and select it with
`--window 0x...` instead. A successful PNG must contain the complete ReShade
half-SBS frame; this probe does not split the image or display it in the HMD yet.

### Synthetic OpenXR stereo-screen probe

`MCXRInputOpenXRStereoScreenProbe.exe` proves the headset-display side without
mixing in Minecraft capture. It opens a focused D3D11 OpenXR session and submits
two core quad layers at the same physical pose: a blue `L` card visible only to
the physical left eye and an orange `R` card visible only to the physical right
eye. It does not capture Minecraft, send UDP, read controllers, or generate
gameplay input.

Start SteamVR with the headset connected and awake, then run:

```bat
bridge\native\build\Release\MCXRInputOpenXRStereoScreenProbe.exe --seconds 30
```

Check the following in the headset:

1. Close each eye separately. The physical left eye must see only the blue `L`,
   and the physical right eye must see only the orange `R`.
2. Move, yaw, and pitch your head. The card should remain centered 1.5 metres
   along physical gaze.
3. Tilt your head toward either shoulder. The card should remain level with
   gravity instead of rolling with the headset.

The default virtual screen is 1.6 metres wide with the test texture's 16:9
aspect ratio. `--distance-m` and `--width-m` adjust those diagnostic values.
`--eye-order rl` deliberately reverses only the test textures and is useful for
confirming the eye-routing check. The probe stops on its own and can be stopped
early with Ctrl+C. Seeing these synthetic cards is the required checkpoint
before running the live Minecraft capture-screen probe below.

### Live Minecraft capture-screen probe

`MCXRInputOpenXRCaptureScreenProbe.exe` connects the two isolated diagnostics:
it captures the selected ReShade half-SBS Minecraft window on SteamVR's required
D3D11 adapter, decodes the squeezed left/right halves entirely on the GPU, and
places them on the proven roll-stabilized eye-specific quads. This remains a
bounded display diagnostic. It does not send UDP, read controllers, or generate
Minecraft input. It intentionally preserves the finite-screen presentation as a
comparison point for the full-FOV diagnostic below.

Start the borderless Minecraft instance with ReShade half-SBS active, then start
SteamVR and run:

```powershell
.\bridge\native\build\Release\MCXRInputOpenXRCaptureScreenProbe.exe `
  --executable "C:\path\to\javaw.exe" `
  --seconds 30
```

Use `--list-windows --executable "C:\path\to\javaw.exe"` first if the exact
window is uncertain; if multiple windows match, select the printed hexadecimal
handle with `--window 0x...`. In the headset, confirm that Minecraft updates
live, the physical eyes receive the correct ReShade views, head roll leaves the
screen gravity-level, and the process exits cleanly. Minimize/restore and capture
resizes invalidate old imagery until a fresh frame arrives; frames older than
500 ms are never submitted, and a five-second capture starvation fails the
diagnostic instead of reporting success from one earlier frame.

This executable owns the focused OpenXR session and therefore is not intended
to run beside `MCXRInputOpenXRBridge.exe`. Use ordinary desktop input for this
display checkpoint. The real bridge can now combine the proven immersive path
with HMD/controller input in one process; that mode is documented below.

### Full-FOV immersive capture probe

`MCXRInputOpenXRImmersiveCaptureProbe.exe` is the bounded hardware oracle for
the immersive presentation. It captures the same half-SBS window and submits a
core two-eye projection layer with fixed, tangent-correct source mapping. It
removes physical head roll from the presented image while retaining the
runtime's eye cant and IPD. It sends no UDP and reads no controller actions.

The Quest-tested command is:

```powershell
.\bridge\native\build\Release\MCXRInputOpenXRImmersiveCaptureProbe.exe `
  --executable "C:\Users\wenyu\AppData\Roaming\ElyPrismLauncher\java\java-runtime-epsilon\bin\javaw.exe" `
  --seconds 60 `
  --fit cover `
  --source-vfov-deg 110 `
  --world-view-scale 1 `
  --roll-coverage-deg 15 `
  --eye-order lr
```

The default fit is undistorted `cover` with a declared 110-degree source
vertical FOV and `--world-view-scale 1`, which preserves the calibrated 1:1
tangent-space view. The optional scale accepts only `0.30..1`; a lower value
samples wider source rays without changing the submitted OpenXR frustum, so the
world looks smaller and more of it fits in view. This is deliberate angular
minification, not additional headset FOV, and may be less comfortable. The
130-degree/0.75 and 150-degree/0.40 checkpoints are Quest-tested; other lower
scales and source FOVs above 130 degrees are experimental. The captured application must actually render
the declared source FOV because this
bounded probe sends no coordination message to Minecraft. Projection calibration
is frozen once and never changes while running. If the source cannot cover the
headset frustum, requested roll range, and view scale, the probe reports the
required FOV and a conservative supported roll value instead of auto-clamping or
introducing zoom breathing.
This is a head-following 3DoF presentation: physical translation does not create
new scene parallax. Do not run this probe beside any other focused OpenXR app.
The explicit 15-degree value is the conservative Quest/SteamVR hardware result;
the bounded probe retains its earlier 20-degree default, which may not fit this
headset. Treat the command as a checkpoint, not a guarantee for other runtimes.

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

### Unified OpenXR display/input bridge

`MCXRInputOpenXRBridge.exe` now owns physical HMD/controller input and optional
Minecraft display in one OpenXR instance, session, and D3D11 device. With no
window selector (or only `--port`), it preserves the earlier controls-only dark
session and protocol-v2 UDP behavior:

```bat
bridge\native\build\Release\MCXRInputOpenXRBridge.exe
```

To use a development port that matches `-Dmcxrinput.port=...`, pass `--port`:

```bat
bridge\native\build\Release\MCXRInputOpenXRBridge.exe --port 28772
```

Exactly one `--executable` or `--window` selector enables live Windows capture,
immersive per-eye projection, HMD/controller actions, and protocol-v2 loopback
output in that same process. Start visible borderless Minecraft with ReShade
already producing an even-width half-SBS image, then start SteamVR with the
headset awake and run:

```powershell
.\bridge\native\build\Release\MCXRInputOpenXRBridge.exe `
  --executable "C:\Users\wenyu\AppData\Roaming\ElyPrismLauncher\java\java-runtime-epsilon\bin\javaw.exe" `
  --port 28771 `
  --fit cover `
  --source-vfov-deg 110 `
  --world-view-scale 1 `
  --roll-coverage-deg 15 `
  --menu-width-m 1.6 `
  --menu-distance-m 1.5 `
  --eye-order lr
```

The next wider-view hardware checkpoint is opt-in:

```powershell
.\bridge\native\build\Release\MCXRInputOpenXRBridge.exe `
  --executable "C:\Users\wenyu\AppData\Roaming\ElyPrismLauncher\java\java-runtime-epsilon\bin\javaw.exe" `
  --port 28771 `
  --fit cover `
  --source-vfov-deg 130 `
  --world-view-scale 0.75 `
  --roll-coverage-deg 15 `
  --menu-width-m 1.6 `
  --menu-distance-m 1.5 `
  --eye-order lr
```

That 130-degree/0.75 combination and the complete-hotbar safe area are now
Quest-tested. More aggressive values should be tested as a ladder, changing
both options together:

| Source VFOV | World-view scale | Status |
| ---: | ---: | :--- |
| 140 | 0.60 | Experimental step |
| 145 | 0.50 | Experimental step |
| 150 | 0.40 | Quest-tested |
| 155 | 0.35 | Experimental step |
| 160 | 0.30 | Experimental limit |

Use the first row that gives enough coverage. Each pair retains source-FOV
headroom for the measured Quest/SteamVR 15-degree roll envelope. Using a much
higher source FOV than a scale requires wastes captured pixel density, while an
insufficient source FOV is rejected instead of stretched or silently clamped.

Use `--list-windows --executable "C:\path\to\javaw.exe"` without starting
OpenXR or UDP. If more than one visible window matches, select the printed
hexadecimal handle with `--window 0x...`. The real bridge runs until Ctrl+C and
does not accept the probes' bounded `--seconds` option. `--source-vfov-deg`
accepts `30..160`, declares the captured rectilinear source, and—only while a
fresh unified-display offer is active in a world—temporarily locks Minecraft's
effective rendered FOV to that exact value. It does not rewrite the user's FOV
option. Raising that value alone does not reveal more world under calibrated
`cover`; it supplies the extra source rays needed by `--world-view-scale`.

`--world-view-scale` accepts `0.30..1` and defaults to `1`. Values below `1`
expand the source rays sampled for each physical eye in tangent space while the
OpenXR projection frustum remains fixed. This intentionally compresses angular
motion and perspective, so return to `1` if uncomfortable. Except for the
Quest-tested 150-degree/0.40 pair, values below `0.75` and source FOVs above 130
degrees are unvalidated experimental comparisons; use the staged table above
rather than jumping directly to the limit. Do not treat
the scale as true additional headset FOV. At extreme FOVs, verify peripheral
chunk/entity culling, ReShade/shader output, and water/lava/powder-snow overlays;
universal render-mod compatibility is not claimed. `cover` preserves rectilinear
aspect; `stretch` keeps the complete source only by deliberate distortion and
therefore rejects a non-default world view scale. `rl` is only for reversed
source-eye routing. `--menu-width-m` and `--menu-distance-m` tune only the finite
comfort screen. Unsupported FOV/roll/scale combinations fail with a diagnostic
rather than being auto-clamped.

Press `F8` to enable VR input for the current world, then press `R` to reset the
HMD reference. The Minecraft camera and immersive capture should follow headset
yaw and pitch; head roll remains gravity-level because ordinary Minecraft camera
control has no roll axis. Never run a probe and the bridge together: one process
must exclusively own OpenXR focus.

Display mode also uses a separate loopback-only `MCXRD1` presentation handshake;
the additive CALIBRATION heartbeat carries the exact native world-view scale,
while protocol-v2 gameplay input remains independently gated. The bridge begins on the proven finite
screen. A fresh `WORLD` acknowledgement of the exact temporary FOV selects the
roll-stabilized immersive projection. Opening any Minecraft screen or overlay, or
leaving a world, selects two eye-specific finite quads rendered with uncropped
`contain` fit so the whole GUI and hotbar are visible. Each state change waits for
a capture newer than the acknowledgement before switching, preventing an old
world/menu frame from flashing in the new mode. In the immersive world view,
the vanilla bottom-center hotbar/status cluster is uniformly reduced only as much
as needed for the hotbar's two outer slots and offhand extent to fit the automatic
physical-view safe area at aligned roll, with conservative roll margins. If offers or
replies become stale, Minecraft restores its normal FOV/HUD behavior and the
bridge falls back to the finite screen.

### Tracked cosmetic-avatar checkpoint

The validated grip-marker milestone remains available, and the next default-off
checkpoint now renders the local skin's wide/slim two-segment arms, optional
sleeve layers, and the item Minecraft assigns to each physical hand. It also
renders a headless neutral torso, jacket, legs, and independently enabled pants
layers below the camera. The elbows use deterministic two-bone IK. Shoulders and
the body anchor stay gravity-stable while nodding, and the complete geometry uses
the exact native world-view-scale calibration. Wide and slim skins share
Minecraft's ordinary torso/leg dimensions while retaining their different arm
widths. For the `150/0.40` checkpoint, the upright torso top begins ten
centimetres above the unchanged tracked-shoulder midpoint. Its center is set
back far enough that even the inflated jacket front remains 1.5625 centimetres
behind that shoulder plane from top through hip. Both leg centers now share the
accepted torso center plane instead of using a separate forward placement. The
hips begin five centimetres inside the torso so every corner stays attached at
every point in the bounded walk cycle. Their centers stay aligned with the torso
while a one-percent depth-only inset prevents the rotating pants layer from
crossing the jacket. Torso, base legs, jacket, and pants use their complete
six-face cuboids; the earlier cropped-top shelf workaround is gone. Neutral feet
remain on Minecraft's standing ground plane.

The legs sample only the local player's ordinary interpolated Minecraft walk
animation position and speed during render extraction. They swing in opposite
phases around fixed gravity-stable hips, scale smoothly with actual rendered
movement speed, and stop at a conservative twenty-degree maximum. This is
cosmetic snapshot state only: it does not read controller buttons, hold movement
keys, change player motion, or affect collision, reach, input, or packets. None
of this changes the working shoulders, arms, or held items.

Held items are rigid cosmetic models at the OpenXR grips. This checkpoint does
not apply vanilla first-person swing/use transforms or animate the arms. It uses
ownerless item-model resolution so live attack/use predicates cannot drive the
model; ordinary stack-driven variants may still differ by item state. It does
not change reach, item state, input, or serverbound behavior; the existing
vanilla key mappings still perform gameplay independently.

In the dedicated singleplayer Minecraft profile, replace the marker option with
this JVM option and install the newly built mod JAR:

```text
-Dmcxrinput.development.trackedAvatar=true
```

Run the unified native bridge normally. For the current Quest-tested projection,
use `--source-vfov-deg 150 --world-view-scale 0.40`. First test empty hands, then
different items in each hand, arm crossing/extension/rotation, HMD nodding, an
asymmetric skin, the slim model, sleeve toggles, and Minecraft's left-handed
main-arm setting. Blocks should occlude the arms, no vanilla hands should be
doubled, and attack/use should not animate this cosmetic rig.

Hardware-check the aligned body at `150/0.40` while looking down, nodding,
turning, crouching, walking forward/backward, strafing, sprinting, opening
screens, and briefly losing tracking. Stationary aligned legs naturally enter
the source view only during a deeper look-down; a forward walking leg enters
earlier. Confirm smooth start/stop easing, opposite speed-scaled swing, fixed
hips, and no exposed hip cap, detached leg, hollow/inside view, exaggerated
foot lift, visible head, duplicate arm, or near-plane artifact. Minecraft's
interpolated crouch height still shortens only hip-to-foot reach, and disabled
jacket/pants layers must remain absent.

Each tracked hand disappears independently on invalid grip tracking or an
implausible/near-camera pose. A fixed-length arm may clamp at most five
centimetres at full extension, with its cosmetic item moved to the solved wrist;
larger overreach hides that hand instead of stretching it. The complete
checkpoint restores vanilla hands on stale UDP, a screen/overlay, third person,
disconnect, unsupported player poses, or loss of fresh display calibration. It
is disabled in multiplayer and controls-only mode, default off, depth tested,
and independent of the `F8` gameplay-input toggle.

An unexpected late render-submission failure is rate-limited in the log and may
omit the affected cosmetic body part or hand for that exact frame; vanilla hands
remain suppressed for that frame so partial tracked geometry is never doubled.

The older cube/axis diagnostic remains available with
`-Dmcxrinput.development.trackedHandMarkers=true`. If both properties are set,
markers take precedence and the tracked avatar stays disabled. This body
placement checkpoint targets `150/0.40`; remove both properties for normal use.
Do not distribute a development-renderer profile. Heads, armor, other body/item
animations, and remote-player avatars remain deferred until this tracked avatar
is hardware-validated.

The publication path is fail-closed. Controls-only input requires a running,
focused session, a runtime-requested and accepted compositor frame, tracked HMD
orientation, successful action sync, and a plausible quaternion. Display mode
also requires tracked HMD position and a capture no older than 250 ms that was
rendered into the accepted frame. Focus/tracking loss, `shouldRender=false`,
minimize/resize/invalid half-SBS capture, stale capture, action-sync failure, or
a failed frame publishes an inactive HMD with both controllers neutral. An
incomplete controller query neutralizes the frame and is treated as a terminal
OpenXR input failure. Five seconds of live capture starvation, a closed selected
window, an aspect-changing capture resize, or a terminal projection/render/session
failure exits nonzero. Same-aspect capture recovery remains allowed. Startup,
Ctrl+C, and teardown force neutral v2 datagrams;
the Fabric receiver's own stale timeout remains a fallback if UDP itself fails.

This is a roll-stabilized 3DoF projection of Minecraft's existing desktop stereo
image, not rendered scene geometry. Head translation creates no positional
parallax. Automatic screen switching and HUD/FOV coordination are presentation
changes only; they neither create gameplay actions nor change protocol-v2 input.
Adding local capture/display does not change server policy: MCXRInput remains
presumed disallowed on Hypixel and must not be used there.

Default controller mapping is intentionally conservative and can be changed in
the Mod Menu settings or `config/mcxrinput.json`:

- Left stick maps to vanilla forward/back/left/right key mappings.
- One right-stick left/right gesture selects one previous/next hotbar slot. The
  stick must return through the deadzone before another selection.
- Right `A` maps to jump.
- Right `B` maps to sneak.
- Left stick click maps to sprint when SteamVR exposes it.
- Right trigger maps to vanilla attack/destroy.
- Left trigger maps to vanilla use/place.
- Left `Y` opens the vanilla inventory.
- Hold right stick click and point the right stick. Releasing on up opens pause,
  left opens chat, and right cycles perspective. Pointing down shows the vanilla
  player list only while the same R3 press remains physically held; releasing
  closes it. Releasing without selecting cancels.
- Controller input releases while screens/overlays are open or if bridge input
  goes stale.

The utility wheel is a non-pausing, client-only cosmetic overlay. It blocks
gameplay controls while held but keeps HMD camera tracking active. Its selection
threshold is the larger of the configured controller deadzone and `0.55`; the
last valid direction stays selected if the stick returns to center before the
wheel control is released. The player-list key is set once on entry and released
when the physical hold ends; it is not reasserted every tick. Stale bridge input,
F8 disable, disconnects, and screen transitions cancel the wheel and release the
player-list key without acting.

While a Minecraft screen is open, the configured menu thumbstick behaves like
the keyboard arrow keys: one dominant direction at a time, followed by a
controlled key-like repeat while held. Right `A` confirms and right `B` goes
back by default. This uses Minecraft's native focus navigation rather than a
virtual mouse, so ordinary menus and focusable mod screens work; inventory slot
interaction uses the dedicated snapped-slot behavior below.

Inside container screens, each neutral-to-deflected menu-navigation gesture snaps
the ordinary Minecraft cursor between active slots. Defaults follow the same
controller conventions used by Controlify:

- Right `A` picks up or places a stack using vanilla left-click behavior.
- Left `Y` quick-moves a slot using vanilla shift-click behavior.
- Left `X` takes half a stack or places one item using vanilla right-click behavior.
- While carrying a stack, left `Y` drops it outside the container instead of
  quick-moving a slot.
- The configured menu-back control closes the container normally.
- One right-stick gesture emits one ordinary mouse-wheel event for a scrollable
  container; holding it does not repeat.

Creative inventory support includes Fabric/mod-added tabs from the current tab
page. D-pad navigation can snap from slots onto visible tab buttons, and `A`
selects the highlighted tab. Right grip cycles to the next tab and left grip to
the previous tab by default, wrapping across Fabric Creative-tab pages. These
bindings and the scroll stick are configurable on the Inventory Bindings page.
Because scrolling enters through Minecraft's real mouse-wheel handler, Creative
tabs and modded container screens that override normal wheel behavior receive
the same event shape as a physical mouse wheel.

All inventory actions occur only once per fresh physical press and call the
container screen's normal `slotClicked` path with vanilla `ContainerInput`
values. The entire snapped inventory controller is disabled by default on remote
multiplayer and published LAN worlds. An explicit config opt-in exists for servers
that permit it; that opt-in is not evidence of server permission. MCXRInput does not
construct container packets.

Hotbar selection changes only Minecraft's local selected-slot state, just like
the vanilla number keys or mouse wheel; Minecraft performs its normal server
sync. The stick must return through the deadzone after tracking loss or VR input
is re-enabled before it can select again.

Trigger pulls use the configured threshold and a small release hysteresis. A
trigger held through a menu, F8 disable, stale frame, or tracking loss must be
released before it can act again. Right-stick turning is deferred. Ordinary
menus remain native-focus/D-pad driven; only inventory-style
container screens use a cursor, and it snaps directly between valid slots.

Minecraft 26.2's Toggle Crouch, Toggle Sprint, Toggle Attack, and Toggle Use
settings cannot safely share these controller holds. MCXRInput fails closed for
the affected action while its vanilla Toggle option is enabled and displays a
message on each fresh rejected press. Click-required mappings also fail closed
when unbound, scan-code-only, or conflicting with another Minecraft key mapping.
Duplicate controller bindings across gameplay actions fail closed, and hotbar
selection is suppressed if configured to the same stick as movement.

## Mechanism and policy boundary

MCXRInput changes the local player's normal yaw/pitch fields and selected hotbar
slot, transitions existing Minecraft key mappings, and invokes the normal
container-screen click path for a physical inventory press. Vanilla decides when
to send its normal movement, rotation, selected-item, and container updates.
MCXRInput contains no custom serverbound gameplay packet code. This narrowly
describes implementation mechanism; it does not make the mod policy-equivalent
to an unmodified client or permitted by a server.

MCXRInput deliberately removes timed gameplay repeat, rejects production v1 test
poses, and releases owned input on stale tracking, screens, F8, disconnect, or
world changes. Bugs and server-specific policy risk can still remain. Test in
singleplayer and do not use this profile on Hypixel.

See [docs/bridge-protocol.md](docs/bridge-protocol.md) for the wire format.
