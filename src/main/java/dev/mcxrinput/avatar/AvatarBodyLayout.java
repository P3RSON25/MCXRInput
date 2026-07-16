package dev.mcxrinput.avatar;

import dev.mcxrinput.avatar.TwoBoneIkSolver.Basis;
import dev.mcxrinput.avatar.TwoBoneIkSolver.Vec3;
import dev.mcxrinput.avatar.AvatarLegSwing.Swing;

import java.util.Objects;

/**
 * Pure neutral torso/leg layout for the development tracked avatar.
 *
 * <p>Points use the same camera-relative, metre-as-block convention as the
 * tracked-arm solver. The supplied gravity direction keeps the torso and legs
 * upright while the camera nods. A caller may shorten the legs by Minecraft's
 * interpolated crouch eye-height drop and apply a bounded opposite walk swing;
 * every hip remains fixed inside the torso's gravity-stable frame.</p>
 *
 * <p>This class computes cosmetic geometry only. It has no Minecraft state,
 * input, collision, reach, or packet behavior.</p>
 */
public final class AvatarBodyLayout {
	public static final double TORSO_LENGTH_BLOCKS = 12.0 / 16.0;
	public static final double LEG_LENGTH_BLOCKS = 12.0 / 16.0;
	public static final double HALF_LEG_SPACING_BLOCKS = 1.9 / 16.0;
	public static final double TORSO_TOP_RISE_FROM_SHOULDERS_BLOCKS = 0.10;
	public static final double OUTER_BODY_HALF_DEPTH_BLOCKS = 2.25 / 16.0;
	public static final double TORSO_FRONT_CLEARANCE_BEHIND_SHOULDERS_BLOCKS =
			0.25 / 16.0;
	public static final double TORSO_CENTER_BACK_FROM_SHOULDERS_BLOCKS =
			OUTER_BODY_HALF_DEPTH_BLOCKS
					+ TORSO_FRONT_CLEARANCE_BEHIND_SHOULDERS_BLOCKS;
	public static final double LEG_TORSO_OVERLAP_BLOCKS = 0.05;
	public static final double LEG_DEPTH_SCALE = 0.99;
	public static final double MINIMUM_LEG_LENGTH_BLOCKS = 0.39;
	private static final double AXIS_EPSILON = 1.0e-9;
	private static final double EYE_HEIGHT_EPSILON_BLOCKS = 1.0e-3;

	private AvatarBodyLayout() {
	}

	/**
	 * Creates an upright torso and two aligned, optionally swinging legs.
	 *
	 * @param shoulderCenter camera-relative midpoint of the tracked shoulders
	 * @param gravityUp gravity-up expressed in the camera-relative pose basis
	 * @param cameraRight the camera-relative right direction
	 * @param legReachBlocks current hip-to-foot reach after crouch-height
	 *        compensation
	 * @return finite anchors and right-handed raw-player-model render bases
	 */
	public static Layout create(
			Vec3 shoulderCenter,
			Vec3 gravityUp,
			Vec3 cameraRight,
			double legReachBlocks) {
		return create(
				shoulderCenter,
				gravityUp,
				cameraRight,
				legReachBlocks,
				AvatarLegSwing.neutral());
	}

	/** Creates one deterministic body layout from an immutable swing sample. */
	public static Layout create(
			Vec3 shoulderCenter,
			Vec3 gravityUp,
			Vec3 cameraRight,
			double legReachBlocks,
			Swing legSwing) {
		Objects.requireNonNull(shoulderCenter, "shoulderCenter");
		Objects.requireNonNull(gravityUp, "gravityUp");
		Objects.requireNonNull(cameraRight, "cameraRight");
		Objects.requireNonNull(legSwing, "legSwing");
		if (!Double.isFinite(legReachBlocks)
				|| legReachBlocks < MINIMUM_LEG_LENGTH_BLOCKS
				|| legReachBlocks > LEG_LENGTH_BLOCKS) {
			throw new IllegalArgumentException(
					"Avatar leg length is outside the supported neutral range");
		}

		Vec3 up = gravityUp.normalized();
		Vec3 projectedRight = cameraRight.subtract(
				up.scale(cameraRight.dot(up)));
		if (projectedRight.length() <= AXIS_EPSILON) {
			throw new IllegalArgumentException(
					"Avatar camera-right axis must not be parallel to gravity");
		}
		Vec3 right = projectedRight.normalized();
		Vec3 down = up.scale(-1.0);
		Vec3 back = right.cross(up).normalized();

		// Keep the hardware-tested shoulders unchanged. The torso cube begins
		// slightly above their tracked midpoint so the neutral feet meet Minecraft's
		// standing ground plane. Its complete inflated front stays behind the
		// shoulder midpoint instead of folding forward across the leg rays.
		Vec3 torsoTop = shoulderCenter
				.add(up.scale(TORSO_TOP_RISE_FROM_SHOULDERS_BLOCKS))
				.add(back.scale(TORSO_CENTER_BACK_FROM_SHOULDERS_BLOCKS));
		Vec3 torsoHipCenter = torsoTop.add(
				down.scale(TORSO_LENGTH_BLOCKS));

		// Legs share the accepted torso center plane. The five-centimetre embed,
		// together with the renderer's one-percent depth inset, keeps every
		// rotating top corner inside the torso throughout the bounded swing. This
		// avoids a detached hip or exposed cap without a second anchor.
		Vec3 legHipCenter = torsoHipCenter;
		Vec3 legTopCenter = legHipCenter.add(
				up.scale(LEG_TORSO_OVERLAP_BLOCKS));
		Vec3 leftLegTop = legTopCenter.add(
				right.scale(-HALF_LEG_SPACING_BLOCKS));
		Vec3 rightLegTop = legTopCenter.add(
				right.scale(HALF_LEG_SPACING_BLOCKS));
		double legSegmentLengthBlocks = legReachBlocks
				+ LEG_TORSO_OVERLAP_BLOCKS;

		// Vanilla player cubes use model +Y downward and are normally submitted
		// after a 180-degree Z rotation. The equivalent basis below keeps the
		// front skin face toward camera-forward without mirroring the model UVs.
		Basis torsoBasis = new Basis(right.scale(-1.0), down, back);
		Basis leftLegBasis = swingBasis(torsoBasis, legSwing.leftRadians());
		Basis rightLegBasis = swingBasis(torsoBasis, legSwing.rightRadians());
		return new Layout(
				torsoTop,
				torsoHipCenter,
				legHipCenter,
				leftLegTop,
				rightLegTop,
				leftLegTop.add(leftLegBasis.yAxis().scale(
						legSegmentLengthBlocks)),
				rightLegTop.add(rightLegBasis.yAxis().scale(
						legSegmentLengthBlocks)),
				legReachBlocks,
				legSegmentLengthBlocks,
				torsoBasis,
				leftLegBasis,
				rightLegBasis);
	}

	private static Basis swingBasis(Basis neutral, double radians) {
		double cosine = Math.cos(radians);
		double sine = Math.sin(radians);
		Vec3 swungDown = neutral.yAxis().scale(cosine)
				.add(neutral.zAxis().scale(sine));
		Vec3 swungBack = neutral.zAxis().scale(cosine)
				.subtract(neutral.yAxis().scale(sine));
		return new Basis(neutral.xAxis(), swungDown, swungBack);
	}

	/**
	 * Converts Minecraft's smoothed rendered eye height into a neutral leg
	 * reach. Shortening from the hip cancels the camera's crouch drop in world
	 * space without moving the torso away from the tracked shoulders.
	 */
	public static double legLengthForEyeHeights(
			double standingEyeHeight,
			double crouchingEyeHeight,
			double renderedEyeHeight) {
		if (!Double.isFinite(standingEyeHeight)
				|| !Double.isFinite(crouchingEyeHeight)
				|| !Double.isFinite(renderedEyeHeight)
				|| standingEyeHeight <= 0.0
				|| crouchingEyeHeight <= 0.0
				|| crouchingEyeHeight > standingEyeHeight) {
			throw new IllegalArgumentException(
					"Avatar eye heights must be finite and ordered");
		}

		double maximumDrop = standingEyeHeight - crouchingEyeHeight;
		if (maximumDrop > LEG_LENGTH_BLOCKS - MINIMUM_LEG_LENGTH_BLOCKS
				|| renderedEyeHeight < crouchingEyeHeight - EYE_HEIGHT_EPSILON_BLOCKS
				|| renderedEyeHeight > standingEyeHeight + EYE_HEIGHT_EPSILON_BLOCKS) {
			throw new IllegalArgumentException(
					"Avatar rendered eye height is outside the supported crouch range");
		}

		double boundedEyeHeight = Math.max(
				crouchingEyeHeight,
				Math.min(standingEyeHeight, renderedEyeHeight));
		return LEG_LENGTH_BLOCKS
				- (standingEyeHeight - boundedEyeHeight);
	}

	/** Immutable anchors and bases for one extracted avatar frame. */
	public record Layout(
			Vec3 torsoTop,
			Vec3 torsoHipCenter,
			Vec3 legHipCenter,
			Vec3 leftLegTop,
			Vec3 rightLegTop,
			Vec3 leftLegBottom,
			Vec3 rightLegBottom,
			double legReachBlocks,
			double legSegmentLengthBlocks,
			Basis torsoBasis,
			Basis leftLegBasis,
			Basis rightLegBasis) {
		public Layout {
			Objects.requireNonNull(torsoTop, "torsoTop");
			Objects.requireNonNull(torsoHipCenter, "torsoHipCenter");
			Objects.requireNonNull(legHipCenter, "legHipCenter");
			Objects.requireNonNull(leftLegTop, "leftLegTop");
			Objects.requireNonNull(rightLegTop, "rightLegTop");
			Objects.requireNonNull(leftLegBottom, "leftLegBottom");
			Objects.requireNonNull(rightLegBottom, "rightLegBottom");
			Objects.requireNonNull(torsoBasis, "torsoBasis");
			Objects.requireNonNull(leftLegBasis, "leftLegBasis");
			Objects.requireNonNull(rightLegBasis, "rightLegBasis");
			if (!Double.isFinite(legReachBlocks)
					|| legReachBlocks < MINIMUM_LEG_LENGTH_BLOCKS
					|| legReachBlocks > LEG_LENGTH_BLOCKS
					|| !Double.isFinite(legSegmentLengthBlocks)
					|| Math.abs(legSegmentLengthBlocks
							- legReachBlocks - LEG_TORSO_OVERLAP_BLOCKS)
							> AXIS_EPSILON) {
				throw new IllegalArgumentException(
						"Avatar leg length is outside the supported neutral range");
			}
		}
	}
}
