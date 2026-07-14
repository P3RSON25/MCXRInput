package dev.mcxrinput.protocol;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

class BridgeInputParserTest {
	private static final long RECEIVED_AT_NANOS = 123_456_789L;
	private static final double EPSILON = 1.0E-12;

	@Test
	void acceptsAndNormalizesValidGripPoses() {
		VrInputFrame frame = parseFrame(frameWithGripPoses(validGripPose(), validGripPose()));

		VrTrackedPose left = frame.leftGripPose();
		assertTrue(left.active());
		assertEquals(0.25, left.positionX(), EPSILON);
		assertEquals(-0.5, left.positionY(), EPSILON);
		assertEquals(1.0, left.positionZ(), EPSILON);
		assertEquals(0.0, left.rotationX(), EPSILON);
		assertEquals(0.0, left.rotationY(), EPSILON);
		assertEquals(0.0, left.rotationZ(), EPSILON);
		assertEquals(1.0, left.rotationW(), EPSILON);
		assertTrue(frame.rightGripPose().active());
		assertEquals(RECEIVED_AT_NANOS, frame.receivedAtNanos());
	}

	@Test
	void acceptsOldV2DatagramWithMissingGripPoses() {
		VrInputFrame frame = parseFrame(frameWithGripPoses(null, null));

		assertTrue(frame.hmd().active());
		assertTrue(frame.leftController().active());
		assertTrue(frame.rightController().active());
		assertSame(VrTrackedPose.INACTIVE, frame.leftGripPose());
		assertSame(VrTrackedPose.INACTIVE, frame.rightGripPose());
	}

	@Test
	void gripPoseRemainsIndependentOfOuterControllerActivity() {
		String inactiveLeftController = controller(validGripPose())
				.replaceFirst("\"active\": true,", "\"active\": false,");
		String json = frameWithControllers(
				inactiveLeftController, controller(null));
		VrInputFrame frame = parseFrame(json);

		assertSame(VrControllerState.INACTIVE, frame.leftController());
		assertTrue(frame.leftGripPose().active());
		assertSame(VrTrackedPose.INACTIVE, frame.rightGripPose());
	}

	@ParameterizedTest
	@ValueSource(strings = {
			"\"not-an-object\"",
			"{\"active\":true,\"positionValid\":true,\"positionTracked\":true,"
					+ "\"orientationValid\":true,\"orientationTracked\":true,"
					+ "\"position\":[0.0,0.0],\"rotation\":[0.0,0.0,0.0,1.0]}",
			"{\"active\":true,\"positionValid\":true,\"positionTracked\":true,"
					+ "\"orientationValid\":true,\"orientationTracked\":true,"
					+ "\"position\":[0.0,0.0,0.0],\"rotation\":[0.0,0.0,0.0,0.0]}",
			"{\"active\":true,\"positionValid\":true,\"positionTracked\":true,"
					+ "\"orientationValid\":true,\"orientationTracked\":true,"
					+ "\"position\":[1e309,0.0,0.0],\"rotation\":[0.0,0.0,0.0,1.0]}",
			"{\"active\":true,\"positionValid\":true,\"positionTracked\":true,"
					+ "\"orientationValid\":true,\"orientationTracked\":true,"
					+ "\"position\":[0.0,0.0,0.0],\"rotation\":[0.0,0.0,1e309,1.0]}"
	})
	void malformedGripPoseDeactivatesOnlyThatHand(String malformedPose) {
		VrInputFrame frame = parseFrame(frameWithGripPoses(malformedPose, validGripPose()));

		assertSame(VrTrackedPose.INACTIVE, frame.leftGripPose());
		assertTrue(frame.leftController().active());
		assertTrue(frame.rightGripPose().active());
		assertTrue(frame.hmd().active());
	}

	@Test
	void oversizedPositionDeactivatesOnlyThatHand() {
		String oversizedPose = validGripPose().replace(
				"\"position\": [0.25, -0.5, 1.0]",
				"\"position\": [4.000001, 0.0, 0.0]"
		);
		VrInputFrame frame = parseFrame(frameWithGripPoses(oversizedPose, validGripPose()));

		assertSame(VrTrackedPose.INACTIVE, frame.leftGripPose());
		assertTrue(frame.leftController().active());
		assertTrue(frame.rightGripPose().active());
	}

	@ParameterizedTest
	@ValueSource(strings = {
			"active",
			"positionValid",
			"positionTracked",
			"orientationValid",
			"orientationTracked"
	})
	void everyTrackingGateMustBeTrue(String falseGate) {
		String untrackedPose = validGripPose().replace(
				"\"" + falseGate + "\": true",
				"\"" + falseGate + "\": false"
		);
		VrInputFrame frame = parseFrame(frameWithGripPoses(untrackedPose, validGripPose()));

		assertSame(VrTrackedPose.INACTIVE, frame.leftGripPose());
		assertTrue(frame.leftController().active());
		assertTrue(frame.rightGripPose().active());
	}

	@ParameterizedTest
	@ValueSource(strings = {
			"active",
			"positionValid",
			"positionTracked",
			"orientationValid",
			"orientationTracked"
	})
	void missingOrWrongTypeTrackingGateDeactivatesOnlyThatHand(String gate) {
		String missingGate = validGripPose().replace(
				"\"" + gate + "\": true,", "");
		VrInputFrame missingFrame = parseFrame(
				frameWithGripPoses(missingGate, validGripPose()));
		assertSame(VrTrackedPose.INACTIVE, missingFrame.leftGripPose());
		assertTrue(missingFrame.rightGripPose().active());

		String wrongTypeGate = validGripPose().replace(
				"\"" + gate + "\": true",
				"\"" + gate + "\": {\"unexpected\": true}");
		VrInputFrame wrongTypeFrame = parseFrame(
				frameWithGripPoses(wrongTypeGate, validGripPose()));
		assertSame(VrTrackedPose.INACTIVE, wrongTypeFrame.leftGripPose());
		assertTrue(wrongTypeFrame.rightGripPose().active());
	}

	@Test
	void positionAtFourMetreLimitIsAccepted() {
		String boundaryPose = validGripPose().replace(
				"\"position\": [0.25, -0.5, 1.0]",
				"\"position\": [0.0, 0.0, 4.0]"
		);
		VrInputFrame frame = parseFrame(frameWithGripPoses(boundaryPose, null));

		assertTrue(frame.leftGripPose().active());
		assertEquals(4.0, frame.leftGripPose().positionZ(), EPSILON);
	}

	@Test
	void diagonalPositionRadiusUsesEuclideanDistance() {
		String inside = validGripPose().replace(
				"\"position\": [0.25, -0.5, 1.0]",
				"\"position\": [2.3094, 2.3094, 2.3094]");
		String outside = validGripPose().replace(
				"\"position\": [0.25, -0.5, 1.0]",
				"\"position\": [2.31, 2.31, 2.31]");

		assertTrue(parseFrame(frameWithGripPoses(inside, null))
				.leftGripPose().active());
		assertSame(VrTrackedPose.INACTIVE,
				parseFrame(frameWithGripPoses(outside, null)).leftGripPose());
	}

	private static VrInputFrame parseFrame(String json) {
		return BridgeInputParser.parse(json, false, RECEIVED_AT_NANOS).orElseThrow();
	}

	private static String frameWithGripPoses(String leftGripPose, String rightGripPose) {
		return frameWithControllers(
				controller(leftGripPose), controller(rightGripPose));
	}

	private static String frameWithControllers(
			String leftController, String rightController) {
		return """
				{
				  "version": 2,
				  "timestamp": 987654321,
				  "hmd": {"rotation": [0.0, 0.0, 0.0, 1.0], "active": true},
				  "controllers": {
				    "left": %s,
				    "right": %s
				  }
				}
				""".formatted(leftController, rightController);
	}

	private static String controller(String gripPose) {
		String gripPoseMember = gripPose == null ? "" : ",\"gripPose\":" + gripPose;
		return """
				{
				  "active": true,
				  "stick": [0.25, -0.5],
				  "trigger": 0.75,
				  "squeeze": 0.5,
				  "stickClick": false,
				  "a": false,
				  "b": false,
				  "x": false,
				  "y": false,
				  "menu": false
				  %s
				}
				""".formatted(gripPoseMember);
	}

	private static String validGripPose() {
		return """
				{
				  "active": true,
				  "positionValid": true,
				  "positionTracked": true,
				  "orientationValid": true,
				  "orientationTracked": true,
				  "position": [0.25, -0.5, 1.0],
				  "rotation": [0.0, 0.0, 0.0, 0.5]
				}
				""";
	}
}
