package dev.mcxrinput.protocol;

/**
 * A validated HMD pose received from the local bridge.
 *
 * <p>The bridge timestamp is informational. Freshness uses the receiver's own
 * monotonic clock, so changing the system clock or a bridge clock mismatch
 * cannot keep old input alive.</p>
 */
public record VrPose(
		double x,
		double y,
		double z,
		double w,
		long bridgeTimestamp,
		long receivedAtNanos,
		boolean active
) {
}
