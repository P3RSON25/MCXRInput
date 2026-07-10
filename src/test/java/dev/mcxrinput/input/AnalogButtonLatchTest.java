package dev.mcxrinput.input;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class AnalogButtonLatchTest {
	private static final float PRESS_THRESHOLD = 0.55F;

	@Test
	void producesOneStablePressAcrossThresholdNoise() {
		AnalogButtonLatch latch = new AnalogButtonLatch(0.10F);

		assertFalse(latch.update(true, 0.0F, PRESS_THRESHOLD));
		assertTrue(latch.update(true, 0.60F, PRESS_THRESHOLD));
		assertTrue(latch.update(true, 0.52F, PRESS_THRESHOLD));
		assertTrue(latch.update(true, 0.46F, PRESS_THRESHOLD));
		assertFalse(latch.update(true, 0.45F, PRESS_THRESHOLD));
	}

	@Test
	void heldTriggerCannotFireAfterSuppressionUntilReleased() {
		AnalogButtonLatch latch = new AnalogButtonLatch(0.10F);

		assertFalse(latch.update(true, 0.0F, PRESS_THRESHOLD));
		assertTrue(latch.update(true, 1.0F, PRESS_THRESHOLD));
		latch.suppress();

		assertFalse(latch.update(true, 1.0F, PRESS_THRESHOLD));
		assertFalse(latch.update(true, 0.0F, PRESS_THRESHOLD));
		assertTrue(latch.update(true, 1.0F, PRESS_THRESHOLD));
	}

	@Test
	void inactiveTrackingSuppressesTheControl() {
		AnalogButtonLatch latch = new AnalogButtonLatch(0.10F);

		assertFalse(latch.update(true, 0.0F, PRESS_THRESHOLD));
		assertTrue(latch.update(true, 1.0F, PRESS_THRESHOLD));
		assertFalse(latch.update(false, 1.0F, PRESS_THRESHOLD));
		assertFalse(latch.update(true, 1.0F, PRESS_THRESHOLD));
	}

	@Test
	void lowConfiguredThresholdStillHasAUsableReleaseRange() {
		AnalogButtonLatch latch = new AnalogButtonLatch(0.10F);

		assertFalse(latch.update(true, 0.01F, 0.05F));
		assertTrue(latch.update(true, 0.05F, 0.05F));
		assertFalse(latch.update(true, 0.02F, 0.05F));
	}
}
