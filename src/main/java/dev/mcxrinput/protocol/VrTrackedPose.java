package dev.mcxrinput.protocol;

/**
 * One validated controller grip pose in the bridge's HMD-relative space.
 *
 * <p>Active instances always contain finite coordinates, a position no more
 * than four metres from the origin, and a normalized rotation quaternion. An
 * inactive value is used instead of {@code null} when tracking is unavailable
 * or any source field fails validation.</p>
 */
public record VrTrackedPose(
		boolean active,
		double positionX,
		double positionY,
		double positionZ,
		double rotationX,
		double rotationY,
		double rotationZ,
		double rotationW
) {
	public static final double MAX_POSITION_RADIUS_METRES = 4.0;
	public static final VrTrackedPose INACTIVE = new VrTrackedPose(
			false,
			0.0, 0.0, 0.0,
			0.0, 0.0, 0.0, 1.0
	);

	public VrTrackedPose {
		if (!Double.isFinite(positionX) || !Double.isFinite(positionY)
				|| !Double.isFinite(positionZ)) {
			throw new IllegalArgumentException("Tracked-pose position must be finite");
		}
		if (Math.hypot(Math.hypot(positionX, positionY), positionZ)
				> MAX_POSITION_RADIUS_METRES) {
			throw new IllegalArgumentException("Tracked-pose position is outside the plausible radius");
		}
		if (!PoseMath.isPlausibleQuaternion(rotationX, rotationY, rotationZ, rotationW)) {
			throw new IllegalArgumentException("Tracked-pose rotation is not a plausible quaternion");
		}

		double inverseNorm = 1.0 / Math.sqrt(
				rotationX * rotationX + rotationY * rotationY
						+ rotationZ * rotationZ + rotationW * rotationW);
		rotationX *= inverseNorm;
		rotationY *= inverseNorm;
		rotationZ *= inverseNorm;
		rotationW *= inverseNorm;
	}
}
