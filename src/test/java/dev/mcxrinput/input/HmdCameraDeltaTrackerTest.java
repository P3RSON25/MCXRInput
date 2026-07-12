package dev.mcxrinput.input;

import dev.mcxrinput.protocol.HeadOrientation;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class HmdCameraDeltaTrackerTest {
	private static final float EPSILON = 0.001F;

	@Test
	void stationaryHmdDoesNotCounterExternalPlayerRotation() {
		HmdCameraDeltaTracker tracker = new HmdCameraDeltaTracker();
		HeadOrientation stationaryHmd = new HeadOrientation(25.0F, -8.0F);
		tracker.anchor(stationaryHmd);

		// Simulate vanilla or a server replacing the player's rotation while the
		// headset remains at precisely the same physical orientation.
		HmdCameraDeltaTracker.CameraUpdate update = tracker.update(
				stationaryHmd, 137.0F, 31.0F, 1.0, 1.0);

		assertFalse(update.hasMovement());
	}

	@Test
	void physicalDeltaIsAddedToCurrentPlayerRotation() {
		HmdCameraDeltaTracker tracker = new HmdCameraDeltaTracker();
		tracker.anchor(new HeadOrientation(10.0F, 5.0F));

		// The current player yaw deliberately has no relationship to the anchor.
		HmdCameraDeltaTracker.CameraUpdate update = tracker.update(
				new HeadOrientation(22.0F, 2.0F), 100.0F, 40.0F, 1.0, 1.0);

		assertTrue(update.hasMovement());
		assertEquals(112.0F, update.yawDegrees(), EPSILON);
		assertEquals(37.0F, update.pitchDegrees(), EPSILON);
	}

	@Test
	void resetRequiresAReferencePoseWithoutCatchUpMotion() {
		HmdCameraDeltaTracker tracker = new HmdCameraDeltaTracker();
		tracker.anchor(new HeadOrientation(0.0F, 0.0F));
		tracker.reset();

		HmdCameraDeltaTracker.CameraUpdate first = tracker.update(
				new HeadOrientation(90.0F, 40.0F), -30.0F, 12.0F, 1.0, 1.0);
		HmdCameraDeltaTracker.CameraUpdate second = tracker.update(
				new HeadOrientation(100.0F, 45.0F), -30.0F, 12.0F, 1.0, 1.0);

		assertFalse(first.hasMovement());
		assertTrue(second.hasMovement());
		assertEquals(-20.0F, second.yawDegrees(), EPSILON);
		assertEquals(17.0F, second.pitchDegrees(), EPSILON);
	}

	@Test
	void yawUsesShortestWrappedPhysicalDelta() {
		HmdCameraDeltaTracker tracker = new HmdCameraDeltaTracker();
		tracker.anchor(new HeadOrientation(179.0F, 0.0F));

		HmdCameraDeltaTracker.CameraUpdate update = tracker.update(
				new HeadOrientation(-179.0F, 0.0F), 50.0F, 0.0F, 1.0, 1.0);

		assertTrue(update.hasMovement());
		assertEquals(52.0F, update.yawDegrees(), EPSILON);
	}

	@Test
	void sensitivityCannotAmplifyPhysicalMotion() {
		HmdCameraDeltaTracker tracker = new HmdCameraDeltaTracker();
		tracker.anchor(new HeadOrientation(0.0F, 0.0F));

		HmdCameraDeltaTracker.CameraUpdate update = tracker.update(
				new HeadOrientation(20.0F, 10.0F), 0.0F, 0.0F, 4.0, 2.0);

		assertTrue(update.hasMovement());
		assertEquals(20.0F, update.yawDegrees(), EPSILON);
		assertEquals(10.0F, update.pitchDegrees(), EPSILON);
	}

	@Test
	void resultingPlayerPitchIsClamped() {
		HmdCameraDeltaTracker tracker = new HmdCameraDeltaTracker();
		tracker.anchor(new HeadOrientation(0.0F, 0.0F));

		HmdCameraDeltaTracker.CameraUpdate update = tracker.update(
				new HeadOrientation(0.0F, 20.0F), 0.0F, 85.0F, 1.0, 1.0);

		assertTrue(update.hasMovement());
		assertEquals(90.0F, update.pitchDegrees(), EPSILON);
	}
}
