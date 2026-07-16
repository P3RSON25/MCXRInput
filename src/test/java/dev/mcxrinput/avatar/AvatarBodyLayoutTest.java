package dev.mcxrinput.avatar;

import dev.mcxrinput.avatar.AvatarBodyLayout.Layout;
import dev.mcxrinput.avatar.AvatarLegSwing.Swing;
import dev.mcxrinput.avatar.TwoBoneIkSolver.Basis;
import dev.mcxrinput.avatar.TwoBoneIkSolver.Vec3;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class AvatarBodyLayoutTest {
	private static final double EPSILON = 1.0e-9;
	private static final double TARGET_SOURCE_FOV_DEGREES = 150.0;
	private static final double TARGET_WORLD_VIEW_SCALE = 0.40;
	private static final double NEAR_PLANE_BLOCKS = 0.05;
	private static final double BASE_TORSO_HALF_WIDTH_BLOCKS = 4.0 / 16.0;
	private static final double BASE_LEG_HALF_WIDTH_BLOCKS = 2.0 / 16.0;
	private static final double OUTER_LAYER_INFLATION_BLOCKS = 0.25 / 16.0;
	private static final double OUTER_LAYER_HALF_DEPTH_BLOCKS = 2.25 / 16.0;

	@Test
	void identityGravityAlignsNeutralLegsInsideTheAcceptedTorso() {
		Vec3 shoulderCenter = new Vec3(0.0, -0.22, 0.04);
		Layout layout = AvatarBodyLayout.create(
				shoulderCenter,
				Vec3.UNIT_Y,
				Vec3.UNIT_X,
				AvatarBodyLayout.LEG_LENGTH_BLOCKS);

		assertVecEquals(new Vec3(0.0, -0.12, 0.19625), layout.torsoTop());
		assertVecEquals(
				new Vec3(0.0, -0.87, 0.19625),
				layout.torsoHipCenter());
		assertVecEquals(layout.torsoHipCenter(), layout.legHipCenter());
		assertVecEquals(
				new Vec3(-0.11875, -0.82, 0.19625),
				layout.leftLegTop());
		assertVecEquals(
				new Vec3(0.11875, -0.82, 0.19625),
				layout.rightLegTop());
		assertVecEquals(
				new Vec3(-0.11875, -1.62, 0.19625),
				layout.leftLegBottom());
		assertVecEquals(
				new Vec3(0.11875, -1.62, 0.19625),
				layout.rightLegBottom());
		assertEquals(
				AvatarBodyLayout.LEG_LENGTH_BLOCKS
						+ AvatarBodyLayout.LEG_TORSO_OVERLAP_BLOCKS,
				layout.legSegmentLengthBlocks(), EPSILON);
		assertEquals(1.62, -layout.leftLegBottom().y(), EPSILON,
				"The neutral base-skin foot must meet Minecraft's standing ground plane");

		for (Basis basis : new Basis[]{
				layout.torsoBasis(),
				layout.leftLegBasis(),
				layout.rightLegBasis()}) {
			assertVecEquals(new Vec3(-1.0, 0.0, 0.0), basis.xAxis());
			assertVecEquals(new Vec3(0.0, -1.0, 0.0), basis.yAxis());
			assertVecEquals(new Vec3(0.0, 0.0, 1.0), basis.zAxis());
			assertOrthonormal(basis);
		}
	}

	@Test
	void completeInflatedTorsoFrontStaysBehindShoulderPlane() {
		for (double pitchDegrees : new double[]{0.0, 30.0, 45.0, 60.0}) {
			LayoutContext context = contextForPitch(
					pitchDegrees,
					AvatarBodyLayout.LEG_LENGTH_BLOCKS,
					AvatarLegSwing.neutral());
			for (double localX : new double[]{-0.265625, 0.265625}) {
				for (double localY : new double[]{
						-OUTER_LAYER_INFLATION_BLOCKS,
						AvatarBodyLayout.TORSO_LENGTH_BLOCKS
								+ OUTER_LAYER_INFLATION_BLOCKS}) {
					Vec3 frontCorner = partPoint(
							context.layout().torsoTop(),
							context.layout().torsoBasis(),
							localX,
							localY,
							-OUTER_LAYER_HALF_DEPTH_BLOCKS);
					double clearance = frontCorner
							.subtract(context.shoulderCenter())
							.dot(context.back());
					assertEquals(
							AvatarBodyLayout
									.TORSO_FRONT_CLEARANCE_BEHIND_SHOULDERS_BLOCKS,
							clearance,
							EPSILON);
				}
			}
		}
	}

	@Test
	void completeLegTopsStayInsideTheTorsoThroughoutTheSwing() {
		for (double legReach : new double[]{
				AvatarBodyLayout.MINIMUM_LEG_LENGTH_BLOCKS,
				0.40,
				AvatarBodyLayout.LEG_LENGTH_BLOCKS}) {
			for (double degrees = -20.0; degrees <= 20.0; degrees += 0.25) {
				double radians = Math.toRadians(degrees);
				Layout layout = contextForPitch(
						0.0,
						legReach,
						new Swing(-radians, radians)).layout();
				double legScale = layout.legSegmentLengthBlocks()
						/ AvatarBodyLayout.LEG_LENGTH_BLOCKS;

				assertVecEquals(layout.torsoHipCenter(), layout.legHipCenter());
				assertLegTopInsideTorso(
						layout, layout.leftLegTop(), layout.leftLegBasis(),
						0.0, legScale);
				assertLegTopInsideTorso(
						layout, layout.rightLegTop(), layout.rightLegBasis(),
						0.0, legScale);
				assertLegTopInsideTorso(
						layout, layout.leftLegTop(), layout.leftLegBasis(),
						OUTER_LAYER_INFLATION_BLOCKS, legScale);
				assertLegTopInsideTorso(
						layout, layout.rightLegTop(), layout.rightLegBasis(),
						OUTER_LAYER_INFLATION_BLOCKS, legScale);
			}
		}
		assertTrue(
				AvatarBodyLayout.HALF_LEG_SPACING_BLOCKS
						+ BASE_LEG_HALF_WIDTH_BLOCKS
						< BASE_TORSO_HALF_WIDTH_BLOCKS);
	}

	@Test
	void oppositeSwingKeepsFixedHipsAndSegmentLengths() {
		Swing swing = new Swing(Math.toRadians(-20.0), Math.toRadians(20.0));
		Layout neutral = AvatarBodyLayout.create(
				new Vec3(0.0, -0.22, 0.04),
				Vec3.UNIT_Y,
				Vec3.UNIT_X,
				AvatarBodyLayout.LEG_LENGTH_BLOCKS);
		Layout moving = AvatarBodyLayout.create(
				new Vec3(0.0, -0.22, 0.04),
				Vec3.UNIT_Y,
				Vec3.UNIT_X,
				AvatarBodyLayout.LEG_LENGTH_BLOCKS,
				swing);

		assertVecEquals(neutral.torsoTop(), moving.torsoTop());
		assertVecEquals(neutral.leftLegTop(), moving.leftLegTop());
		assertVecEquals(neutral.rightLegTop(), moving.rightLegTop());
		assertEquals(moving.legSegmentLengthBlocks(),
				moving.leftLegBottom().subtract(moving.leftLegTop()).length(), EPSILON);
		assertEquals(moving.legSegmentLengthBlocks(),
				moving.rightLegBottom().subtract(moving.rightLegTop()).length(), EPSILON);
		assertEquals(
				-moving.leftLegBottom().subtract(moving.leftLegTop()).z(),
				moving.rightLegBottom().subtract(moving.rightLegTop()).z(),
				EPSILON);
		assertEquals(moving.leftLegBottom().y(),
				moving.rightLegBottom().y(), EPSILON);
		assertOrthonormal(moving.leftLegBasis());
		assertOrthonormal(moving.rightLegBasis());
	}

	@Test
	void noddingCameraKeepsTorsoAndSwingAxesGravityStable() {
		double halfSqrt = Math.sqrt(0.5);
		Vec3 up = new Vec3(0.0, halfSqrt, halfSqrt);
		Vec3 down = up.scale(-1.0);
		Vec3 back = Vec3.UNIT_X.cross(up).normalized();
		Swing swing = new Swing(Math.toRadians(-10.0), Math.toRadians(10.0));
		Layout layout = AvatarBodyLayout.create(
				new Vec3(0.0, -0.12, 0.08),
				up,
				Vec3.UNIT_X,
				AvatarBodyLayout.LEG_LENGTH_BLOCKS,
				swing);

		assertVecEquals(down, layout.torsoBasis().yAxis());
		assertVecEquals(back, layout.torsoBasis().zAxis());
		assertVecEquals(
				down.scale(Math.cos(swing.leftRadians()))
						.add(back.scale(Math.sin(swing.leftRadians()))),
				layout.leftLegBasis().yAxis());
		assertVecEquals(
				down.scale(Math.cos(swing.rightRadians()))
						.add(back.scale(Math.sin(swing.rightRadians()))),
				layout.rightLegBasis().yAxis());
		assertOrthonormal(layout.leftLegBasis());
		assertOrthonormal(layout.rightLegBasis());
	}

	@Test
	void rightAxisIsProjectedDeterministicallyOntoTheGravityPlane() {
		Vec3 up = new Vec3(0.2, 0.9, -0.3).normalized();
		Vec3 impreciseRight = new Vec3(1.0, 0.4, 0.1);
		Swing swing = AvatarLegSwing.fromWalkAnimation(3.0, 0.4);
		Layout first = AvatarBodyLayout.create(
				new Vec3(0.0, 0.0, 0.0), up, impreciseRight,
				AvatarBodyLayout.LEG_LENGTH_BLOCKS, swing);
		Layout second = AvatarBodyLayout.create(
				new Vec3(0.0, 0.0, 0.0), up, impreciseRight,
				AvatarBodyLayout.LEG_LENGTH_BLOCKS, swing);

		assertEquals(first, second);
		for (Basis basis : new Basis[]{
				first.torsoBasis(), first.leftLegBasis(), first.rightLegBasis()}) {
			assertEquals(0.0, basis.xAxis().dot(up), EPSILON);
			assertOrthonormal(basis);
		}
	}

	@Test
	void translatedAnchorMovesTheCompleteRigRigidly() {
		Vec3 shoulders = new Vec3(0.0, -0.22, 0.04);
		Vec3 shift = new Vec3(0.0, -0.27, 0.0);
		Swing swing = AvatarLegSwing.fromWalkAnimation(2.0, 0.4);
		Layout first = AvatarBodyLayout.create(
				shoulders, Vec3.UNIT_Y, Vec3.UNIT_X,
				AvatarBodyLayout.LEG_LENGTH_BLOCKS, swing);
		Layout shifted = AvatarBodyLayout.create(
				shoulders.add(shift), Vec3.UNIT_Y, Vec3.UNIT_X,
				AvatarBodyLayout.LEG_LENGTH_BLOCKS, swing);

		assertVecEquals(shift, shifted.torsoTop().subtract(first.torsoTop()));
		assertVecEquals(shift,
				shifted.leftLegTop().subtract(first.leftLegTop()));
		assertVecEquals(shift,
				shifted.rightLegBottom().subtract(first.rightLegBottom()));
		assertEquals(first.leftLegBasis(), shifted.leftLegBasis());
		assertEquals(first.rightLegBasis(), shifted.rightLegBasis());
	}

	@Test
	void crouchShortensSwingingLegsWithoutMovingTheirHips() {
		double crouchingLegLength = AvatarBodyLayout.legLengthForEyeHeights(
				1.62, 1.27, 1.27);
		Swing swing = AvatarLegSwing.fromWalkAnimation(0.0, 0.4);
		Layout standing = AvatarBodyLayout.create(
				new Vec3(0.0, -0.22, 0.04), Vec3.UNIT_Y, Vec3.UNIT_X,
				AvatarBodyLayout.LEG_LENGTH_BLOCKS, swing);
		Layout crouching = AvatarBodyLayout.create(
				new Vec3(0.0, -0.22, 0.04), Vec3.UNIT_Y, Vec3.UNIT_X,
				crouchingLegLength, swing);

		assertEquals(0.40, crouchingLegLength, EPSILON);
		assertVecEquals(standing.torsoTop(), crouching.torsoTop());
		assertVecEquals(standing.leftLegTop(), crouching.leftLegTop());
		assertEquals(crouching.legSegmentLengthBlocks(),
				crouching.leftLegBottom().subtract(crouching.leftLegTop()).length(),
				EPSILON);
		assertEquals(standing.leftLegBasis(), crouching.leftLegBasis());
	}

	@Test
	void alignedLegsAndForwardStepRemainVisibleInTargetView() {
		for (double pitch : new double[]{40.0, 45.0, 55.0, 60.0}) {
			Layout neutral = contextForPitch(
					pitch,
					AvatarBodyLayout.LEG_LENGTH_BLOCKS,
					AvatarLegSwing.neutral()).layout();
			assertFrontIntersectsTarget(
					projectOuterLegFront(
							neutral.leftLegTop(),
							neutral.leftLegBasis(),
							neutral.legSegmentLengthBlocks()
									/ AvatarBodyLayout.LEG_LENGTH_BLOCKS),
					0.01,
					"neutral leg at " + pitch);
		}

		Swing maximum = new Swing(
				-AvatarLegSwing.MAXIMUM_SWING_RADIANS,
				AvatarLegSwing.MAXIMUM_SWING_RADIANS);
		for (double pitch : new double[]{30.0, 35.0, 40.0, 45.0, 55.0, 60.0}) {
			Layout moving = contextForPitch(
					pitch,
					AvatarBodyLayout.LEG_LENGTH_BLOCKS,
					maximum).layout();
			assertFrontIntersectsTarget(
					projectOuterLegFront(
							moving.leftLegTop(),
							moving.leftLegBasis(),
							moving.legSegmentLengthBlocks()
									/ AvatarBodyLayout.LEG_LENGTH_BLOCKS),
					0.015,
					"forward walking leg at " + pitch);
		}
	}

	@Test
	void torsoNearPlaneCrossingsStayBelowTheTargetSourceView() {
		for (double pitchDegrees : new double[]{30.0, 35.0, 40.0, 45.0, 55.0, 60.0}) {
			Layout layout = contextForPitch(
					pitchDegrees,
					AvatarBodyLayout.LEG_LENGTH_BLOCKS,
					AvatarLegSwing.neutral()).layout();
			Vec3 topFront = partPoint(
					layout.torsoTop(), layout.torsoBasis(), 0.0,
					-OUTER_LAYER_INFLATION_BLOCKS,
					-OUTER_LAYER_HALF_DEPTH_BLOCKS);
			Vec3 bottomFront = partPoint(
					layout.torsoTop(), layout.torsoBasis(), 0.0,
					AvatarBodyLayout.TORSO_LENGTH_BLOCKS
							+ OUTER_LAYER_INFLATION_BLOCKS,
					-OUTER_LAYER_HALF_DEPTH_BLOCKS);
			Vec3 topRear = partPoint(
					layout.torsoTop(), layout.torsoBasis(), 0.0,
					-OUTER_LAYER_INFLATION_BLOCKS,
					OUTER_LAYER_HALF_DEPTH_BLOCKS);

			assertNearEdgeOutsideTarget(
					topFront, bottomFront, pitchDegrees, "front side");
			assertNearEdgeOutsideTarget(
					topFront, topRear, pitchDegrees, "shoulder top");
		}
	}

	@Test
	void invalidAxesLengthsAndSwingFailClosed() {
		Vec3 origin = new Vec3(0.0, 0.0, 0.0);
		assertThrows(IllegalArgumentException.class,
				() -> AvatarBodyLayout.create(
						origin, Vec3.UNIT_Y, Vec3.UNIT_Y,
						AvatarBodyLayout.LEG_LENGTH_BLOCKS));
		assertThrows(IllegalArgumentException.class,
				() -> AvatarBodyLayout.create(
						origin, new Vec3(0.0, 0.0, 0.0), Vec3.UNIT_X,
						AvatarBodyLayout.LEG_LENGTH_BLOCKS));
		assertThrows(NullPointerException.class,
				() -> AvatarBodyLayout.create(
						origin, Vec3.UNIT_Y, Vec3.UNIT_X,
						AvatarBodyLayout.LEG_LENGTH_BLOCKS, null));
		assertThrows(IllegalArgumentException.class,
				() -> AvatarBodyLayout.create(
						origin, Vec3.UNIT_Y, Vec3.UNIT_X,
						AvatarBodyLayout.MINIMUM_LEG_LENGTH_BLOCKS - 0.01));
		assertThrows(IllegalArgumentException.class,
				() -> AvatarBodyLayout.legLengthForEyeHeights(
						1.62, 1.27, Double.NaN));
		assertThrows(IllegalArgumentException.class,
				() -> AvatarBodyLayout.legLengthForEyeHeights(
						1.62, 1.0, 1.2));
	}

	private static void assertLegTopInsideTorso(
			Layout layout,
			Vec3 legTop,
			Basis legBasis,
			double inflation,
			double legScale) {
		double legHalfWidth = BASE_LEG_HALF_WIDTH_BLOCKS + inflation;
		double legHalfDepth = (2.0 / 16.0 + inflation)
				* AvatarBodyLayout.LEG_DEPTH_SCALE;
		double torsoHalfWidth = BASE_TORSO_HALF_WIDTH_BLOCKS + inflation;
		double torsoHalfDepth = 2.0 / 16.0 + inflation;
		double torsoMinimumY = -inflation;
		double torsoMaximumY = AvatarBodyLayout.TORSO_LENGTH_BLOCKS + inflation;
		for (double x : new double[]{-legHalfWidth, legHalfWidth}) {
			for (double z : new double[]{-legHalfDepth, legHalfDepth}) {
				Vec3 corner = partPoint(
						legTop, legBasis, x, -inflation * legScale, z);
				Vec3 local = corner.subtract(layout.torsoTop());
				assertTrue(Math.abs(local.dot(layout.torsoBasis().xAxis()))
						<= torsoHalfWidth + EPSILON);
				assertTrue(local.dot(layout.torsoBasis().yAxis())
						>= torsoMinimumY - EPSILON);
				assertTrue(local.dot(layout.torsoBasis().yAxis())
						<= torsoMaximumY + EPSILON);
				assertTrue(Math.abs(local.dot(layout.torsoBasis().zAxis()))
						<= torsoHalfDepth + EPSILON);
			}
		}
	}

	private static LayoutContext contextForPitch(
			double pitchDegrees,
			double legReachBlocks,
			Swing swing) {
		double radians = Math.toRadians(pitchDegrees);
		Vec3 up = new Vec3(0.0, Math.cos(radians), Math.sin(radians));
		Vec3 down = up.scale(-1.0);
		Vec3 back = Vec3.UNIT_X.cross(up).normalized();
		Vec3 shoulderCenter = down.scale(0.22).add(back.scale(0.04));
		Layout layout = AvatarBodyLayout.create(
				shoulderCenter, up, Vec3.UNIT_X, legReachBlocks, swing);
		return new LayoutContext(layout, shoulderCenter, down, back);
	}

	private static PartFront projectOuterLegFront(
			Vec3 top,
			Basis basis,
			double lengthScale) {
		return new PartFront(
				projectTarget(partPoint(
						top, basis, 0.0,
						-OUTER_LAYER_INFLATION_BLOCKS * lengthScale,
						-OUTER_LAYER_HALF_DEPTH_BLOCKS
								* AvatarBodyLayout.LEG_DEPTH_SCALE)),
				projectTarget(partPoint(
						top, basis, 0.0,
						(AvatarBodyLayout.LEG_LENGTH_BLOCKS
								+ OUTER_LAYER_INFLATION_BLOCKS) * lengthScale,
						-OUTER_LAYER_HALF_DEPTH_BLOCKS
								* AvatarBodyLayout.LEG_DEPTH_SCALE)));
	}

	private static void assertFrontIntersectsTarget(
			PartFront front,
			double minimumVisibleSpan,
			String description) {
		assertTrue(front.top().depth() > NEAR_PLANE_BLOCKS, description);
		assertTrue(front.bottom().depth() > NEAR_PLANE_BLOCKS, description);
		double minimum = Math.min(front.top().screenY(), front.bottom().screenY());
		double maximum = Math.max(front.top().screenY(), front.bottom().screenY());
		double visibleSpan = Math.min(maximum, 1.0) - Math.max(minimum, 0.0);
		assertTrue(visibleSpan >= minimumVisibleSpan,
				description + " must visibly intersect 150/0.40: " + front);
	}

	private static Vec3 partPoint(
			Vec3 top,
			Basis basis,
			double localX,
			double localY,
			double localZ) {
		return top
				.add(basis.xAxis().scale(localX))
				.add(basis.yAxis().scale(localY))
				.add(basis.zAxis().scale(localZ));
	}

	private static ProjectedPoint projectTarget(Vec3 point) {
		double depth = -point.z();
		double sourceLimit = Math.tan(
				Math.toRadians(TARGET_SOURCE_FOV_DEGREES / 2.0));
		double normalizedY = (point.y() / TARGET_WORLD_VIEW_SCALE)
				/ (depth * sourceLimit);
		return new ProjectedPoint(depth, (1.0 - normalizedY) / 2.0);
	}

	private static void assertNearEdgeOutsideTarget(
			Vec3 first,
			Vec3 second,
			double pitchDegrees,
			String edgeName) {
		double firstDepth = -first.z();
		double secondDepth = -second.z();
		Vec3 relevant;
		if ((firstDepth - NEAR_PLANE_BLOCKS)
				* (secondDepth - NEAR_PLANE_BLOCKS) <= 0.0) {
			double interpolation = (NEAR_PLANE_BLOCKS - firstDepth)
					/ (secondDepth - firstDepth);
			relevant = first.add(second.subtract(first).scale(interpolation));
		} else if (firstDepth > NEAR_PLANE_BLOCKS
				&& secondDepth > NEAR_PLANE_BLOCKS) {
			relevant = firstDepth < secondDepth ? first : second;
		} else {
			return;
		}
		ProjectedPoint projected = projectTarget(relevant);
		assertTrue(projected.screenY() > 1.0,
				edgeName + " near edge must be below 150/0.40 at pitch "
						+ pitchDegrees + ": " + projected);
	}

	private static void assertOrthonormal(Basis basis) {
		assertEquals(1.0, basis.xAxis().length(), EPSILON);
		assertEquals(1.0, basis.yAxis().length(), EPSILON);
		assertEquals(1.0, basis.zAxis().length(), EPSILON);
		assertEquals(0.0, basis.xAxis().dot(basis.yAxis()), EPSILON);
		assertEquals(0.0, basis.xAxis().dot(basis.zAxis()), EPSILON);
		assertEquals(0.0, basis.yAxis().dot(basis.zAxis()), EPSILON);
		assertVecEquals(basis.zAxis(), basis.xAxis().cross(basis.yAxis()));
	}

	private static void assertVecEquals(Vec3 expected, Vec3 actual) {
		assertEquals(expected.x(), actual.x(), EPSILON);
		assertEquals(expected.y(), actual.y(), EPSILON);
		assertEquals(expected.z(), actual.z(), EPSILON);
	}

	private record LayoutContext(
			Layout layout,
			Vec3 shoulderCenter,
			Vec3 down,
			Vec3 back) {
	}

	private record ProjectedPoint(double depth, double screenY) {
	}

	private record PartFront(ProjectedPoint top, ProjectedPoint bottom) {
	}
}
