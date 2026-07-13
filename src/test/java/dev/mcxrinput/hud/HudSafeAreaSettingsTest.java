package dev.mcxrinput.hud;

import dev.mcxrinput.presentation.PresentationOffer;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class HudSafeAreaSettingsTest {
	private static final double EPSILON = 0.0001;
	private static final PresentationOffer OFFER = new PresentationOffer(
			"0123456789abcdef", 1L, 110_000, 60, 90);

	@Test
	void manualSettingsOverrideFreshAutomaticRecommendation() {
		HudSafeAreaSettings.Settings settings = HudSafeAreaSettings.resolve(
				true, 0.31, 0.12, true, OFFER);
		assertTrue(settings.enabled());
		assertEquals(0.31, settings.horizontalInset(), EPSILON);
		assertEquals(0.12, settings.verticalInset(), EPSILON);
	}

	@Test
	void automaticSettingsUseOnlyProvidedFreshOffer() {
		HudSafeAreaSettings.Settings settings = HudSafeAreaSettings.resolve(
				false, 0.31, 0.12, true, OFFER);
		assertTrue(settings.enabled());
		assertEquals(0.06, settings.horizontalInset(), EPSILON);
		assertEquals(0.09, settings.verticalInset(), EPSILON);

		assertFalse(HudSafeAreaSettings.resolve(
				false, 0.31, 0.12, true, null).enabled());
		assertFalse(HudSafeAreaSettings.resolve(
				false, 0.31, 0.12, false, OFFER).enabled());
	}

	@Test
	void versionEightMigratesOnWhileVersionNinePreservesUserChoice() {
		assertTrue(HudSafeAreaSettings.migrateAutomaticEnabled(8, false));
		assertTrue(HudSafeAreaSettings.migrateAutomaticEnabled(9, true));
		assertFalse(HudSafeAreaSettings.migrateAutomaticEnabled(9, false));
	}
}
