package dev.mcxrinput.input;

/**
 * Converts a physical analog control into a stable button state.
 *
 * <p>The latch uses a lower release threshold to prevent sensor noise around
 * the press threshold from producing extra press edges. After tracking or
 * gameplay input is suppressed, the control must return below the release
 * threshold before it can be pressed again.</p>
 */
public final class AnalogButtonLatch {
	private final float releaseMargin;
	private boolean armed;
	private boolean down;

	public AnalogButtonLatch(float releaseMargin) {
		if (!Float.isFinite(releaseMargin) || releaseMargin <= 0.0F) {
			throw new IllegalArgumentException("releaseMargin must be finite and positive");
		}
		this.releaseMargin = releaseMargin;
	}

	public boolean update(boolean active, float value, float pressThreshold) {
		if (!active || !Float.isFinite(value) || !Float.isFinite(pressThreshold) || pressThreshold <= 0.0F) {
			suppress();
			return false;
		}

		// Keep a useful release range even when the configured press threshold is
		// smaller than the normal margin.
		float effectiveReleaseMargin = Math.min(releaseMargin, pressThreshold * 0.5F);
		float releaseThreshold = pressThreshold - effectiveReleaseMargin;
		if (!armed) {
			if (value <= releaseThreshold) {
				armed = true;
			}
			return false;
		}

		if (down) {
			if (value <= releaseThreshold) {
				down = false;
			}
		} else if (value >= pressThreshold) {
			down = true;
		}
		return down;
	}

	public void suppress() {
		armed = false;
		down = false;
	}
}
