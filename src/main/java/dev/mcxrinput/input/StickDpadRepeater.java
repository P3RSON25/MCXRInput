package dev.mcxrinput.input;

/**
 * Converts a thumbstick into D-pad presses with key-like repeat.
 * This is restricted to ordinary non-gameplay screen-focus navigation.
 */
public final class StickDpadRepeater {
	private final int initialRepeatDelayTicks;
	private final int repeatIntervalTicks;
	private boolean armed;
	private Direction heldDirection;
	private int heldTicks;

	public StickDpadRepeater(int initialRepeatDelayTicks, int repeatIntervalTicks) {
		if (initialRepeatDelayTicks < 1 || repeatIntervalTicks < 1) {
			throw new IllegalArgumentException("repeat timings must be positive");
		}
		this.initialRepeatDelayTicks = initialRepeatDelayTicks;
		this.repeatIntervalTicks = repeatIntervalTicks;
	}

	public Direction update(boolean active, float x, float y, float pressThreshold) {
		if (!active || !Float.isFinite(x) || !Float.isFinite(y)
				|| !Float.isFinite(pressThreshold) || pressThreshold <= 0.0F) {
			suppress();
			return null;
		}

		float releaseThreshold = pressThreshold * 0.75F;
		if (!armed) {
			if (direction(x, y, releaseThreshold) == null) {
				armed = true;
			}
			return null;
		}

		float threshold = heldDirection == null ? pressThreshold : releaseThreshold;
		Direction direction = direction(x, y, threshold);
		if (direction == null) {
			heldDirection = null;
			heldTicks = 0;
			return null;
		}

		if (direction != heldDirection) {
			heldDirection = direction;
			heldTicks = 0;
			return direction;
		}

		heldTicks++;
		if (heldTicks >= initialRepeatDelayTicks
				&& (heldTicks - initialRepeatDelayTicks) % repeatIntervalTicks == 0) {
			return direction;
		}
		return null;
	}

	public void suppress() {
		armed = false;
		heldDirection = null;
		heldTicks = 0;
	}

	private static Direction direction(float x, float y, float threshold) {
		float absoluteX = Math.abs(x);
		float absoluteY = Math.abs(y);
		if (absoluteX < threshold && absoluteY < threshold) {
			return null;
		}
		if (absoluteY >= absoluteX) {
			return y >= 0.0F ? Direction.UP : Direction.DOWN;
		}
		return x >= 0.0F ? Direction.RIGHT : Direction.LEFT;
	}

	public enum Direction {
		UP,
		DOWN,
		LEFT,
		RIGHT
	}
}
