package dev.mcxrinput.presentation;

/**
 * Issues an independent strictly increasing unsigned sequence per bridge
 * session. Its receiver owner supplies synchronization.
 */
public final class PresentationStateSequencer {
	private String session;
	private long nextSequence = 1L;
	private boolean exhausted;

	public long next(String requestedSession) {
		if (!PresentationOffer.isSessionToken(requestedSession)) {
			throw new IllegalArgumentException("Invalid presentation session");
		}
		if (session == null || !session.equalsIgnoreCase(requestedSession)) {
			session = requestedSession;
			nextSequence = 1L;
			exhausted = false;
		}
		if (exhausted) {
			throw new IllegalStateException("Presentation sequence exhausted");
		}

		long issued = nextSequence;
		if (issued == -1L) {
			exhausted = true;
		} else {
			nextSequence = issued + 1L;
		}
		return issued;
	}
}
