package dev.mcxrinput.presentation;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

class PresentationOfferTrackerTest {
	private static final long MAXIMUM_AGE = 500_000_000L;

	@Test
	void offerIsFreshThroughFiveHundredMillisecondsOnly() {
		PresentationOfferTracker tracker = new PresentationOfferTracker();
		PresentationOffer offer = offer("0123456789abcdef", 1L);
		assertTrue(tracker.accept(offer, 1_000_000_000L));
		assertSame(offer, tracker.latestFresh(1_500_000_000L, MAXIMUM_AGE).offer());
		assertNull(tracker.latestFresh(1_500_000_001L, MAXIMUM_AGE));
		assertNull(tracker.latestFresh(999_999_999L, MAXIMUM_AGE));
	}

	@Test
	void sameRevisionRefreshesButOlderRevisionCannotRollBackOrRefresh() {
		PresentationOfferTracker tracker = new PresentationOfferTracker();
		PresentationOffer revisionTwo = offer("0123456789abcdef", 2L);
		assertTrue(tracker.accept(revisionTwo, 100L));
		assertFalse(tracker.accept(offer("0123456789ABCDEF", 1L), 200L));
		assertEquals(100L, tracker.latest().receivedAtNanos());

		PresentationOffer repeated = offer("0123456789ABCDEF", 2L);
		assertTrue(tracker.accept(repeated, 300L));
		assertSame(revisionTwo, tracker.latest().offer());
		assertEquals(300L, tracker.latest().receivedAtNanos());
	}

	@Test
	void sameRevisionCannotMutateCalibrationWhileRefreshingFreshness() {
		PresentationOfferTracker tracker = new PresentationOfferTracker();
		PresentationOffer original = offer("0123456789abcdef", 4L);
		assertTrue(tracker.accept(original, 100L));
		PresentationOffer mutated = new PresentationOffer(
				"0123456789abcdef", 4L, 120_000, 100, 120);
		assertFalse(tracker.accept(mutated, 200L));
		assertSame(original, tracker.latest().offer());
		assertEquals(100L, tracker.latest().receivedAtNanos());
	}

	@Test
	void unsignedRevisionOrderingAndNewSessionsAreHandled() {
		PresentationOfferTracker tracker = new PresentationOfferTracker();
		assertTrue(tracker.accept(offer("0123456789abcdef", -1L), 100L));
		assertFalse(tracker.accept(offer("0123456789abcdef", Long.MAX_VALUE), 200L));
		assertTrue(tracker.accept(offer("fedcba9876543210", 0L), 300L));
		assertEquals("fedcba9876543210", tracker.latest().offer().session());
	}

	private static PresentationOffer offer(String session, long revision) {
		return new PresentationOffer(session, revision, 110_000, 60, 90);
	}
}
