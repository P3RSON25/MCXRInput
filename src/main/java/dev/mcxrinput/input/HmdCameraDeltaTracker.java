package dev.mcxrinput.input;

import dev.mcxrinput.protocol.HeadOrientation;
import dev.mcxrinput.protocol.PoseMath;

import java.util.Objects;

/**
 * Converts successive physical HMD orientations into relative camera movement.
 *
 * <p>The current and previous player rotations are supplied for every update
 * deliberately. The current rotation keeps vanilla or server look changes
 * authoritative, while the previous rotation lets the caller shift both ends of
 * Minecraft's render interpolation by exactly the same accepted physical delta.</p>
 */
public final class HmdCameraDeltaTracker {
	private HeadOrientation previousHmdOrientation;

	public void reset() {
		previousHmdOrientation = null;
	}

	public void anchor(HeadOrientation orientation) {
		previousHmdOrientation = Objects.requireNonNull(orientation, "orientation");
	}

	public CameraUpdate update(
			HeadOrientation orientation,
			float currentPlayerYaw,
			float currentPlayerPitch,
			float previousPlayerYaw,
			float previousPlayerPitch,
			double yawSensitivity,
			double pitchSensitivity
	) {
		Objects.requireNonNull(orientation, "orientation");
		HeadOrientation previous = previousHmdOrientation;
		previousHmdOrientation = orientation;

		if (previous == null
				|| !Float.isFinite(currentPlayerYaw)
				|| !Float.isFinite(currentPlayerPitch)
				|| !Float.isFinite(previousPlayerYaw)
				|| !Float.isFinite(previousPlayerPitch)
				|| !isVanillaPitch(currentPlayerPitch)
				|| !isVanillaPitch(previousPlayerPitch)) {
			return CameraUpdate.NO_MOVEMENT;
		}

		float yawDelta = PoseMath.wrapDegrees(
				orientation.yawDegrees() - previous.yawDegrees());
		float pitchDelta = orientation.pitchDegrees() - previous.pitchDegrees();
		yawDelta *= safeSensitivity(yawSensitivity);
		pitchDelta *= safeSensitivity(pitchSensitivity);

		if (!Float.isFinite(yawDelta) || !Float.isFinite(pitchDelta)) {
			return CameraUpdate.NO_MOVEMENT;
		}

		// Bound the accepted pitch movement against both interpolation endpoints.
		// The caller can therefore add this exact value to current and previous
		// pitch without producing a hidden overshoot or catch-up near +/-90 degrees.
		float minimumPitchDelta = Math.max(
				-90.0F - currentPlayerPitch,
				-90.0F - previousPlayerPitch);
		float maximumPitchDelta = Math.min(
				90.0F - currentPlayerPitch,
				90.0F - previousPlayerPitch);
		pitchDelta = Math.max(minimumPitchDelta, Math.min(maximumPitchDelta, pitchDelta));
		if (yawDelta == 0.0F && pitchDelta == 0.0F) {
			return CameraUpdate.NO_MOVEMENT;
		}

		return new CameraUpdate(
				true,
				PoseMath.wrapDegrees(currentPlayerYaw + yawDelta),
				PoseMath.clampPitch(currentPlayerPitch + pitchDelta),
				yawDelta,
				pitchDelta
		);
	}

	private static boolean isVanillaPitch(float pitch) {
		return pitch >= -90.0F && pitch <= 90.0F;
	}

	private static float safeSensitivity(double sensitivity) {
		if (!Double.isFinite(sensitivity)) {
			return 0.0F;
		}
		// Camera input is intentionally never amplified beyond physical HMD motion.
		return (float) Math.max(0.0, Math.min(1.0, sensitivity));
	}

	public record CameraUpdate(
			boolean hasMovement,
			float yawDegrees,
			float pitchDegrees,
			float appliedYawDeltaDegrees,
			float appliedPitchDeltaDegrees
	) {
		private static final CameraUpdate NO_MOVEMENT =
				new CameraUpdate(false, 0.0F, 0.0F, 0.0F, 0.0F);
	}
}
