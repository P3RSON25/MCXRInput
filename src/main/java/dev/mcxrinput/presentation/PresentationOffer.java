package dev.mcxrinput.presentation;

import java.util.Objects;

/**
 * Immutable display-only offer from the native bridge. Values use integers on
 * the wire so both sides can acknowledge one exact projection calibration.
 */
public record PresentationOffer(
		String session,
		long revision,
		int sourceFovMilliDegrees,
		int hudHorizontalPermille,
		int hudVerticalPermille) {
	public static final int MIN_SOURCE_FOV_MILLIDEGREES = 30_000;
	public static final int MAX_SOURCE_FOV_MILLIDEGREES = 160_000;
	public static final int MIN_HUD_INSET_PERMILLE = 0;
	public static final int MAX_HUD_INSET_PERMILLE = 450;

	public PresentationOffer {
		Objects.requireNonNull(session, "session");
		if (!isSessionToken(session)) {
			throw new IllegalArgumentException("Presentation session must contain exactly 16 hexadecimal digits");
		}
		if (sourceFovMilliDegrees < MIN_SOURCE_FOV_MILLIDEGREES
				|| sourceFovMilliDegrees > MAX_SOURCE_FOV_MILLIDEGREES) {
			throw new IllegalArgumentException("Presentation source FOV is outside the supported range");
		}
		if (hudHorizontalPermille < MIN_HUD_INSET_PERMILLE
				|| hudHorizontalPermille > MAX_HUD_INSET_PERMILLE
				|| hudVerticalPermille < MIN_HUD_INSET_PERMILLE
				|| hudVerticalPermille > MAX_HUD_INSET_PERMILLE) {
			throw new IllegalArgumentException("Presentation HUD inset is outside the supported range");
		}
	}

	public float sourceFovDegrees() {
		return sourceFovMilliDegrees / 1000.0F;
	}

	public double hudHorizontalInset() {
		return hudHorizontalPermille / 1000.0;
	}

	public double hudVerticalInset() {
		return hudVerticalPermille / 1000.0;
	}

	public boolean sameSession(PresentationOffer other) {
		return other != null && session.equalsIgnoreCase(other.session);
	}

	public static boolean isSessionToken(String value) {
		if (value == null || value.length() != 16) {
			return false;
		}
		for (int index = 0; index < value.length(); index++) {
			char character = value.charAt(index);
			if (!((character >= '0' && character <= '9')
					|| (character >= 'a' && character <= 'f')
					|| (character >= 'A' && character <= 'F'))) {
				return false;
			}
		}
		return true;
	}
}
