package dev.mcxrinput.hud;

import dev.mcxrinput.hud.HotbarSafeAreaTransform.Transform;
import dev.mcxrinput.presentation.PresentationOffer;
import org.joml.Matrix3x2fStack;
import org.joml.Vector2f;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;

class HotbarSafeAreaTransformTest {
	private static final double EPSILON = 0.0001;

	@Test
	void observedImmersiveCropContainsBothHotbarEdges() {
		int guiWidth = 320;
		int guiHeight = 180;
		double horizontalInset = 0.305;
		Transform transform = HotbarSafeAreaTransform.calculate(
				guiWidth, guiHeight, horizontalInset, 0.09);

		double safeLeft = guiWidth * horizontalInset;
		double safeRight = guiWidth * (1.0 - horizontalInset);
		double center = guiWidth * 0.5;
		assertEquals(safeLeft, transform.applyX(center - 120.0), EPSILON);
		assertEquals(safeRight, transform.applyX(center + 120.0), EPSILON);
		assertEquals(0.52, transform.scale(), EPSILON);
	}

	@Test
	void hotbarAlreadyInsideSafeWidthKeepsVanillaScale() {
		Transform transform = HotbarSafeAreaTransform.calculate(1000, 500, 0.31, 0.09);

		assertEquals(1.0, transform.scale(), EPSILON);
		assertEquals(0.0, transform.translationX(), EPSILON);
		assertEquals(-45.0, transform.translationY(), EPSILON);
	}

	@Test
	void uniformScaleKeepsCenterAndTranslatedBottomAnchored() {
		int guiWidth = 320;
		int guiHeight = 180;
		Transform transform = HotbarSafeAreaTransform.calculate(
				guiWidth, guiHeight, 0.305, 0.09);

		assertEquals(guiWidth * 0.5, transform.applyX(guiWidth * 0.5), EPSILON);
		assertEquals(guiHeight * 0.91, transform.applyY(guiHeight), EPSILON);
		assertEquals(transform.scale(),
				transform.applyX(161.0) - transform.applyX(160.0), EPSILON);
		assertEquals(transform.scale(),
				transform.applyY(101.0) - transform.applyY(100.0), EPSILON);
	}

	@Test
	void jomlOperationOrderMatchesTheAffineTransform() {
		Transform transform = HotbarSafeAreaTransform.calculate(320, 180, 0.305, 0.09);
		Matrix3x2fStack pose = new Matrix3x2fStack(2);
		pose.translate((float) transform.translationX(), (float) transform.translationY());
		pose.scale((float) transform.scale(), (float) transform.scale());

		Vector2f transformed = pose.transformPosition(new Vector2f(40.0F, 160.0F));
		assertEquals(transform.applyX(40.0), transformed.x, EPSILON);
		assertEquals(transform.applyY(160.0), transformed.y, EPSILON);
	}

	@Test
	void manualAndAutomaticSettingsProduceTheSameFit() {
		PresentationOffer offer = new PresentationOffer(
				"0123456789abcdef", 1L, 110_000, 305, 90);
		HudSafeAreaSettings.Settings manual = HudSafeAreaSettings.resolve(
				true, 0.305, 0.09, true, offer);
		HudSafeAreaSettings.Settings automatic = HudSafeAreaSettings.resolve(
				false, 0.0, 0.0, true, offer);

		Transform manualTransform = HotbarSafeAreaTransform.calculate(
				320, 180, manual.horizontalInset(), manual.verticalInset());
		Transform automaticTransform = HotbarSafeAreaTransform.calculate(
				320, 180, automatic.horizontalInset(), automatic.verticalInset());
		assertEquals(manualTransform, automaticTransform);
	}

	@Test
	void invalidDimensionsFailClosedToIdentity() {
		assertSame(HotbarSafeAreaTransform.IDENTITY,
				HotbarSafeAreaTransform.calculate(0, 180, 0.305, 0.09));
		assertSame(HotbarSafeAreaTransform.IDENTITY,
				HotbarSafeAreaTransform.calculate(320, -1, 0.305, 0.09));
	}
}
