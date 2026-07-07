# MCXRInput local bridge protocol

The local bridge transport is one UTF-8 JSON object per UDP datagram, sent to
`127.0.0.1:28771`. The mod binds only to that address. A datagram is limited to
4095 bytes; larger, malformed, unsupported-version, and non-finite HMD poses are
ignored.

## v1 HMD-only message

v1 remains accepted for the simulator and older test tools.

```json
{
  "version": 1,
  "timestamp": 1783270000000000000,
  "hmd": {
    "rotation": [0.0, 0.0, 0.0, 1.0],
    "active": true
  }
}
```

- `rotation` is the OpenXR quaternion `[x, y, z, w]` in the usual right-handed
  coordinate system where identity looks along `-Z` and `+Y` is up.
- `active` should be false when the headset is not worn or tracking is invalid.
  It defaults to true only for compatibility with the earliest test messages.
- `timestamp` is informational. The mod uses its local monotonic receive time
  for the 250 ms freshness cutoff.
- Send at the OpenXR frame rate. The current prototype consumes the most recent
  pose once per Minecraft client tick.

## v2 HMD and controller message

v2 adds optional controller state. Controller fields are physical user input
only; the mod may translate them into ordinary Minecraft key mappings, but must
not generate autonomous, repeated, or timed gameplay actions from them.

```json
{
  "version": 2,
  "timestamp": 1783270000000000000,
  "hmd": {
    "rotation": [0.0, 0.0, 0.0, 1.0],
    "active": true
  },
  "controllers": {
    "left": {
      "active": true,
      "stick": [0.0, 0.0],
      "trigger": 0.0,
      "squeeze": 0.0,
      "stickClick": false,
      "a": false,
      "b": false,
      "x": false,
      "y": false,
      "menu": false
    },
    "right": {
      "active": true,
      "stick": [0.0, 0.0],
      "trigger": 0.0,
      "squeeze": 0.0,
      "stickClick": false,
      "a": false,
      "b": false,
      "x": false,
      "y": false,
      "menu": false
    }
  }
}
```

- `stick` is an OpenXR thumbstick vector clamped by the receiver to `[-1, 1]`.
- `trigger` and `squeeze` are clamped by the receiver to `[0, 1]`.
- Missing, inactive, or malformed controller blocks become inactive for that
  hand only. A malformed HMD pose still rejects the whole datagram.

The port can be changed for development with JVM option
`-Dmcxrinput.port=28772`. The receiver never accepts a non-loopback address.
