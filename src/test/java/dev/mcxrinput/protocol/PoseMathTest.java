package dev.mcxrinput.protocol;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class PoseMathTest {
	private static final float EPSILON = 0.001F;

	@Test
	void identityLooksForward() {
		HeadOrientation orientation = PoseMath.toHeadOrientation(0, 0, 0, 1);
		assertEquals(0.0F, orientation.yawDegrees(), EPSILON);
		assertEquals(0.0F, orientation.pitchDegrees(), EPSILON);
	}

	@Test
	void quarterTurnAroundYAxisProducesOneToOneMinecraftYaw() {
		double halfAngle = Math.toRadians(45);
		HeadOrientation orientation = PoseMath.toHeadOrientation(0, Math.sin(halfAngle), 0, Math.cos(halfAngle));
		assertEquals(-90.0F, orientation.yawDegrees(), EPSILON);
		assertEquals(0.0F, orientation.pitchDegrees(), EPSILON);
	}

	@Test
	void pureRollIsStabilizedAway() {
		double halfAngle = Math.toRadians(45);
		HeadOrientation orientation = PoseMath.toHeadOrientation(0, 0, Math.sin(halfAngle), Math.cos(halfAngle));
		assertEquals(0.0F, orientation.yawDegrees(), EPSILON);
		assertEquals(0.0F, orientation.pitchDegrees(), EPSILON);
	}

	@Test
	void rejectsBadQuaternions() {
		assertFalse(PoseMath.isPlausibleQuaternion(0, 0, 0, 0));
		assertFalse(PoseMath.isPlausibleQuaternion(Double.NaN, 0, 0, 1));
		assertTrue(PoseMath.isPlausibleQuaternion(0, 0, 0, 1));
	}

	@Test
	void wrapsAndClampsMinecraftAngles() {
		assertEquals(-179.0F, PoseMath.wrapDegrees(181.0F), EPSILON);
		assertEquals(90.0F, PoseMath.clampPitch(120.0F), EPSILON);
		assertEquals(-90.0F, PoseMath.clampPitch(-120.0F), EPSILON);
	}
}
