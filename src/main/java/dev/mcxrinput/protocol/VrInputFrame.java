package dev.mcxrinput.protocol;

/**
 * One validated bridge frame received over localhost UDP. Protocol provenance
 * is retained so development-only v1 input can be rejected in multiplayer.
 */
public record VrInputFrame(
		int protocolVersion,
		VrPose hmd,
		VrControllerState leftController,
		VrControllerState rightController,
		long receivedAtNanos
) {
}
