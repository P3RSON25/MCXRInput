package dev.mcxrinput.presentation;

import org.junit.jupiter.api.Test;

import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.UnknownHostException;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class PresentationEndpointPolicyTest {
	private static final long MAXIMUM_AGE = 500_000_000L;

	@Test
	void freshOwnerRejectsAnotherEndpointUntilItsOfferExpires()
			throws UnknownHostException {
		InetAddress loopback = InetAddress.getByName("127.0.0.1");
		InetSocketAddress first = new InetSocketAddress(loopback, 30_001);
		InetSocketAddress second = new InetSocketAddress(loopback, 30_002);
		long receivedAt = 1_000_000_000L;

		assertTrue(PresentationEndpointPolicy.mayAcceptOffer(
				null, first, receivedAt, receivedAt, MAXIMUM_AGE));
		assertTrue(PresentationEndpointPolicy.mayAcceptOffer(
				first, first, receivedAt,
				receivedAt + MAXIMUM_AGE, MAXIMUM_AGE));
		assertFalse(PresentationEndpointPolicy.mayAcceptOffer(
				first, second, receivedAt,
				receivedAt + MAXIMUM_AGE, MAXIMUM_AGE));
		assertTrue(PresentationEndpointPolicy.mayAcceptOffer(
				first, second, receivedAt,
				receivedAt + MAXIMUM_AGE + 1L, MAXIMUM_AGE));
	}

	@Test
	void backwardsOrInvalidFreshnessFailsClosedForTakeover()
			throws UnknownHostException {
		InetAddress loopback = InetAddress.getByName("127.0.0.1");
		InetSocketAddress first = new InetSocketAddress(loopback, 30_001);
		InetSocketAddress second = new InetSocketAddress(loopback, 30_002);

		assertFalse(PresentationEndpointPolicy.mayAcceptOffer(
				first, second, 200L, 100L, MAXIMUM_AGE));
		assertFalse(PresentationEndpointPolicy.mayAcceptOffer(
				first, second, 100L, 200L, -1L));
	}
}
