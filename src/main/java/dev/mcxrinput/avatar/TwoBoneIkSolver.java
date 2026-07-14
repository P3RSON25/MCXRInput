package dev.mcxrinput.avatar;

import java.util.Objects;

/**
 * Pure two-segment inverse kinematics for cosmetic arms.
 *
 * <p>All points use the native bridge's camera-relative OpenXR convention:
 * {@code +X} right, {@code +Y} up, and {@code -Z} forward. Distances are in
 * metres, which the cosmetic renderer treats as Minecraft blocks. The pole is
 * a point in that same space which indicates the preferred side of the
 * shoulder-to-wrist line for the elbow.</p>
 *
 * <p>This class only computes geometry. It has no Minecraft state, input, or
 * packet behavior.</p>
 */
public final class TwoBoneIkSolver {
	private static final double DIRECTION_EPSILON = 1.0e-12;
	private static final double SINGULAR_POLE_SINE = 1.0e-8;
	private static final double MINIMUM_RELATIVE_SOLVE_DISTANCE = 1.0e-9;

	private TwoBoneIkSolver() {
	}

	/**
	 * Solves an arm while preserving both requested segment lengths.
	 *
	 * <p>An unreachable target is projected onto the nearest point of the arm's
	 * reachable radial interval. If equal segment lengths make the mathematical
	 * minimum distance zero, a negligible relative distance is used so the
	 * shoulder-to-wrist direction and render bases remain well-defined.</p>
	 *
	 * @param shoulder the fixed shoulder point
	 * @param wristTarget the tracked grip target
	 * @param elbowPole a point indicating the preferred elbow-bend side
	 * @param upperLength shoulder-to-elbow length, greater than zero
	 * @param lowerLength elbow-to-wrist length, greater than zero
	 * @return finite solved joints and right-handed segment bases
	 */
	public static Solution solve(
			Vec3 shoulder,
			Vec3 wristTarget,
			Vec3 elbowPole,
			double upperLength,
			double lowerLength) {
		Objects.requireNonNull(shoulder, "shoulder");
		Objects.requireNonNull(wristTarget, "wristTarget");
		Objects.requireNonNull(elbowPole, "elbowPole");
		validateLength(upperLength, "upperLength");
		validateLength(lowerLength, "lowerLength");

		double maximumReach = upperLength + lowerLength;
		if (!Double.isFinite(maximumReach)) {
			throw new IllegalArgumentException("Combined arm length must be finite");
		}

		Vec3 targetOffset = wristTarget.subtract(shoulder);
		double targetDistance = targetOffset.length();
		if (!Double.isFinite(targetDistance)) {
			throw new IllegalArgumentException("Shoulder-to-target distance must be finite");
		}

		Vec3 reachDirection = targetDistance > DIRECTION_EPSILON
				? targetOffset.scale(1.0 / targetDistance)
				: Vec3.FORWARD;
		Vec3 bendDirection = bendDirection(shoulder, elbowPole, reachDirection);

		double geometricMinimumReach = Math.abs(upperLength - lowerLength);
		double numericalMinimumReach = maximumReach * MINIMUM_RELATIVE_SOLVE_DISTANCE;
		double minimumSolveDistance = Math.max(geometricMinimumReach, numericalMinimumReach);

		ReachStatus status;
		double solvedDistance;
		if (targetDistance < minimumSolveDistance) {
			status = ReachStatus.CLAMPED_TOO_CLOSE;
			solvedDistance = minimumSolveDistance;
		} else if (targetDistance > maximumReach) {
			status = ReachStatus.CLAMPED_TOO_FAR;
			solvedDistance = maximumReach;
		} else {
			status = ReachStatus.REACHABLE;
			solvedDistance = targetDistance;
		}

		// Normalize by total length before applying the cosine rule. This avoids
		// squaring large real-world values and keeps the intermediate range bounded.
		double normalizedUpper = upperLength / maximumReach;
		double normalizedLower = lowerLength / maximumReach;
		double normalizedDistance = solvedDistance / maximumReach;
		double along = 0.5 * (normalizedDistance
				+ (normalizedUpper - normalizedLower)
						* ((normalizedUpper + normalizedLower) / normalizedDistance));
		along = clamp(along, -normalizedUpper, normalizedUpper);
		double heightSquared = Math.max(0.0,
				normalizedUpper * normalizedUpper - along * along);
		double height = Math.sqrt(heightSquared);

		Vec3 wrist = shoulder.add(reachDirection.scale(solvedDistance));
		Vec3 elbow = shoulder
				.add(reachDirection.scale(along * maximumReach))
				.add(bendDirection.scale(height * maximumReach));

		// The plane normal remains usable when the arm is exactly straight or
		// folded because it comes from the stable pole basis, not the joint angle.
		Vec3 planeNormal = bendDirection.cross(reachDirection).normalized();
		Vec3 upperDirection = elbow.subtract(shoulder).normalized();
		Vec3 lowerDirection = wrist.subtract(elbow).normalized();

		return new Solution(
				shoulder,
				elbow,
				wrist,
				basisFor(upperDirection, planeNormal),
				basisFor(lowerDirection, planeNormal),
				bendDirection,
				status,
				targetDistance,
				solvedDistance);
	}

	private static void validateLength(double value, String name) {
		if (!Double.isFinite(value) || value <= 0.0) {
			throw new IllegalArgumentException(name + " must be finite and greater than zero");
		}
	}

	private static Vec3 bendDirection(Vec3 shoulder, Vec3 pole, Vec3 reachDirection) {
		Vec3 poleOffset = pole.subtract(shoulder);
		double poleLength = poleOffset.length();
		Vec3 projected = poleOffset.subtract(
				reachDirection.scale(poleOffset.dot(reachDirection)));
		double projectedLength = projected.length();

		if (poleLength > DIRECTION_EPSILON
				&& projectedLength > poleLength * SINGULAR_POLE_SINE) {
			return projected.scale(1.0 / projectedLength);
		}
		return deterministicPerpendicular(reachDirection);
	}

	private static Vec3 deterministicPerpendicular(Vec3 direction) {
		double absoluteX = Math.abs(direction.x());
		double absoluteY = Math.abs(direction.y());
		double absoluteZ = Math.abs(direction.z());
		Vec3 reference;
		if (absoluteX <= absoluteY && absoluteX <= absoluteZ) {
			reference = Vec3.UNIT_X;
		} else if (absoluteY <= absoluteZ) {
			reference = Vec3.UNIT_Y;
		} else {
			reference = Vec3.UNIT_Z;
		}

		return reference.subtract(direction.scale(reference.dot(direction))).normalized();
	}

	private static Basis basisFor(Vec3 longitudinal, Vec3 planeNormal) {
		Vec3 xAxis = longitudinal.cross(planeNormal).normalized();
		Vec3 zAxis = xAxis.cross(longitudinal).normalized();
		return new Basis(xAxis, longitudinal, zAxis);
	}

	private static double clamp(double value, double minimum, double maximum) {
		return Math.max(minimum, Math.min(maximum, value));
	}

	/** Describes whether the requested wrist radius had to be clamped. */
	public enum ReachStatus {
		REACHABLE,
		CLAMPED_TOO_CLOSE,
		CLAMPED_TOO_FAR
	}

	/**
	 * One right-handed render basis. Local {@code +Y} follows the segment from
	 * its proximal joint to its distal joint; {@code +X} lies in the bend plane.
	 */
	public record Basis(Vec3 xAxis, Vec3 yAxis, Vec3 zAxis) {
		public Basis {
			Objects.requireNonNull(xAxis, "xAxis");
			Objects.requireNonNull(yAxis, "yAxis");
			Objects.requireNonNull(zAxis, "zAxis");
		}
	}

	/** Complete solved arm geometry in the same space as the inputs. */
	public record Solution(
			Vec3 shoulder,
			Vec3 elbow,
			Vec3 wrist,
			Basis upperBasis,
			Basis lowerBasis,
			Vec3 bendDirection,
			ReachStatus reachStatus,
			double requestedDistance,
			double solvedDistance) {
		public Solution {
			Objects.requireNonNull(shoulder, "shoulder");
			Objects.requireNonNull(elbow, "elbow");
			Objects.requireNonNull(wrist, "wrist");
			Objects.requireNonNull(upperBasis, "upperBasis");
			Objects.requireNonNull(lowerBasis, "lowerBasis");
			Objects.requireNonNull(bendDirection, "bendDirection");
			Objects.requireNonNull(reachStatus, "reachStatus");
		}
	}

	/** Small immutable vector type kept independent from Minecraft classes. */
	public record Vec3(double x, double y, double z) {
		public static final Vec3 UNIT_X = new Vec3(1.0, 0.0, 0.0);
		public static final Vec3 UNIT_Y = new Vec3(0.0, 1.0, 0.0);
		public static final Vec3 UNIT_Z = new Vec3(0.0, 0.0, 1.0);
		public static final Vec3 FORWARD = new Vec3(0.0, 0.0, -1.0);

		public Vec3 {
			if (!Double.isFinite(x) || !Double.isFinite(y) || !Double.isFinite(z)) {
				throw new IllegalArgumentException("IK vector components must be finite");
			}
		}

		public Vec3 add(Vec3 other) {
			Objects.requireNonNull(other, "other");
			return new Vec3(x + other.x, y + other.y, z + other.z);
		}

		public Vec3 subtract(Vec3 other) {
			Objects.requireNonNull(other, "other");
			return new Vec3(x - other.x, y - other.y, z - other.z);
		}

		public Vec3 scale(double scalar) {
			if (!Double.isFinite(scalar)) {
				throw new IllegalArgumentException("IK vector scale must be finite");
			}
			return new Vec3(x * scalar, y * scalar, z * scalar);
		}

		public double dot(Vec3 other) {
			Objects.requireNonNull(other, "other");
			return x * other.x + y * other.y + z * other.z;
		}

		public Vec3 cross(Vec3 other) {
			Objects.requireNonNull(other, "other");
			return new Vec3(
					y * other.z - z * other.y,
					z * other.x - x * other.z,
					x * other.y - y * other.x);
		}

		public double length() {
			return Math.hypot(Math.hypot(x, y), z);
		}

		public Vec3 normalized() {
			double length = length();
			if (!Double.isFinite(length) || length <= DIRECTION_EPSILON) {
				throw new IllegalArgumentException("Cannot normalize a degenerate IK vector");
			}
			return scale(1.0 / length);
		}
	}
}
