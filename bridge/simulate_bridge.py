#!/usr/bin/env python3
"""Dependency-free Phase 1 bridge simulator for validating the local protocol."""

from __future__ import annotations

import argparse
import json
import math
import socket
import time


def quaternion_from_yaw_pitch(yaw_degrees: float, pitch_degrees: float) -> list[float]:
    """Return an OpenXR-style quaternion using Y yaw followed by X pitch."""
    yaw = math.radians(yaw_degrees) * 0.5
    pitch = math.radians(pitch_degrees) * 0.5
    return [
        math.sin(pitch) * math.cos(yaw),
        math.cos(pitch) * math.sin(yaw),
        -math.sin(pitch) * math.sin(yaw),
        math.cos(pitch) * math.cos(yaw),
    ]


def main() -> None:
    parser = argparse.ArgumentParser(description="Send test HMD poses to MCXRInput")
    parser.add_argument("--port", type=int, default=28771)
    parser.add_argument("--yaw", type=float, default=0.0, help="OpenXR yaw in degrees; positive turns left")
    parser.add_argument("--pitch", type=float, default=0.0)
    parser.add_argument("--sweep", action="store_true", help="gently sweep yaw until interrupted")
    args = parser.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
        started = time.monotonic()
        while True:
            yaw = args.yaw
            if args.sweep:
                yaw += math.sin((time.monotonic() - started) * 0.7) * 35.0
            message = {
                "version": 1,
                "timestamp": time.time_ns(),
                "hmd": {
                    "rotation": quaternion_from_yaw_pitch(yaw, args.pitch),
                    "active": True,
                },
            }
            sender.sendto(json.dumps(message, separators=(",", ":")).encode(), ("127.0.0.1", args.port))
            if not args.sweep:
                return
            time.sleep(1.0 / 90.0)


if __name__ == "__main__":
    main()
