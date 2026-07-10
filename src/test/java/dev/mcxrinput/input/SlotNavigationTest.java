package dev.mcxrinput.input;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

class SlotNavigationTest {
	private static final List<SlotNavigation.Point> GRID = List.of(
			new SlotNavigation.Point(0, 0),
			new SlotNavigation.Point(18, 0),
			new SlotNavigation.Point(36, 0),
			new SlotNavigation.Point(0, 18),
			new SlotNavigation.Point(18, 18),
			new SlotNavigation.Point(36, 18)
	);

	@Test
	void findsNearestSlotToExistingPointer() {
		assertEquals(4, SlotNavigation.findNearest(GRID, 20, 20));
		assertEquals(-1, SlotNavigation.findNearest(List.of(), 0, 0));
	}

	@Test
	void followsTheGridInCardinalDirections() {
		assertEquals(1, SlotNavigation.findNext(GRID, 0, StickDpadRepeater.Direction.RIGHT));
		assertEquals(3, SlotNavigation.findNext(GRID, 0, StickDpadRepeater.Direction.DOWN));
		assertEquals(0, SlotNavigation.findNext(GRID, 3, StickDpadRepeater.Direction.UP));
	}

	@Test
	void wrapsToTheOppositeEdge() {
		assertEquals(0, SlotNavigation.findNext(GRID, 2, StickDpadRepeater.Direction.RIGHT));
		assertEquals(5, SlotNavigation.findNext(GRID, 2, StickDpadRepeater.Direction.UP));
	}

	@Test
	void stronglyPrefersAlignmentInIrregularLayouts() {
		List<SlotNavigation.Point> points = List.of(
				new SlotNavigation.Point(0, 0),
				new SlotNavigation.Point(20, 12),
				new SlotNavigation.Point(35, 1)
		);
		assertEquals(2, SlotNavigation.findNext(points, 0, StickDpadRepeater.Direction.RIGHT));
	}
}
