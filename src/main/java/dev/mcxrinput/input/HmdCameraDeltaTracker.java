package dev.mcxrinput.input;

import dev.mcxrinput.protocol.HeadOrientation;
import dev.mcxrinput.protocol.PoseMath;

import java.util.Objects;

/**
 * Converts successive physical HMD orientations into relative camera movement.
 *
 * <p>The current player rotation is supplied for every update deliberately. This
 * prevents a stationary headset from restoring an old calibration target after
 * vanilla or a server changes the player's view.</p>
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
			double yawSensitivity,
			double pitchSensitivity
	) {
		Objects.requireNonNull(orientation, "orientation");
		HeadOrientation previous = previousHmdOrientation;
		previousHmdOrientation = orientation;

		if (previous == null
				|| !Float.isFinite(currentPlayerYaw)
				|| !Float.isFinite(currentPlayerPitch)) {
			return CameraUpdate.NO_MOVEMENT;
		}

		float yawDelta = PoseMath.wrapDegrees(
				orientation.yawDegrees() - previous.yawDegrees());
		float pitchDelta = orientation.pitchDegrees() - previous.pitchDegrees();
		yawDelta *= safeSensitivity(yawSensitivity);
		pitchDelta *= safeSensitivity(pitchSensitivity);

		if (!Float.isFinite(yawDelta) || !Float.isFinite(pitchDelta)
				|| (yawDelta == 0.0F && pitchDelta == 0.0F)) {
			return CameraUpdate.NO_MOVEMENT;
		}

		return new CameraUpdate(
				true,
				PoseMath.wrapDegrees(currentPlayerYaw + yawDelta),
				PoseMath.clampPitch(currentPlayerPitch + pitchDelta)
		);
	}

	private static float safeSensitivity(double sensitivity) {
		if (!Double.isFinite(sensitivity)) {
			return 0.0F;
		}
		// Camera input is intentionally never amplified beyond physical HMD motion.
		return (float) Math.max(0.0, Math.min(1.0, sensitivity));
	}

	public record CameraUpdate(boolean hasMovement, float yawDegrees, float pitchDegrees) {
		private static final CameraUpdate NO_MOVEMENT = new CameraUpdate(false, 0.0F, 0.0F);
	}
}
