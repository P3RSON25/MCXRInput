package dev.mcxrinput.input;

import org.junit.jupiter.api.Test;

import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertEquals;

class UtilityWheelSelectionTest {
	private static final float THRESHOLD = 0.55F;

	@Test
	void mapsCardinalDirectionsToFixedActions() {
		assertSelection(UtilityWheelSelection.Action.PAUSE, 0.0F, 1.0F);
		assertSelection(UtilityWheelSelection.Action.CHAT, -1.0F, 0.0F);
		assertSelection(UtilityWheelSelection.Action.PLAYER_LIST, 0.0F, -1.0F);
		assertSelection(UtilityWheelSelection.Action.PERSPECTIVE, 1.0F, 0.0F);
	}

	@Test
	void usesTheDominantAxisForDiagonalInput() {
		assertSelection(UtilityWheelSelection.Action.PAUSE, 0.5F, 0.8F);
		assertSelection(UtilityWheelSelection.Action.CHAT, -0.9F, 0.2F);
		assertSelection(UtilityWheelSelection.Action.PLAYER_LIST, 0.4F, -0.7F);
		assertSelection(UtilityWheelSelection.Action.PERSPECTIVE, 0.8F, -0.3F);
	}

	@Test
	void doesNotSelectInsideOrOnTheActivationBoundary() {
		assertEquals(Optional.empty(), UtilityWheelSelection.select(0.0F, 0.0F, THRESHOLD));
		assertEquals(Optional.empty(), UtilityWheelSelection.select(0.3F, 0.4F, THRESHOLD));
		assertEquals(Optional.empty(), UtilityWheelSelection.select(THRESHOLD, 0.0F, THRESHOLD));
		assertEquals(Optional.of(UtilityWheelSelection.Action.PERSPECTIVE),
				UtilityWheelSelection.select(0.56F, 0.0F, THRESHOLD));
	}

	@Test
	void rejectsNonFiniteStickAndThresholdValues() {
		assertNoSelection(Float.NaN, 1.0F, THRESHOLD);
		assertNoSelection(Float.POSITIVE_INFINITY, 1.0F, THRESHOLD);
		assertNoSelection(1.0F, Float.NEGATIVE_INFINITY, THRESHOLD);
		assertNoSelection(1.0F, 0.0F, Float.NaN);
		assertNoSelection(1.0F, 0.0F, Float.POSITIVE_INFINITY);
	}

	@Test
	void rejectsThresholdsOutsideTheNormalizedStickRange() {
		assertNoSelection(1.0F, 0.0F, -0.01F);
		assertNoSelection(1.0F, 0.0F, 1.01F);
	}

	private static void assertSelection(UtilityWheelSelection.Action expected, float x, float y) {
		assertEquals(Optional.of(expected), UtilityWheelSelection.select(x, y, THRESHOLD));
	}

	private static void assertNoSelection(float x, float y, float threshold) {
		assertEquals(Optional.empty(), UtilityWheelSelection.select(x, y, threshold));
	}
}
