package dev.mcxrinput.hud;

/**
 * Pure affine transform for fitting Minecraft 26.2's vanilla bottom-center
 * gameplay/status group inside a centered HUD safe area. The hotbar, selector,
 * offhand slot, and attack indicator are the widest supported members at 120
 * GUI pixels from screen center, hence the conservative 240-pixel group width.
 * Applying one transform to every isolated layer preserves their vanilla scale
 * and spacing relative to each other.
 */
public final class BottomCenterHudTransform {
	public static final double VANILLA_GROUP_MAX_WIDTH = 240.0;
	public static final Transform IDENTITY = new Transform(1.0, 0.0, 0.0);

	private BottomCenterHudTransform() {
	}

	/**
	 * Uniformly scales around the original bottom-center anchor, then places that
	 * anchor at the vertically inset bottom-center point. Invalid GUI dimensions
	 * fail closed to the identity transform.
	 */
	public static Transform calculate(
			int guiWidth,
			int guiHeight,
			double horizontalInset,
			double verticalInset) {
		if (guiWidth <= 0 || guiHeight <= 0) {
			return IDENTITY;
		}

		double safeHorizontalInset = HudSafeAreaOffsets.sanitizeInset(
				horizontalInset, HudSafeAreaOffsets.MIN_INSET);
		double safeVerticalInset = HudSafeAreaOffsets.sanitizeInset(
				verticalInset, HudSafeAreaOffsets.MIN_INSET);
		double safeWidth = guiWidth * (1.0 - 2.0 * safeHorizontalInset);
		double scale = Math.min(1.0, safeWidth / VANILLA_GROUP_MAX_WIDTH);
		if (!Double.isFinite(scale) || scale <= 0.0) {
			return IDENTITY;
		}

		double centerX = guiWidth * 0.5;
		double translationX = centerX * (1.0 - scale);
		double translationY = guiHeight * (1.0 - scale - safeVerticalInset);
		if (!Double.isFinite(translationX) || !Double.isFinite(translationY)) {
			return IDENTITY;
		}
		return new Transform(scale, translationX, translationY);
	}

	/** Maps an original GUI point as {@code output = scale * input + translation}. */
	public record Transform(double scale, double translationX, double translationY) {
		public double applyX(double x) {
			return scale * x + translationX;
		}

		public double applyY(double y) {
			return scale * y + translationY;
		}
	}
}
