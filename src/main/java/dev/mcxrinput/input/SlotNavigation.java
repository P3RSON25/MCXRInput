package dev.mcxrinput.input;

import java.util.List;

/** Geometry-only directional navigation for irregular container slot layouts. */
public final class SlotNavigation {
	private SlotNavigation() {
	}

	public static int findNearest(List<Point> points, double x, double y) {
		if (points.isEmpty()) {
			return -1;
		}
		int bestIndex = 0;
		double bestDistance = Double.POSITIVE_INFINITY;
		for (int index = 0; index < points.size(); index++) {
			Point point = points.get(index);
			double dx = point.x - x;
			double dy = point.y - y;
			double distance = dx * dx + dy * dy;
			if (distance < bestDistance) {
				bestDistance = distance;
				bestIndex = index;
			}
		}
		return bestIndex;
	}

	public static int findNext(
			List<Point> points,
			int currentIndex,
			StickDpadRepeater.Direction direction
	) {
		if (points.isEmpty()) {
			return -1;
		}
		if (currentIndex < 0 || currentIndex >= points.size()) {
			return 0;
		}

		Point current = points.get(currentIndex);
		int bestIndex = -1;
		double bestScore = Double.POSITIVE_INFINITY;
		for (int index = 0; index < points.size(); index++) {
			if (index == currentIndex) {
				continue;
			}
			Point candidate = points.get(index);
			double dx = candidate.x - current.x;
			double dy = candidate.y - current.y;
			double primary = primaryDistance(dx, dy, direction);
			if (primary <= 0.0) {
				continue;
			}
			double deviation = orthogonalDistance(dx, dy, direction);
			double score = primary + deviation * 4.0;
			if (score < bestScore) {
				bestScore = score;
				bestIndex = index;
			}
		}

		return bestIndex >= 0 ? bestIndex : findWrapped(points, currentIndex, direction);
	}

	private static int findWrapped(
			List<Point> points,
			int currentIndex,
			StickDpadRepeater.Direction direction
	) {
		Point current = points.get(currentIndex);
		double extreme = switch (direction) {
			case RIGHT, DOWN -> Double.POSITIVE_INFINITY;
			case LEFT, UP -> Double.NEGATIVE_INFINITY;
		};

		for (int index = 0; index < points.size(); index++) {
			if (index == currentIndex) {
				continue;
			}
			Point point = points.get(index);
			double axis = axis(point, direction);
			extreme = switch (direction) {
				case RIGHT, DOWN -> Math.min(extreme, axis);
				case LEFT, UP -> Math.max(extreme, axis);
			};
		}

		if (!Double.isFinite(extreme)) {
			return currentIndex;
		}

		int bestIndex = currentIndex;
		double bestDeviation = Double.POSITIVE_INFINITY;
		for (int index = 0; index < points.size(); index++) {
			if (index == currentIndex) {
				continue;
			}
			Point point = points.get(index);
			if (Double.compare(axis(point, direction), extreme) != 0) {
				continue;
			}
			double deviation = switch (direction) {
				case LEFT, RIGHT -> Math.abs(point.y - current.y);
				case UP, DOWN -> Math.abs(point.x - current.x);
			};
			if (deviation < bestDeviation) {
				bestDeviation = deviation;
				bestIndex = index;
			}
		}
		return bestIndex;
	}

	private static double primaryDistance(
			double dx, double dy, StickDpadRepeater.Direction direction
	) {
		return switch (direction) {
			case UP -> -dy;
			case DOWN -> dy;
			case LEFT -> -dx;
			case RIGHT -> dx;
		};
	}

	private static double orthogonalDistance(
			double dx, double dy, StickDpadRepeater.Direction direction
	) {
		return switch (direction) {
			case UP, DOWN -> Math.abs(dx);
			case LEFT, RIGHT -> Math.abs(dy);
		};
	}

	private static double axis(Point point, StickDpadRepeater.Direction direction) {
		return switch (direction) {
			case LEFT, RIGHT -> point.x;
			case UP, DOWN -> point.y;
		};
	}

	public record Point(int x, int y) {
	}
}
