package dev.mcxrinput.presentation;

import java.net.InetSocketAddress;
import java.util.Objects;

/**
 * Endpoint-ownership policy for the display-only coordination channel.
 * A fresh bridge keeps exclusive ownership even if another loopback process
 * invents a new session token. Takeover becomes possible only after expiry.
 */
public final class PresentationEndpointPolicy {
	private PresentationEndpointPolicy() {
	}

	public static boolean mayAcceptOffer(
			InetSocketAddress currentEndpoint,
			InetSocketAddress candidateEndpoint,
			long currentOfferReceivedAtNanos,
			long nowNanos,
			long maximumAgeNanos) {
		Objects.requireNonNull(candidateEndpoint, "candidateEndpoint");
		if (currentEndpoint == null || currentEndpoint.equals(candidateEndpoint)) {
			return true;
		}
		if (maximumAgeNanos < 0L) {
			return false;
		}
		long age = nowNanos - currentOfferReceivedAtNanos;
		return age >= 0L && age > maximumAgeNanos;
	}
}
