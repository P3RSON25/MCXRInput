package dev.mcxrinput.avatar;

import dev.mcxrinput.avatar.AvatarLegSwing.Swing;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class AvatarLegSwingTest {
	private static final double EPSILON = 1.0e-9;

	@Test
	void idleSpeedIsExactlyNeutral() {
		Swing swing = AvatarLegSwing.fromWalkAnimation(12345.0, 0.0);

		assertSame(AvatarLegSwing.neutral(), swing);
		assertEquals(0.0, swing.leftRadians(), 0.0);
		assertEquals(0.0, swing.rightRadians(), 0.0);
	}

	@Test
	void amplitudeScalesWithMinecraftMovementSpeedAndCapsConservatively() {
		Swing half = AvatarLegSwing.fromWalkAnimation(0.0, 0.25);
		Swing full = AvatarLegSwing.fromWalkAnimation(0.0, 0.50);
		Swing capped = AvatarLegSwing.fromWalkAnimation(0.0, 1.0);

		assertEquals(
				AvatarLegSwing.MAXIMUM_SWING_RADIANS / 2.0,
				half.rightRadians(),
				EPSILON);
		assertEquals(
				AvatarLegSwing.MAXIMUM_SWING_RADIANS,
				full.rightRadians(),
				EPSILON);
		assertEquals(full, capped);
	}

	@Test
	void phaseUsesVanillaFrequencyAndKeepsLegsOpposed() {
		double quarterCyclePosition = (Math.PI / 2.0)
				/ AvatarLegSwing.VANILLA_PHASE_SCALE;
		double halfCyclePosition = Math.PI
				/ AvatarLegSwing.VANILLA_PHASE_SCALE;
		Swing quarter = AvatarLegSwing.fromWalkAnimation(
				quarterCyclePosition, 0.5);
		Swing half = AvatarLegSwing.fromWalkAnimation(
				halfCyclePosition, 0.5);

		assertEquals(0.0, quarter.leftRadians(), EPSILON);
		assertEquals(0.0, quarter.rightRadians(), EPSILON);
		assertEquals(
				AvatarLegSwing.MAXIMUM_SWING_RADIANS,
				half.leftRadians(),
				EPSILON);
		assertEquals(
				-AvatarLegSwing.MAXIMUM_SWING_RADIANS,
				half.rightRadians(),
				EPSILON);
	}

	@Test
	void everySampleIsDeterministicOpposedAndBounded() {
		for (double position : new double[]{-1000.0, -1.0, 0.0, 0.5, 50_000.0}) {
			for (double speed : new double[]{0.01, 0.1, 0.4, 0.5, 1.0}) {
				Swing first = AvatarLegSwing.fromWalkAnimation(position, speed);
				Swing second = AvatarLegSwing.fromWalkAnimation(position, speed);
				assertEquals(first, second);
				assertEquals(0.0,
						first.leftRadians() + first.rightRadians(), EPSILON);
				assertTrue(Math.abs(first.leftRadians())
						<= AvatarLegSwing.MAXIMUM_SWING_RADIANS + EPSILON);
			}
		}
	}

	@Test
	void invalidAnimationStateFailsClosed() {
		assertThrows(IllegalArgumentException.class,
				() -> AvatarLegSwing.fromWalkAnimation(Double.NaN, 0.2));
		assertThrows(IllegalArgumentException.class,
				() -> AvatarLegSwing.fromWalkAnimation(0.0, Double.NaN));
		assertThrows(IllegalArgumentException.class,
				() -> AvatarLegSwing.fromWalkAnimation(0.0, -0.01));
		assertThrows(IllegalArgumentException.class,
				() -> AvatarLegSwing.fromWalkAnimation(0.0, 1.01));
		assertThrows(IllegalArgumentException.class,
				() -> new Swing(
						AvatarLegSwing.MAXIMUM_SWING_RADIANS + 0.01,
						-AvatarLegSwing.MAXIMUM_SWING_RADIANS - 0.01));
		assertThrows(IllegalArgumentException.class,
				() -> new Swing(0.1, 0.1));
	}
}
