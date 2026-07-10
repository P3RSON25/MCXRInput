package dev.mcxrinput.input;

import dev.mcxrinput.protocol.VrControllerState;

/** Stable, config-file-facing names for physical OpenXR controller controls. */
public enum ControllerButton {
	NONE("none", Hand.NONE, Control.NONE),
	RIGHT_A("right_a", Hand.RIGHT, Control.A),
	RIGHT_B("right_b", Hand.RIGHT, Control.B),
	LEFT_X("left_x", Hand.LEFT, Control.X),
	LEFT_Y("left_y", Hand.LEFT, Control.Y),
	RIGHT_TRIGGER("right_trigger", Hand.RIGHT, Control.TRIGGER),
	LEFT_TRIGGER("left_trigger", Hand.LEFT, Control.TRIGGER),
	RIGHT_GRIP("right_grip", Hand.RIGHT, Control.SQUEEZE),
	LEFT_GRIP("left_grip", Hand.LEFT, Control.SQUEEZE),
	RIGHT_STICK_CLICK("right_stick_click", Hand.RIGHT, Control.STICK_CLICK),
	LEFT_STICK_CLICK("left_stick_click", Hand.LEFT, Control.STICK_CLICK),
	LEFT_MENU("left_menu", Hand.LEFT, Control.MENU),
	RIGHT_MENU("right_menu", Hand.RIGHT, Control.MENU),
	LEFT_A("left_a", Hand.LEFT, Control.A),
	LEFT_B("left_b", Hand.LEFT, Control.B),
	RIGHT_X("right_x", Hand.RIGHT, Control.X),
	RIGHT_Y("right_y", Hand.RIGHT, Control.Y);

	private final String id;
	private final Hand hand;
	private final Control control;

	ControllerButton(String id, Hand hand, Control control) {
		this.id = id;
		this.hand = hand;
		this.control = control;
	}

	public String id() {
		return id;
	}

	public String translationKey() {
		return "controller.mcxrinput." + id;
	}

	public ControllerButton next() {
		ControllerButton[] buttons = values();
		return buttons[(ordinal() + 1) % buttons.length];
	}

	public Sample sample(VrControllerState left, VrControllerState right) {
		if (hand == Hand.NONE) {
			return Sample.INACTIVE;
		}

		VrControllerState state = hand == Hand.LEFT ? left : right;
		if (!state.active()) {
			return Sample.INACTIVE;
		}

		float value = switch (control) {
			case TRIGGER -> state.trigger();
			case SQUEEZE -> state.squeeze();
			case STICK_CLICK -> state.stickClick() ? 1.0F : 0.0F;
			case A -> state.a() ? 1.0F : 0.0F;
			case B -> state.b() ? 1.0F : 0.0F;
			case X -> state.x() ? 1.0F : 0.0F;
			case Y -> state.y() ? 1.0F : 0.0F;
			case MENU -> state.menu() ? 1.0F : 0.0F;
			case NONE -> 0.0F;
		};
		return new Sample(true, value);
	}

	public static ControllerButton fromId(String id, ControllerButton fallback) {
		if (id != null) {
			for (ControllerButton button : values()) {
				if (button.id.equals(id)) {
					return button;
				}
			}
		}
		return fallback;
	}

	private enum Hand {
		NONE,
		LEFT,
		RIGHT
	}

	private enum Control {
		NONE,
		TRIGGER,
		SQUEEZE,
		STICK_CLICK,
		A,
		B,
		X,
		Y,
		MENU
	}

	public record Sample(boolean active, float value) {
		private static final Sample INACTIVE = new Sample(false, 0.0F);
	}
}
