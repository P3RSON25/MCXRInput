package dev.mcxrinput.input;

/** Emits one press edge from an analog/digital sample and never repeats while held. */
public final class AnalogButtonPress {
	private final AnalogButtonLatch latch;
	private boolean wasDown;

	public AnalogButtonPress(float releaseMargin) {
		latch = new AnalogButtonLatch(releaseMargin);
	}

	public boolean update(boolean active, float value, float pressThreshold) {
		boolean down = latch.update(active, value, pressThreshold);
		boolean pressed = down && !wasDown;
		wasDown = down;
		return pressed;
	}

	public void suppress() {
		latch.suppress();
		wasDown = false;
	}
}
