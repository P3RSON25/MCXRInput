"""Development-only synthetic pose sender for singleplayer testing.

This tool emits protocol-v1 test poses. It does not read an OpenXR headset.
"""

from __future__ import annotations

import json
import math
import os
import socket
import sys
import threading
import time

if getattr(sys, "frozen", False):
    bundle_root = getattr(sys, "_MEIPASS")
    os.environ.setdefault("TCL_LIBRARY", os.path.join(bundle_root, "_tcl_data"))
    os.environ.setdefault("TK_LIBRARY", os.path.join(bundle_root, "_tk_data"))

import tkinter as tk
from tkinter import messagebox, ttk


DEFAULT_PORT = 28771
SEND_RATE_HZ = 90.0


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


class BridgeWindow:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.stop_event = threading.Event()
        self.sender_thread: threading.Thread | None = None
        self.started_at = 0.0

        root.title("MCXRInput Synthetic Pose Test Tool - SINGLEPLAYER ONLY")
        root.resizable(False, False)
        root.protocol("WM_DELETE_WINDOW", self.close)

        self.port = tk.StringVar(value=str(DEFAULT_PORT))
        self.yaw = tk.DoubleVar(value=0.0)
        self.pitch = tk.DoubleVar(value=0.0)
        self.sweep = tk.BooleanVar(value=False)
        self.status = tk.StringVar(value="Stopped")
        self.current_yaw = 0.0
        self.current_pitch = 0.0
        self.current_sweep = False
        self.yaw.trace_add("write", self.update_controls)
        self.pitch.trace_add("write", self.update_controls)
        self.sweep.trace_add("write", self.update_controls)

        panel = ttk.Frame(root, padding=16)
        panel.grid(sticky="nsew")

        ttk.Label(
            panel,
            text="DEVELOPMENT / TEST / SINGLEPLAYER ONLY",
            foreground="#9b1c1c",
        ).grid(row=0, column=0, columnspan=2, pady=(0, 6))
        ttk.Label(
            panel,
            text="Sends synthetic protocol-v1 poses. This tool does not read a headset.",
            wraplength=430,
            justify="center",
        ).grid(row=1, column=0, columnspan=2, pady=(0, 4))
        ttk.Label(
            panel,
            text="Requires JVM option: -Dmcxrinput.development.allowProtocolV1TestPoses=true",
            wraplength=430,
            justify="center",
        ).grid(row=2, column=0, columnspan=2, pady=(0, 14))

        ttk.Label(panel, text="Local UDP port").grid(row=3, column=0, sticky="w")
        self.port_entry = ttk.Entry(panel, textvariable=self.port, width=10)
        self.port_entry.grid(row=3, column=1, sticky="e", pady=(0, 10))

        ttk.Label(panel, text="Synthetic yaw").grid(row=4, column=0, sticky="w")
        ttk.Scale(panel, from_=-180, to=180, variable=self.yaw, length=320).grid(
            row=5, column=0, columnspan=2
        )
        ttk.Label(panel, textvariable=self.yaw, width=8).grid(row=4, column=1, sticky="e")

        ttk.Label(panel, text="Synthetic pitch").grid(row=6, column=0, sticky="w", pady=(10, 0))
        ttk.Scale(panel, from_=-90, to=90, variable=self.pitch, length=320).grid(
            row=7, column=0, columnspan=2
        )
        ttk.Label(panel, textvariable=self.pitch, width=8).grid(row=6, column=1, sticky="e", pady=(10, 0))

        self.sweep_check = ttk.Checkbutton(
            panel,
            text="Synthetic yaw sweep (DEVELOPMENT TEST; off by default)",
            variable=self.sweep,
        )
        self.sweep_check.grid(row=8, column=0, columnspan=2, sticky="w", pady=(12, 8))

        buttons = ttk.Frame(panel)
        buttons.grid(row=9, column=0, columnspan=2, sticky="ew")
        self.start_button = ttk.Button(buttons, text="Start synthetic v1 test", command=self.start)
        self.start_button.pack(side="left", expand=True, fill="x", padx=(0, 4))
        self.stop_button = ttk.Button(buttons, text="Stop", command=self.stop, state="disabled")
        self.stop_button.pack(side="left", expand=True, fill="x", padx=(4, 0))

        ttk.Separator(panel).grid(row=10, column=0, columnspan=2, sticky="ew", pady=12)
        ttk.Label(panel, text="Status:").grid(row=11, column=0, sticky="w")
        ttk.Label(panel, textvariable=self.status).grid(row=11, column=1, sticky="e")
        ttk.Label(
            panel,
            text="Synthetic test sender only - never use for multiplayer gameplay.",
            foreground="#555555",
        ).grid(row=12, column=0, columnspan=2, pady=(12, 0))

    def start(self) -> None:
        try:
            port = int(self.port.get())
            if not 1024 <= port <= 65535:
                raise ValueError
        except ValueError:
            messagebox.showerror("Invalid port", "Enter a port between 1024 and 65535.")
            return

        if not messagebox.askokcancel(
            "Singleplayer development test",
            "This tool sends synthetic protocol-v1 poses and does not read a headset.\n\n"
            "Continue only in a dedicated singleplayer development profile?",
        ):
            return

        self.started_at = time.monotonic()
        self.stop_event.clear()
        self.sender_thread = threading.Thread(target=self.send_loop, args=(port,), daemon=True)
        self.sender_thread.start()
        self.port_entry.configure(state="disabled")
        self.start_button.configure(state="disabled")
        self.stop_button.configure(state="normal")
        self.status.set(f"Synthetic v1 -> 127.0.0.1:{port}")

    def update_controls(self, *_args: object) -> None:
        """Copy Tk state on the UI thread for the sender thread to read safely."""
        self.current_yaw = self.yaw.get()
        self.current_pitch = self.pitch.get()
        self.current_sweep = self.sweep.get()

    def stop(self) -> None:
        self.stop_event.set()
        self.sender_thread = None
        self.port_entry.configure(state="normal")
        self.start_button.configure(state="normal")
        self.stop_button.configure(state="disabled")
        self.status.set("Stopped")

    def send_loop(self, port: int) -> None:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                while not self.stop_event.is_set():
                    yaw = self.current_yaw
                    if self.current_sweep:
                        yaw += math.sin((time.monotonic() - self.started_at) * 0.7) * 35.0
                    message = {
                        "version": 1,
                        "timestamp": time.time_ns(),
                        "hmd": {
                            "rotation": quaternion_from_yaw_pitch(yaw, self.current_pitch),
                            "active": True,
                        },
                    }
                    sender.sendto(
                        json.dumps(message, separators=(",", ":")).encode(),
                        ("127.0.0.1", port),
                    )
                    self.stop_event.wait(1.0 / SEND_RATE_HZ)
        except OSError as error:
            self.root.after(0, self.show_send_error, str(error))

    def show_send_error(self, error: str) -> None:
        self.stop()
        messagebox.showerror("Synthetic test sender error", error)

    def close(self) -> None:
        self.stop_event.set()
        self.root.destroy()


def main() -> None:
    root = tk.Tk()
    BridgeWindow(root)
    root.mainloop()


if __name__ == "__main__":
    main()
