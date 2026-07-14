package dev.mcxrinput.protocol;

import java.util.Objects;

/**
 * One validated bridge frame received over localhost UDP. Protocol provenance
 * is retained so development-only v1 input can be rejected in multiplayer.
 */
public record VrInputFrame(
		int protocolVersion,
		VrPose hmd,
		VrControllerState leftController,
		VrControllerState rightController,
		VrTrackedPose leftGripPose,
		VrTrackedPose rightGripPose,
		long receivedAtNanos
) {
	public VrInputFrame {
		Objects.requireNonNull(hmd, "hmd");
		Objects.requireNonNull(leftController, "leftController");
		Objects.requireNonNull(rightController, "rightController");
		Objects.requireNonNull(leftGripPose, "leftGripPose");
		Objects.requireNonNull(rightGripPose, "rightGripPose");
	}

	/** Preserves the pre-grip-pose construction shape for isolated callers. */
	public VrInputFrame(
			int protocolVersion,
			VrPose hmd,
			VrControllerState leftController,
			VrControllerState rightController,
			long receivedAtNanos
	) {
		this(
				protocolVersion,
				hmd,
				leftController,
				rightController,
				VrTrackedPose.INACTIVE,
				VrTrackedPose.INACTIVE,
				receivedAtNanos
		);
	}
}
