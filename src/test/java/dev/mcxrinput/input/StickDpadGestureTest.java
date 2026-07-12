package dev.mcxrinput.input;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

class StickDpadGestureTest {
	@Test
	void holdingForOneHundredTicksProducesOneAction() {
		StickDpadGesture gesture = new StickDpadGesture();
		assertNull(gesture.update(true, 0.0F, 0.0F, 0.3F));

		int actions = 0;
		for (int tick = 0; tick < 100; tick++) {
			if (gesture.update(true, 0.9F, 0.0F, 0.3F) != null) {
				actions++;
			}
		}

		assertEquals(1, actions);
	}

	@Test
	void neutralRearmsTheNextGesture() {
		StickDpadGesture gesture = new StickDpadGesture();
		assertNull(gesture.update(true, 0.0F, 0.0F, 0.3F));
		assertEquals(StickDpadRepeater.Direction.RIGHT,
				gesture.update(true, 0.9F, 0.0F, 0.3F));
		assertNull(gesture.update(true, 0.0F, 0.0F, 0.3F));
		assertEquals(StickDpadRepeater.Direction.LEFT,
				gesture.update(true, -0.9F, 0.0F, 0.3F));
	}

	@Test
	void changingDirectionWithoutNeutralDoesNotCreateAnotherAction() {
		StickDpadGesture gesture = new StickDpadGesture();
		assertNull(gesture.update(true, 0.0F, 0.0F, 0.3F));
		assertEquals(StickDpadRepeater.Direction.RIGHT,
				gesture.update(true, 0.9F, 0.0F, 0.3F));
		assertNull(gesture.update(true, 0.0F, 0.9F, 0.3F));
		assertNull(gesture.update(true, -0.9F, 0.0F, 0.3F));
	}

	@Test
	void suppressionRequiresNeutralBeforeRearming() {
		StickDpadGesture gesture = new StickDpadGesture();
		assertNull(gesture.update(true, 0.0F, 0.0F, 0.3F));
		assertEquals(StickDpadRepeater.Direction.DOWN,
				gesture.update(true, 0.0F, -0.9F, 0.3F));

		gesture.suppress();
		assertNull(gesture.update(true, 0.0F, -0.9F, 0.3F));
		assertNull(gesture.update(true, 0.0F, 0.0F, 0.3F));
		assertEquals(StickDpadRepeater.Direction.DOWN,
				gesture.update(true, 0.0F, -0.9F, 0.3F));
	}

	@Test
	void inactiveOrInvalidSamplesFailClosed() {
		StickDpadGesture gesture = new StickDpadGesture();
		assertNull(gesture.update(true, 0.0F, 0.0F, 0.3F));
		assertNull(gesture.update(false, 1.0F, 0.0F, 0.3F));
		assertNull(gesture.update(true, 1.0F, 0.0F, 0.3F));
		assertNull(gesture.update(true, 0.0F, 0.0F, 0.3F));
		assertNull(gesture.update(true, Float.NaN, 0.0F, 0.3F));
	}
}
