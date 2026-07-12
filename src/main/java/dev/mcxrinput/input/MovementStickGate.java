package dev.mcxrinput.input;

/** Requires a neutral tracked movement-stick sample after every suppression. */
public final class MovementStickGate {
	private boolean armed;

	public boolean accepts(boolean active, float x, float y, float deadzone) {
		if (!active || !Float.isFinite(x) || !Float.isFinite(y)
				|| !Float.isFinite(deadzone) || deadzone <= 0.0F) {
			suppress();
			return false;
		}

		if (!armed) {
			if (Math.abs(x) <= deadzone && Math.abs(y) <= deadzone) {
				armed = true;
			}
			return false;
		}
		return true;
	}

	public void suppress() {
		armed = false;
	}
}
