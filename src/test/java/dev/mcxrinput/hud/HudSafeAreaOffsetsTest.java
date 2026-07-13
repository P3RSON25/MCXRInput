package dev.mcxrinput.hud;

import org.junit.jupiter.api.Test;

import static dev.mcxrinput.hud.HudSafeAreaOffsets.HorizontalAnchor.CENTER;
import static dev.mcxrinput.hud.HudSafeAreaOffsets.HorizontalAnchor.LEFT;
import static dev.mcxrinput.hud.HudSafeAreaOffsets.HorizontalAnchor.RIGHT;
import static dev.mcxrinput.hud.HudSafeAreaOffsets.VerticalAnchor.BOTTOM;
import static dev.mcxrinput.hud.HudSafeAreaOffsets.VerticalAnchor.TOP;
import static org.junit.jupiter.api.Assertions.assertEquals;

class HudSafeAreaOffsetsTest {
	private static final double EPSILON = 0.0001;

	@Test
	void bottomCenterMovesUpWithoutHorizontalMovement() {
		assertOffset(0.0, -45.0,
				HudSafeAreaOffsets.calculate(1000, 500, 0.31, 0.09, CENTER, BOTTOM));
	}

	@Test
	void topCenterMovesDownWithoutHorizontalMovement() {
		assertOffset(0.0, 45.0,
				HudSafeAreaOffsets.calculate(1000, 500, 0.31, 0.09, CENTER, TOP));
	}

	@Test
	void cornerAnchorsCombineIndependentTranslationAxes() {
		assertOffset(310.0, -45.0,
				HudSafeAreaOffsets.calculate(1000, 500, 0.31, 0.09, LEFT, BOTTOM));
		assertOffset(-310.0, 45.0,
				HudSafeAreaOffsets.calculate(1000, 500, 0.31, 0.09, RIGHT, TOP));
	}

	@Test
	void centerAnchorsRemainUnchanged() {
		assertOffset(0.0, 0.0, HudSafeAreaOffsets.calculate(
				1000, 500, 0.31, 0.09, CENTER,
				HudSafeAreaOffsets.VerticalAnchor.CENTER));
	}

	@Test
	void insetValidationUsesFiniteFallbackAndConservativeBounds() {
		assertEquals(0.31, HudSafeAreaOffsets.sanitizeInset(Double.NaN, 0.31), EPSILON);
		assertEquals(0.0, HudSafeAreaOffsets.sanitizeInset(-2.0, 0.31), EPSILON);
		assertEquals(0.45, HudSafeAreaOffsets.sanitizeInset(2.0, 0.31), EPSILON);
		assertEquals(0.0,
				HudSafeAreaOffsets.sanitizeInset(Double.POSITIVE_INFINITY, Double.NaN),
				EPSILON);
	}

	@Test
	void invalidDimensionsOrAnchorsFailClosedToNoMovement() {
		assertOffset(0.0, 0.0,
				HudSafeAreaOffsets.calculate(0, 500, 0.31, 0.09, LEFT, TOP));
		assertOffset(0.0, 0.0,
				HudSafeAreaOffsets.calculate(1000, -1, 0.31, 0.09, RIGHT, BOTTOM));
		assertOffset(0.0, 0.0,
				HudSafeAreaOffsets.calculate(1000, 500, 0.31, 0.09, null, BOTTOM));
	}

	private static void assertOffset(
			double expectedX, double expectedY, HudSafeAreaOffsets.Offset actual) {
		assertEquals(expectedX, actual.x(), EPSILON);
		assertEquals(expectedY, actual.y(), EPSILON);
	}
}
