package dev.mcxrinput.avatar;

import dev.mcxrinput.avatar.AvatarViewTransform.Point;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

class AvatarViewTransformTest {
	private static final double EPSILON = 1.0e-6;

	@Test
	void identityScalePreservesCameraSpace() {
		assertEquals(new Point(0.2, -0.1, -0.5),
				AvatarViewTransform.compensateCameraPoint(0.2, -0.1, -0.5, 1.0F));
	}

	@Test
	void inverseCameraPlaneScaleCancelsNativeTangentMinification() {
		float scale = 0.40F;
		Point compensated = AvatarViewTransform.compensateCameraPoint(
				0.2, -0.1, -0.5, scale);
		assertEquals(0.5, compensated.x(), EPSILON);
		assertEquals(-0.25, compensated.y(), EPSILON);
		assertEquals(-0.5, compensated.z(), EPSILON);

		// The native projection multiplies the source tangent by the same scale.
		assertEquals(0.2 / 0.5,
				scale * (compensated.x() / -compensated.z()), EPSILON);
		assertEquals(-0.1 / 0.5,
				scale * (compensated.y() / -compensated.z()), EPSILON);
	}

	@Test
	void invalidOrUnsupportedCalibrationFailsClosed() {
		assertThrows(IllegalArgumentException.class,
				() -> AvatarViewTransform.compensateCameraPoint(0, 0, -1, 0.299F));
		assertThrows(IllegalArgumentException.class,
				() -> AvatarViewTransform.compensateCameraPoint(0, 0, -1, 1.001F));
		assertThrows(IllegalArgumentException.class,
				() -> AvatarViewTransform.compensateCameraPoint(
						Double.NaN, 0, -1, 1.0F));
	}
}
