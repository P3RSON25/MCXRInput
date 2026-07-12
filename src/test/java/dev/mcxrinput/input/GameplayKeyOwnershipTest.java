package dev.mcxrinput.input;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import static dev.mcxrinput.input.GameplayKeyOwnership.Decision.NONE;
import static dev.mcxrinput.input.GameplayKeyOwnership.Decision.PRESS;
import static dev.mcxrinput.input.GameplayKeyOwnership.Decision.REJECTED_PRESS;
import static dev.mcxrinput.input.GameplayKeyOwnership.Decision.RELEASE;
import static dev.mcxrinput.input.GameplayKeyOwnership.Decision.RELINQUISH;
import static org.junit.jupiter.api.Assertions.assertEquals;

class GameplayKeyOwnershipTest {
	@Test
	void heldControllerInputProducesOnePressAndOneRelease() {
		GameplayKeyOwnership ownership = new GameplayKeyOwnership();
		assertEquals(PRESS, ownership.update(true, true, false, false));
		for (int tick = 0; tick < 100; tick++) {
			assertEquals(NONE, ownership.update(true, true, true, false));
		}
		assertEquals(RELEASE, ownership.update(true, false, true, false));
	}

	@ParameterizedTest
	@ValueSource(strings = {"crouch", "sprint", "attack", "use"})
	void vanillaToggleModeRejectsHeldInputWithoutOscillation(String ignoredAction) {
		GameplayKeyOwnership ownership = new GameplayKeyOwnership();
		assertEquals(REJECTED_PRESS, ownership.update(false, true, false, false));
		for (int tick = 0; tick < 100; tick++) {
			assertEquals(NONE, ownership.update(false, true, false, false));
		}
		assertEquals(NONE, ownership.update(false, false, false, false));
	}

	@Test
	void enablingToggleModeDuringAnOwnedHoldReleasesOnceThenStaysReleased() {
		GameplayKeyOwnership ownership = new GameplayKeyOwnership();
		assertEquals(PRESS, ownership.update(true, true, false, false));
		assertEquals(RELEASE, ownership.update(false, true, true, false));
		for (int tick = 0; tick < 100; tick++) {
			assertEquals(NONE, ownership.update(false, true, false, false));
		}
	}

	@ParameterizedTest
	@ValueSource(strings = {"stale", "tracking_loss", "f8", "disconnect", "screen"})
	void everyFailClosedPathReleasesOwnedState(String ignoredReason) {
		GameplayKeyOwnership ownership = new GameplayKeyOwnership();
		assertEquals(PRESS, ownership.update(true, true, false, false));
		assertEquals(RELEASE, ownership.release(false));
	}

	@Test
	void independentlyHeldPhysicalInputIsNeverClaimedOrReleased() {
		GameplayKeyOwnership ownership = new GameplayKeyOwnership();
		assertEquals(NONE, ownership.update(true, true, true, true));
		assertEquals(NONE, ownership.update(true, false, true, true));

		assertEquals(PRESS, ownership.update(true, true, false, false));
		assertEquals(RELINQUISH, ownership.update(true, false, true, true));
	}

	@Test
	void failClosedReleaseAlwaysRelinquishesToCurrentPhysicalInput() {
		GameplayKeyOwnership ownership = new GameplayKeyOwnership();
		assertEquals(PRESS, ownership.update(true, true, false, false));
		assertEquals(RELINQUISH, ownership.release(true));
	}
}
