package dev.mcxrinput.avatar;

/**
 * Pure bounded walk-cycle sampling for the cosmetic tracked-avatar legs.
 *
 * <p>The inputs are Minecraft's already-smoothed, interpolated
 * {@code WalkAnimationState} position and speed. This class never reads or
 * changes controls, movement, collision, reach, entities, or packets.</p>
 */
public final class AvatarLegSwing {
	public static final double VANILLA_PHASE_SCALE = 0.6662;
	public static final double FULL_SWING_ANIMATION_SPEED = 0.50;
	public static final double MAXIMUM_SWING_RADIANS = Math.toRadians(20.0);
	private static final double INPUT_EPSILON = 1.0e-6;
	private static final double OPPOSITION_EPSILON = 1.0e-9;
	private static final Swing NEUTRAL = new Swing(0.0, 0.0);

	private AvatarLegSwing() {
	}

	/** Returns a conservative opposite-phase swing from ordinary movement state. */
	public static Swing fromWalkAnimation(
			double animationPosition,
			double animationSpeed) {
		if (!Double.isFinite(animationPosition)
				|| !Double.isFinite(animationSpeed)
				|| animationSpeed < 0.0
				|| animationSpeed > 1.0 + INPUT_EPSILON) {
			throw new IllegalArgumentException(
					"Avatar walk animation inputs must be finite and bounded");
		}
		if (animationSpeed <= INPUT_EPSILON) {
			return NEUTRAL;
		}

		double boundedSpeed = Math.min(animationSpeed, 1.0);
		double amplitude = MAXIMUM_SWING_RADIANS * Math.min(
				boundedSpeed / FULL_SWING_ANIMATION_SPEED,
				1.0);
		double phase = Math.IEEEremainder(
				animationPosition * VANILLA_PHASE_SCALE,
				Math.PI * 2.0);
		double rightRadians = Math.cos(phase) * amplitude;
		return new Swing(-rightRadians, rightRadians);
	}

	/** Exact idle pose without consulting a clock or movement state. */
	public static Swing neutral() {
		return NEUTRAL;
	}

	/** Opposed local-X rotations for one immutable extracted frame. */
	public record Swing(double leftRadians, double rightRadians) {
		public Swing {
			if (!Double.isFinite(leftRadians)
					|| !Double.isFinite(rightRadians)
					|| Math.abs(leftRadians) > MAXIMUM_SWING_RADIANS
							+ OPPOSITION_EPSILON
					|| Math.abs(rightRadians) > MAXIMUM_SWING_RADIANS
							+ OPPOSITION_EPSILON
					|| Math.abs(leftRadians + rightRadians)
							> OPPOSITION_EPSILON) {
				throw new IllegalArgumentException(
						"Avatar leg swing must be finite, bounded, and opposed");
			}
		}
	}
}
