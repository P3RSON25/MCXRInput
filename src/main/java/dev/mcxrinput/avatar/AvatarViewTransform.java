package dev.mcxrinput.avatar;

/**
 * Pure camera-space compensation for native immersive world minification.
 * Minecraft renders the source image before the native bridge scales its
 * tangent-space world view, so cosmetic geometry must use the inverse scale in
 * the camera plane to retain its physical angular position after projection.
 */
public final class AvatarViewTransform {
	public static final float MIN_WORLD_VIEW_SCALE = 0.30F;
	public static final float MAX_WORLD_VIEW_SCALE = 1.0F;

	private AvatarViewTransform() {
	}

	public static Point compensateCameraPoint(
			double x, double y, double z, float worldViewScale) {
		if (!Double.isFinite(x) || !Double.isFinite(y) || !Double.isFinite(z)
				|| !Float.isFinite(worldViewScale)
				|| worldViewScale < MIN_WORLD_VIEW_SCALE
				|| worldViewScale > MAX_WORLD_VIEW_SCALE) {
			throw new IllegalArgumentException("Invalid avatar projection calibration");
		}
		double inverseScale = 1.0 / worldViewScale;
		return new Point(x * inverseScale, y * inverseScale, z);
	}

	public record Point(double x, double y, double z) {
		public Point {
			if (!Double.isFinite(x) || !Double.isFinite(y) || !Double.isFinite(z)) {
				throw new IllegalArgumentException("Avatar point must be finite");
			}
		}
	}
}
