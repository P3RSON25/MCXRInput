package dev.mcxrinput.avatar;

import dev.mcxrinput.avatar.TwoBoneIkSolver.Basis;
import dev.mcxrinput.avatar.TwoBoneIkSolver.ReachStatus;
import dev.mcxrinput.avatar.TwoBoneIkSolver.Solution;
import dev.mcxrinput.avatar.TwoBoneIkSolver.Vec3;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class TwoBoneIkSolverTest {
	private static final double EPSILON = 1.0e-9;

	@Test
	void reachesTargetAndPreservesBothLengths() {
		Vec3 shoulder = new Vec3(0.1, -0.2, 0.05);
		Vec3 target = new Vec3(0.1, -0.2, -0.35);
		Solution solution = TwoBoneIkSolver.solve(
				shoulder, target, new Vec3(-1.0, -0.2, -0.15), 0.3, 0.3);

		assertEquals(ReachStatus.REACHABLE, solution.reachStatus());
		assertVecEquals(target, solution.wrist(), EPSILON);
		assertEquals(0.3, distance(solution.shoulder(), solution.elbow()), EPSILON);
		assertEquals(0.3, distance(solution.elbow(), solution.wrist()), EPSILON);
		assertTrue(solution.elbow().x() < shoulder.x());
		assertOrthonormal(solution.upperBasis());
		assertOrthonormal(solution.lowerBasis());
	}

	@Test
	void mirroredPolePreservesLeftAndRightBend() {
		Vec3 shoulder = new Vec3(0.0, 0.0, 0.0);
		Vec3 target = new Vec3(0.0, -0.05, -0.4);
		Solution left = TwoBoneIkSolver.solve(
				shoulder, target, new Vec3(-1.0, -0.2, -0.2), 0.32, 0.28);
		Solution right = TwoBoneIkSolver.solve(
				shoulder, target, new Vec3(1.0, -0.2, -0.2), 0.32, 0.28);

		assertEquals(-left.elbow().x(), right.elbow().x(), EPSILON);
		assertEquals(left.elbow().y(), right.elbow().y(), EPSILON);
		assertEquals(left.elbow().z(), right.elbow().z(), EPSILON);
		assertEquals(-left.bendDirection().x(), right.bendDirection().x(), EPSILON);
		assertEquals(left.bendDirection().y(), right.bendDirection().y(), EPSILON);
		assertEquals(left.bendDirection().z(), right.bendDirection().z(), EPSILON);
	}

	@Test
	void targetBeyondMaximumReachClampsToStraightArm() {
		Solution solution = TwoBoneIkSolver.solve(
				new Vec3(0.0, 0.0, 0.0),
				new Vec3(0.0, 0.0, -2.0),
				new Vec3(-1.0, -0.2, 0.0),
				0.3,
				0.2);

		assertEquals(ReachStatus.CLAMPED_TOO_FAR, solution.reachStatus());
		assertEquals(2.0, solution.requestedDistance(), EPSILON);
		assertEquals(0.5, solution.solvedDistance(), EPSILON);
		assertVecEquals(new Vec3(0.0, 0.0, -0.5), solution.wrist(), EPSILON);
		assertEquals(0.3, distance(solution.shoulder(), solution.elbow()), EPSILON);
		assertEquals(0.2, distance(solution.elbow(), solution.wrist()), EPSILON);
		assertFinite(solution);
	}

	@Test
	void targetInsideUnequalLengthMinimumClampsToFoldedArm() {
		Solution solution = TwoBoneIkSolver.solve(
				new Vec3(0.0, 0.0, 0.0),
				new Vec3(0.0, 0.0, -0.01),
				new Vec3(-1.0, 0.0, 0.0),
				0.4,
				0.2);

		assertEquals(ReachStatus.CLAMPED_TOO_CLOSE, solution.reachStatus());
		assertEquals(0.2, solution.solvedDistance(), EPSILON);
		assertVecEquals(new Vec3(0.0, 0.0, -0.2), solution.wrist(), EPSILON);
		assertEquals(0.4, distance(solution.shoulder(), solution.elbow()), EPSILON);
		assertEquals(0.2, distance(solution.elbow(), solution.wrist()), EPSILON);
		assertFinite(solution);
	}

	@Test
	void lowerSegmentLongerThanUpperAlsoClampsWithoutDegeneracy() {
		Solution solution = TwoBoneIkSolver.solve(
				new Vec3(0.0, 0.0, 0.0),
				new Vec3(0.0, 0.0, -0.01),
				new Vec3(1.0, -0.2, 0.0),
				0.2,
				0.4);

		assertEquals(ReachStatus.CLAMPED_TOO_CLOSE, solution.reachStatus());
		assertEquals(0.2, solution.solvedDistance(), EPSILON);
		assertEquals(0.2, distance(solution.shoulder(), solution.elbow()), EPSILON);
		assertEquals(0.4, distance(solution.elbow(), solution.wrist()), EPSILON);
		assertFinite(solution);
		assertOrthonormal(solution.upperBasis());
		assertOrthonormal(solution.lowerBasis());
	}

	@Test
	void coincidentTargetWithEqualLengthsUsesStableFiniteFallback() {
		Solution first = TwoBoneIkSolver.solve(
				new Vec3(0.0, 0.0, 0.0),
				new Vec3(0.0, 0.0, 0.0),
				new Vec3(-1.0, -0.2, 0.0),
				0.3,
				0.3);
		Solution second = TwoBoneIkSolver.solve(
				new Vec3(0.0, 0.0, 0.0),
				new Vec3(0.0, 0.0, 0.0),
				new Vec3(-1.0, -0.2, 0.0),
				0.3,
				0.3);

		assertEquals(ReachStatus.CLAMPED_TOO_CLOSE, first.reachStatus());
		assertEquals(0.0, first.requestedDistance(), EPSILON);
		assertTrue(first.solvedDistance() > 0.0);
		assertTrue(first.solvedDistance() < 1.0e-8);
		assertEquals(first, second);
		assertEquals(0.3, distance(first.shoulder(), first.elbow()), EPSILON);
		assertEquals(0.3, distance(first.elbow(), first.wrist()), EPSILON);
		assertFinite(first);
	}

	@Test
	void collinearPoleUsesDeterministicSingularityFallback() {
		Vec3 shoulder = new Vec3(0.0, 0.0, 0.0);
		Vec3 target = new Vec3(0.0, 0.0, -0.4);
		Solution exactlyCollinear = TwoBoneIkSolver.solve(
				shoulder, target, new Vec3(0.0, 0.0, -1.0), 0.3, 0.3);
		Solution numericallyCollinear = TwoBoneIkSolver.solve(
				shoulder, target, new Vec3(1.0e-12, 0.0, -1.0), 0.3, 0.3);

		assertVecEquals(exactlyCollinear.elbow(), numericallyCollinear.elbow(), EPSILON);
		assertVecEquals(exactlyCollinear.bendDirection(),
				numericallyCollinear.bendDirection(), EPSILON);
		assertFinite(exactlyCollinear);
		assertOrthonormal(exactlyCollinear.upperBasis());
		assertOrthonormal(exactlyCollinear.lowerBasis());
	}

	@Test
	void poleAtShoulderUsesStableFallback() {
		Vec3 shoulder = new Vec3(0.2, -0.1, 0.3);
		Solution solution = TwoBoneIkSolver.solve(
				shoulder,
				new Vec3(0.35, -0.2, -0.05),
				shoulder,
				0.31,
				0.27);

		assertEquals(ReachStatus.REACHABLE, solution.reachStatus());
		assertEquals(0.31, distance(solution.shoulder(), solution.elbow()), EPSILON);
		assertEquals(0.27, distance(solution.elbow(), solution.wrist()), EPSILON);
		assertFinite(solution);
		assertOrthonormal(solution.upperBasis());
		assertOrthonormal(solution.lowerBasis());
	}

	@Test
	void exactReachBoundariesDoNotProduceNan() {
		Solution extended = TwoBoneIkSolver.solve(
				new Vec3(0.0, 0.0, 0.0), new Vec3(0.0, 0.0, -0.6),
				new Vec3(-1.0, 0.0, 0.0), 0.4, 0.2);
		Solution folded = TwoBoneIkSolver.solve(
				new Vec3(0.0, 0.0, 0.0), new Vec3(0.0, 0.0, -0.2),
				new Vec3(-1.0, 0.0, 0.0), 0.4, 0.2);

		assertEquals(ReachStatus.REACHABLE, extended.reachStatus());
		assertEquals(ReachStatus.REACHABLE, folded.reachStatus());
		assertEquals(0.4, distance(extended.shoulder(), extended.elbow()), EPSILON);
		assertEquals(0.2, distance(extended.elbow(), extended.wrist()), EPSILON);
		assertEquals(0.4, distance(folded.shoulder(), folded.elbow()), EPSILON);
		assertEquals(0.2, distance(folded.elbow(), folded.wrist()), EPSILON);
		assertFinite(extended);
		assertFinite(folded);
	}

	@Test
	void nonOriginDiagonalSolveRetainsGeometryAndBasisHandedness() {
		Solution solution = TwoBoneIkSolver.solve(
				new Vec3(12.0, -3.0, 7.0),
				new Vec3(12.2, -2.85, 6.65),
				new Vec3(11.0, -3.5, 6.8),
				0.34,
				0.29);

		assertEquals(ReachStatus.REACHABLE, solution.reachStatus());
		assertVecEquals(new Vec3(12.2, -2.85, 6.65), solution.wrist(), EPSILON);
		assertEquals(0.34, distance(solution.shoulder(), solution.elbow()), EPSILON);
		assertEquals(0.29, distance(solution.elbow(), solution.wrist()), EPSILON);
		assertOrthonormal(solution.upperBasis());
		assertOrthonormal(solution.lowerBasis());
	}

	@Test
	void invalidLengthsAndVectorsFailClosed() {
		Vec3 origin = new Vec3(0.0, 0.0, 0.0);
		Vec3 target = new Vec3(0.0, 0.0, -0.4);
		Vec3 pole = new Vec3(-1.0, 0.0, 0.0);

		assertThrows(IllegalArgumentException.class,
				() -> TwoBoneIkSolver.solve(origin, target, pole, 0.0, 0.3));
		assertThrows(IllegalArgumentException.class,
				() -> TwoBoneIkSolver.solve(origin, target, pole, 0.3, -0.1));
		assertThrows(IllegalArgumentException.class,
				() -> TwoBoneIkSolver.solve(origin, target, pole, Double.NaN, 0.3));
		assertThrows(IllegalArgumentException.class,
				() -> TwoBoneIkSolver.solve(
						origin, target, pole, Double.MAX_VALUE, Double.MAX_VALUE));
		assertThrows(IllegalArgumentException.class,
				() -> new Vec3(Double.NaN, 0.0, 0.0));
	}

	private static double distance(Vec3 first, Vec3 second) {
		return first.subtract(second).length();
	}

	private static void assertFinite(Solution solution) {
		assertVecFinite(solution.shoulder());
		assertVecFinite(solution.elbow());
		assertVecFinite(solution.wrist());
		assertVecFinite(solution.bendDirection());
		assertVecFinite(solution.upperBasis().xAxis());
		assertVecFinite(solution.upperBasis().yAxis());
		assertVecFinite(solution.upperBasis().zAxis());
		assertVecFinite(solution.lowerBasis().xAxis());
		assertVecFinite(solution.lowerBasis().yAxis());
		assertVecFinite(solution.lowerBasis().zAxis());
		assertTrue(Double.isFinite(solution.requestedDistance()));
		assertTrue(Double.isFinite(solution.solvedDistance()));
	}

	private static void assertVecFinite(Vec3 vector) {
		assertTrue(Double.isFinite(vector.x()));
		assertTrue(Double.isFinite(vector.y()));
		assertTrue(Double.isFinite(vector.z()));
	}

	private static void assertOrthonormal(Basis basis) {
		assertEquals(1.0, basis.xAxis().length(), EPSILON);
		assertEquals(1.0, basis.yAxis().length(), EPSILON);
		assertEquals(1.0, basis.zAxis().length(), EPSILON);
		assertEquals(0.0, basis.xAxis().dot(basis.yAxis()), EPSILON);
		assertEquals(0.0, basis.xAxis().dot(basis.zAxis()), EPSILON);
		assertEquals(0.0, basis.yAxis().dot(basis.zAxis()), EPSILON);
		assertVecEquals(basis.zAxis(), basis.xAxis().cross(basis.yAxis()), EPSILON);
	}

	private static void assertVecEquals(Vec3 expected, Vec3 actual, double epsilon) {
		assertEquals(expected.x(), actual.x(), epsilon);
		assertEquals(expected.y(), actual.y(), epsilon);
		assertEquals(expected.z(), actual.z(), epsilon);
	}
}
