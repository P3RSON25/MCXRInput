package dev.mcxrinput.presentation;

import java.util.Objects;

/**
 * Immutable display-only calibration accompanying one presentation offer.
 * The scale is transmitted as an integer so Java and the native bridge agree
 * on the exact tangent-space minification applied to the captured world.
 */
public record PresentationCalibration(
		String session,
		long revision,
		int worldViewScalePermille) {
	public static final int MIN_WORLD_VIEW_SCALE_PERMILLE = 300;
	public static final int MAX_WORLD_VIEW_SCALE_PERMILLE = 1_000;

	public PresentationCalibration {
		Objects.requireNonNull(session, "session");
		if (!PresentationOffer.isSessionToken(session)) {
			throw new IllegalArgumentException(
					"Presentation session must contain exactly 16 hexadecimal digits");
		}
		if (worldViewScalePermille < MIN_WORLD_VIEW_SCALE_PERMILLE
				|| worldViewScalePermille > MAX_WORLD_VIEW_SCALE_PERMILLE) {
			throw new IllegalArgumentException(
					"Presentation world-view scale is outside the supported range");
		}
	}

	public float worldViewScale() {
		return worldViewScalePermille / 1_000.0F;
	}

	public boolean matches(PresentationOffer offer) {
		return offer != null
				&& session.equalsIgnoreCase(offer.session())
				&& revision == offer.revision();
	}
}
