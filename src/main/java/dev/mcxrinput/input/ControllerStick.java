package dev.mcxrinput.input;

import dev.mcxrinput.protocol.VrControllerState;

/** A stable config value selecting one of the two OpenXR thumbsticks. */
public enum ControllerStick {
	LEFT("left"),
	RIGHT("right");

	private final String id;

	ControllerStick(String id) {
		this.id = id;
	}

	public String id() {
		return id;
	}

	public String translationKey() {
		return "controller.mcxrinput." + id + "_stick";
	}

	public ControllerStick next() {
		return this == LEFT ? RIGHT : LEFT;
	}

	public VrControllerState select(VrControllerState left, VrControllerState right) {
		return this == LEFT ? left : right;
	}

	public static ControllerStick fromId(String id, ControllerStick fallback) {
		if (id != null) {
			for (ControllerStick stick : values()) {
				if (stick.id.equals(id)) {
					return stick;
				}
			}
		}
		return fallback;
	}
}
