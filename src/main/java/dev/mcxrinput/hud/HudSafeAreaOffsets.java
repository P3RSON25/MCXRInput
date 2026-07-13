package dev.mcxrinput.hud;

/**
 * Pure normalized-inset math for moving edge-anchored HUD elements inward.
 * The result is translation-only: callers must not scale or otherwise distort
 * the element being moved.
 */
public final class HudSafeAreaOffsets {
	public static final double MIN_INSET = 0.0;
	public static final double MAX_INSET = 0.45;

	private HudSafeAreaOffsets() {
	}

	public static double sanitizeInset(double value, double fallback) {
		double safeFallback = Double.isFinite(fallback) ? fallback : MIN_INSET;
		double candidate = Double.isFinite(value) ? value : safeFallback;
		return Math.max(MIN_INSET, Math.min(MAX_INSET, candidate));
	}

	public static Offset calculate(
			int guiWidth,
			int guiHeight,
			double horizontalInset,
			double verticalInset,
			HorizontalAnchor horizontalAnchor,
			VerticalAnchor verticalAnchor) {
		if (guiWidth <= 0 || guiHeight <= 0
				|| horizontalAnchor == null || verticalAnchor == null) {
			return Offset.NONE;
		}

		double horizontalPixels = guiWidth * sanitizeInset(horizontalInset, MIN_INSET);
		double verticalPixels = guiHeight * sanitizeInset(verticalInset, MIN_INSET);
		double x = switch (horizontalAnchor) {
			case LEFT -> horizontalPixels;
			case CENTER -> 0.0;
			case RIGHT -> -horizontalPixels;
		};
		double y = switch (verticalAnchor) {
			case TOP -> verticalPixels;
			case CENTER -> 0.0;
			case BOTTOM -> -verticalPixels;
		};
		return new Offset(x, y);
	}

	public enum HorizontalAnchor {
		LEFT,
		CENTER,
		RIGHT
	}

	public enum VerticalAnchor {
		TOP,
		CENTER,
		BOTTOM
	}

	public record Offset(double x, double y) {
		public static final Offset NONE = new Offset(0.0, 0.0);
	}
}
