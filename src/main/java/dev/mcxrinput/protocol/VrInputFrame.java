package dev.mcxrinput.protocol;

/**
 * One validated bridge frame received over localhost UDP.
 */
public record VrInputFrame(
		VrPose hmd,
		VrControllerState leftController,
		VrControllerState rightController,
		long receivedAtNanos
) {
}
