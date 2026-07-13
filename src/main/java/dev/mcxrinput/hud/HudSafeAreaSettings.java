package dev.mcxrinput.hud;

import dev.mcxrinput.presentation.PresentationOffer;

/** Resolves manual HUD settings against an optional fresh bridge recommendation. */
public final class HudSafeAreaSettings {
	public static final int AUTOMATIC_CONFIG_VERSION = 9;
	public static final Settings DISABLED = new Settings(false, 0.0, 0.0);

	private HudSafeAreaSettings() {
	}

	public static Settings resolve(
			boolean manualEnabled,
			double manualHorizontalInset,
			double manualVerticalInset,
			boolean automaticEnabled,
			PresentationOffer freshOffer) {
		if (manualEnabled) {
			return new Settings(
					true,
					HudSafeAreaOffsets.sanitizeInset(manualHorizontalInset, HudSafeAreaOffsets.MIN_INSET),
					HudSafeAreaOffsets.sanitizeInset(manualVerticalInset, HudSafeAreaOffsets.MIN_INSET));
		}
		if (automaticEnabled && freshOffer != null) {
			return new Settings(
					true,
					HudSafeAreaOffsets.sanitizeInset(
							freshOffer.hudHorizontalInset(), HudSafeAreaOffsets.MIN_INSET),
					HudSafeAreaOffsets.sanitizeInset(
							freshOffer.hudVerticalInset(), HudSafeAreaOffsets.MIN_INSET));
		}
		return DISABLED;
	}

	/** Configs predating v9 had no automatic setting, so migration opts them in once. */
	public static boolean migrateAutomaticEnabled(int loadedConfigVersion, boolean storedValue) {
		return loadedConfigVersion < AUTOMATIC_CONFIG_VERSION || storedValue;
	}

	public record Settings(boolean enabled, double horizontalInset, double verticalInset) {
	}
}
