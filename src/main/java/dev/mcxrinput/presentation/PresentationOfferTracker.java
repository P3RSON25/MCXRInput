package dev.mcxrinput.presentation;

/**
 * Monotonic-time freshness and revision ordering for one bridge presentation
 * session. Network endpoint ownership and synchronization remain with the
 * loopback receiver.
 */
public final class PresentationOfferTracker {
	private Snapshot latest;

	public boolean accept(PresentationOffer offer, long receivedAtNanos) {
		if (offer == null) {
			return false;
		}
		if (latest != null && offer.sameSession(latest.offer())) {
			int revisionOrder = Long.compareUnsigned(offer.revision(), latest.offer().revision());
			if (revisionOrder < 0) {
				return false;
			}
			if (revisionOrder == 0) {
				if (!samePayload(offer, latest.offer())) {
					return false;
				}
				// Preserve the originally accepted spelling of the session token;
				// an identical heartbeat refreshes time, not calibration identity.
				latest = new Snapshot(latest.offer(), receivedAtNanos);
				return true;
			}
		}
		latest = new Snapshot(offer, receivedAtNanos);
		return true;
	}

	public Snapshot latest() {
		return latest;
	}

	public Snapshot latestFresh(long nowNanos, long maximumAgeNanos) {
		if (latest == null || maximumAgeNanos < 0) {
			return null;
		}
		long age = nowNanos - latest.receivedAtNanos();
		return age >= 0 && age <= maximumAgeNanos ? latest : null;
	}

	public record Snapshot(PresentationOffer offer, long receivedAtNanos) {
		public Snapshot {
			if (offer == null) {
				throw new IllegalArgumentException("Presentation offer is required");
			}
		}
	}

	private static boolean samePayload(PresentationOffer first, PresentationOffer second) {
		return first.sourceFovMilliDegrees() == second.sourceFovMilliDegrees()
				&& first.hudHorizontalPermille() == second.hudHorizontalPermille()
				&& first.hudVerticalPermille() == second.hudVerticalPermille();
	}
}
