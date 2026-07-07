package dev.mcxrinput.protocol;

public final class PoseMath {
	private static final double MIN_QUATERNION_NORM_SQUARED = 0.25;
	private static final double MAX_QUATERNION_NORM_SQUARED = 2.25;

	private PoseMath() {
	}

	public static boolean isPlausibleQuaternion(double x, double y, double z, double w) {
		if (!Double.isFinite(x) || !Double.isFinite(y) || !Double.isFinite(z) || !Double.isFinite(w)) {
			return false;
		}

		double normSquared = x * x + y * y + z * z + w * w;
		return normSquared >= MIN_QUATERNION_NORM_SQUARED
				&& normSquared <= MAX_QUATERNION_NORM_SQUARED;
	}

	/**
	 * Converts an OpenXR orientation quaternion to relative yaw and pitch.
	 * OpenXR looks down -Z at identity; Minecraft pitch is negative when looking up.
	 */
	public static HeadOrientation toHeadOrientation(double x, double y, double z, double w) {
		if (!isPlausibleQuaternion(x, y, z, w)) {
			throw new IllegalArgumentException("Quaternion is non-finite or has an implausible norm");
		}

		double inverseNorm = 1.0 / Math.sqrt(x * x + y * y + z * z + w * w);
		x *= inverseNorm;
		y *= inverseNorm;
		z *= inverseNorm;
		w *= inverseNorm;

		// Rotate OpenXR's forward vector (0, 0, -1) by the normalized quaternion.
		double forwardX = -2.0 * (x * z + w * y);
		double forwardY = -2.0 * (y * z - w * x);
		double forwardZ = -(1.0 - 2.0 * (x * x + y * y));

		// Minecraft's local yaw sign is opposite the OpenXR forward-vector yaw
		// observed through SteamVR, so invert the horizontal angle here. This keeps
		// real head turns 1:1 while making left/right motion match in-game motion.
		double yaw = Math.toDegrees(Math.atan2(forwardX, -forwardZ));
		double pitch = -Math.toDegrees(Math.asin(clamp(forwardY, -1.0, 1.0)));
		return new HeadOrientation((float) yaw, (float) pitch);
	}

	public static float wrapDegrees(float degrees) {
		float wrapped = degrees % 360.0F;
		if (wrapped >= 180.0F) {
			wrapped -= 360.0F;
		}
		if (wrapped < -180.0F) {
			wrapped += 360.0F;
		}
		return wrapped;
	}

	public static float clampPitch(float pitch) {
		return Math.max(-90.0F, Math.min(90.0F, pitch));
	}

	private static double clamp(double value, double minimum, double maximum) {
		return Math.max(minimum, Math.min(maximum, value));
	}
}
