package dev.mcxrinput.input;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

class StickDpadRepeaterTest {
	@Test
	void requiresNeutralBeforeFirstNavigation() {
		StickDpadRepeater repeater = new StickDpadRepeater(3, 2);

		assertNull(repeater.update(true, 1.0F, 0.0F, 0.5F));
		assertNull(repeater.update(true, 0.0F, 0.0F, 0.5F));
		assertEquals(StickDpadRepeater.Direction.RIGHT,
				repeater.update(true, 1.0F, 0.0F, 0.5F));
	}

	@Test
	void repeatsAfterDelayAtConfiguredInterval() {
		StickDpadRepeater repeater = new StickDpadRepeater(3, 2);
		assertNull(repeater.update(true, 0.0F, 0.0F, 0.5F));
		assertEquals(StickDpadRepeater.Direction.UP,
				repeater.update(true, 0.0F, 1.0F, 0.5F));
		assertNull(repeater.update(true, 0.0F, 1.0F, 0.5F));
		assertNull(repeater.update(true, 0.0F, 1.0F, 0.5F));
		assertEquals(StickDpadRepeater.Direction.UP,
				repeater.update(true, 0.0F, 1.0F, 0.5F));
		assertNull(repeater.update(true, 0.0F, 1.0F, 0.5F));
		assertEquals(StickDpadRepeater.Direction.UP,
				repeater.update(true, 0.0F, 1.0F, 0.5F));
	}

	@Test
	void dominantAxisProducesOnlyOneDirection() {
		StickDpadRepeater repeater = new StickDpadRepeater(3, 2);
		assertNull(repeater.update(true, 0.0F, 0.0F, 0.5F));
		assertEquals(StickDpadRepeater.Direction.DOWN,
				repeater.update(true, 0.7F, -0.9F, 0.5F));
	}

	@Test
	void trackingLossRequiresNeutralToRearm() {
		StickDpadRepeater repeater = new StickDpadRepeater(3, 2);
		assertNull(repeater.update(true, 0.0F, 0.0F, 0.5F));
		assertEquals(StickDpadRepeater.Direction.LEFT,
				repeater.update(true, -1.0F, 0.0F, 0.5F));
		assertNull(repeater.update(false, -1.0F, 0.0F, 0.5F));
		assertNull(repeater.update(true, -1.0F, 0.0F, 0.5F));
		assertNull(repeater.update(true, 0.0F, 0.0F, 0.5F));
		assertEquals(StickDpadRepeater.Direction.LEFT,
				repeater.update(true, -1.0F, 0.0F, 0.5F));
	}
}
