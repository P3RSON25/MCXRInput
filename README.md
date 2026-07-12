# MCXRInput

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
- Movement-only HMD orientation deltas added to the current player yaw/pitch
- Optional `config/mcxrinput.json` settings and controller remapping, with a
  Mod Menu config screen when Mod Menu is installed
- Controller movement and physical attack/use triggers through ordinary
  Minecraft key mappings
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
- No armswinger or custom gameplay packets; three isolated accessor mixins expose
  Minecraft's existing container-screen, Creative-tab, and mouse-position methods

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
  "configVersion": 7,
  "hmdYawSensitivity": 1.0,
  "hmdPitchSensitivity": 1.0,
  "controllerDeadzone": 0.35,
  "triggerThreshold": 0.55,
  "allowInventoryInputInMultiplayer": false,
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

The default and maximum HMD sensitivity is 1:1; v7 conservatively clamps older
values above `1.0` instead of amplifying physical head movement. If Mod Menu is
installed, MCXRInput exposes a config button there that edits the same file,
including separate gameplay, menu, inventory, and utility binding pages. Each
binding button cycles through the physical OpenXR controls; `Unbound` disables
that action. Older configs migrate to v7 while preserving bindings and in-range
numeric values; yaw/pitch sensitivities above `1.0` are intentionally reduced to
`1.0`. Mod Menu is optional and is not required to run MCXRInput.

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
before connecting the proven Minecraft half-SBS capture path to OpenXR.

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

Press `F8` to enable VR input for the current world, then press `R` to reset the
HMD reference. The desktop Minecraft camera should follow headset yaw and pitch.
Head roll is intentionally stabilized away because
ordinary Minecraft camera control has no roll axis. The headset may show a blank
dark MCXRInput app while SteamVR focuses the bridge; in-headset display of
Minecraft is a later rendering/viewing milestone.

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
