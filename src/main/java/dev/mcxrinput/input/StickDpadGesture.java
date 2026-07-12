package dev.mcxrinput.input;

/**
 * Converts a thumbstick deflection into one D-pad action, then waits for the
 * stick to return through the deadzone before another action can occur.
 *
 * <p>This deliberately has no time-based repeat. Calling {@link #suppress()}
 * after stale or blocked input also requires a neutral sample before re-arming,
 * so an input held across a tracking or screen transition cannot fire later.</p>
 */
public final class StickDpadGesture {
	private boolean armed;
	private boolean waitingForNeutral;

	public StickDpadRepeater.Direction update(
			boolean active,
			float x,
			float y,
			float pressThreshold
	) {
		if (!active || !Float.isFinite(x) || !Float.isFinite(y)
				|| !Float.isFinite(pressThreshold) || pressThreshold <= 0.0F) {
			suppress();
			return null;
		}

		float releaseThreshold = pressThreshold * 0.75F;
		if (!armed || waitingForNeutral) {
			if (direction(x, y, releaseThreshold) == null) {
				armed = true;
				waitingForNeutral = false;
			}
			return null;
		}

		StickDpadRepeater.Direction direction = direction(x, y, pressThreshold);
		if (direction != null) {
			waitingForNeutral = true;
		}
		return direction;
	}

	public void suppress() {
		armed = false;
		waitingForNeutral = false;
	}

	private static StickDpadRepeater.Direction direction(float x, float y, float threshold) {
		float absoluteX = Math.abs(x);
		float absoluteY = Math.abs(y);
		if (absoluteX < threshold && absoluteY < threshold) {
			return null;
		}
		if (absoluteY >= absoluteX) {
			return y >= 0.0F
					? StickDpadRepeater.Direction.UP
					: StickDpadRepeater.Direction.DOWN;
		}
		return x >= 0.0F
				? StickDpadRepeater.Direction.RIGHT
				: StickDpadRepeater.Direction.LEFT;
	}
}
