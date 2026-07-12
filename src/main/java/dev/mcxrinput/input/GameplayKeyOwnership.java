package dev.mcxrinput.input;

/**
 * Pure ownership state for one controller-driven Minecraft key mapping.
 * Decisions are emitted only on physical controller edges or a fail-closed release.
 */
public final class GameplayKeyOwnership {
	private boolean controllerWasDown;
	private boolean ownsKey;

	public Decision update(
			boolean inputAccepted,
			boolean controllerDown,
			boolean logicalKeyDown,
			boolean physicalKeyDown
	) {
		if (!inputAccepted && ownsKey) {
			ownsKey = false;
			return physicalKeyDown ? Decision.RELINQUISH : Decision.RELEASE;
		}

		if (controllerDown == controllerWasDown) {
			return Decision.NONE;
		}
		controllerWasDown = controllerDown;

		if (!controllerDown) {
			if (!ownsKey) {
				return Decision.NONE;
			}
			ownsKey = false;
			return physicalKeyDown ? Decision.RELINQUISH : Decision.RELEASE;
		}

		if (!inputAccepted) {
			return Decision.REJECTED_PRESS;
		}
		if (logicalKeyDown || physicalKeyDown) {
			// An independent keyboard, mouse, or mod input already owns the logical key.
			return Decision.NONE;
		}

		ownsKey = true;
		return Decision.PRESS;
	}

	public Decision release(boolean physicalKeyDown) {
		controllerWasDown = false;
		if (!ownsKey) {
			return Decision.NONE;
		}
		ownsKey = false;
		return physicalKeyDown
				? Decision.RELINQUISH
				: Decision.RELEASE;
	}

	public boolean ownsKey() {
		return ownsKey;
	}

	public enum Decision {
		NONE,
		PRESS,
		RELEASE,
		RELINQUISH,
		REJECTED_PRESS
	}
}
