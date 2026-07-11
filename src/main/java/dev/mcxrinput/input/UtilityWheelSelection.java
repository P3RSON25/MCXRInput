package dev.mcxrinput.input;

import java.util.Optional;

/** Pure radial selection for the four fixed utility-wheel actions. */
public final class UtilityWheelSelection {
	private UtilityWheelSelection() {
	}

	/**
	 * Selects the action in the dominant direction of a normalized right stick.
	 * Positive Y points up, matching the OpenXR controller state used by the mod.
	 */
	public static Optional<Action> select(float stickX, float stickY, float activationThreshold) {
		if (!Float.isFinite(stickX) || !Float.isFinite(stickY)
				|| !Float.isFinite(activationThreshold)
				|| activationThreshold < 0.0F || activationThreshold > 1.0F) {
			return Optional.empty();
		}

		double magnitudeSquared = (double) stickX * stickX + (double) stickY * stickY;
		double thresholdSquared = (double) activationThreshold * activationThreshold;
		if (magnitudeSquared <= thresholdSquared) {
			return Optional.empty();
		}

		if (Math.abs(stickY) >= Math.abs(stickX)) {
			return Optional.of(stickY > 0.0F ? Action.PAUSE : Action.PLAYER_LIST);
		}
		return Optional.of(stickX > 0.0F ? Action.PERSPECTIVE : Action.CHAT);
	}

	public enum Action {
		PAUSE,
		CHAT,
		PLAYER_LIST,
		PERSPECTIVE
	}
}
