package dev.mcxrinput.input;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class AnalogButtonPressTest {
	@Test
	void oneInventoryButtonEdgeProducesAtMostOneAction() {
		AnalogButtonPress press = new AnalogButtonPress(0.1F);
		assertFalse(press.update(true, 0.0F, 0.55F));

		int actions = 0;
		for (int tick = 0; tick < 100; tick++) {
			if (press.update(true, 1.0F, 0.55F)) {
				actions++;
			}
		}
		assertEquals(1, actions);

		assertFalse(press.update(true, 0.0F, 0.55F));
		assertTrue(press.update(true, 1.0F, 0.55F));
	}

	@Test
	void suppressionRequiresAReleaseBeforeAnotherPress() {
		AnalogButtonPress press = new AnalogButtonPress(0.1F);
		assertFalse(press.update(true, 0.0F, 0.55F));
		assertTrue(press.update(true, 1.0F, 0.55F));
		press.suppress();
		assertFalse(press.update(true, 1.0F, 0.55F));
		assertFalse(press.update(true, 0.0F, 0.55F));
		assertTrue(press.update(true, 1.0F, 0.55F));
	}
}
