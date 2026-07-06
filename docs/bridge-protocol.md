# MCXRInput local bridge protocol v1

The Phase 1 transport is one UTF-8 JSON object per UDP datagram, sent to
`127.0.0.1:28771`. The mod binds only to that address. A datagram is limited to
4095 bytes; larger, malformed, non-v1, and non-finite poses are ignored.

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

The port can be changed for development with JVM option
`-Dmcxrinput.port=28772`. The receiver never accepts a non-loopback address.
