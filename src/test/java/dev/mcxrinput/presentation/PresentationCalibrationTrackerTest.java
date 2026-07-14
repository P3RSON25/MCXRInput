package dev.mcxrinput.presentation;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

class PresentationCalibrationTrackerTest {
	private static final long MAXIMUM_AGE = 500_000_000L;
	private static final PresentationOffer OFFER = new PresentationOffer(
			"0123456789abcdef", 7L, 150_000, 300, 90);

	@Test
	void returnsOnlyFreshCalibrationMatchingTheOfferIdentity() {
		PresentationCalibrationTracker tracker = new PresentationCalibrationTracker();
		PresentationCalibration calibration = calibration(
				"0123456789abcdef", 7L, 400);
		assertTrue(tracker.accept(calibration, 1_000_000_000L));
		assertSame(calibration, tracker.latestFreshMatching(
				OFFER, 1_500_000_000L, MAXIMUM_AGE).calibration());
		assertNull(tracker.latestFreshMatching(
				OFFER, 1_500_000_001L, MAXIMUM_AGE));
		assertNull(tracker.latestFreshMatching(
				new PresentationOffer("0123456789abcdef", 8L, 150_000, 300, 90),
				1_100_000_000L, MAXIMUM_AGE));
		assertNull(tracker.latestFreshMatching(
				new PresentationOffer("fedcba9876543210", 7L, 150_000, 300, 90),
				1_100_000_000L, MAXIMUM_AGE));
	}

	@Test
	void identicalHeartbeatRefreshesButCannotMutateOneRevision() {
		PresentationCalibrationTracker tracker = new PresentationCalibrationTracker();
		PresentationCalibration original = calibration(
				"0123456789abcdef", 7L, 400);
		assertTrue(tracker.accept(original, 100L));
		assertTrue(tracker.accept(calibration(
				"0123456789ABCDEF", 7L, 400), 200L));
		assertSame(original, tracker.latest().calibration());
		assertEquals(200L, tracker.latest().receivedAtNanos());

		assertFalse(tracker.accept(calibration(
				"0123456789abcdef", 7L, 401), 300L));
		assertSame(original, tracker.latest().calibration());
		assertEquals(200L, tracker.latest().receivedAtNanos());
	}

	@Test
	void revisionsAreOrderedAsUnsignedAndNewSessionCanReplaceOld() {
		PresentationCalibrationTracker tracker = new PresentationCalibrationTracker();
		assertTrue(tracker.accept(calibration(
				"0123456789abcdef", -1L, 400), 100L));
		assertFalse(tracker.accept(calibration(
				"0123456789abcdef", Long.MAX_VALUE, 400), 200L));
		assertTrue(tracker.accept(calibration(
				"fedcba9876543210", 0L, 750), 300L));
		assertEquals(0.75F, tracker.latest().calibration().worldViewScale());
	}

	private static PresentationCalibration calibration(
			String session, long revision, int scalePermille) {
		return new PresentationCalibration(session, revision, scalePermille);
	}
}
