package dev.mcxrinput.protocol;

import com.google.gson.Gson;
import com.google.gson.JsonElement;

import java.util.Optional;

/** Parses and validates gameplay-input JSON independently of UDP transport. */
public final class BridgeInputParser {
	private static final Gson GSON = new Gson();

	private BridgeInputParser() {
	}

	/**
	 * Parses one complete v1/v2 gameplay-input object.
	 *
	 * <p>A structurally malformed grip pose is isolated to that hand. Malformed
	 * outer JSON may still throw a Gson runtime exception for the receiver to
	 * log and discard.</p>
	 */
	public static Optional<VrInputFrame> parse(
			String json,
			boolean allowV1TestPoses,
			long receivedAtNanos
	) {
		BridgeMessage message = GSON.fromJson(json, BridgeMessage.class);
		if (message == null || !BridgeProtocolPolicy.accepts(message.version, allowV1TestPoses)
				|| message.hmd == null
				|| message.hmd.rotation == null || message.hmd.rotation.length != 4) {
			return Optional.empty();
		}

		double[] rotation = message.hmd.rotation;
		if (!PoseMath.isPlausibleQuaternion(rotation[0], rotation[1], rotation[2], rotation[3])) {
			return Optional.empty();
		}

		// Missing tracking state is never treated as permission to move or act.
		boolean active = message.hmd.active != null && message.hmd.active;
		VrPose hmd = new VrPose(
				rotation[0], rotation[1], rotation[2], rotation[3],
				message.timestamp, receivedAtNanos, active
		);
		VrControllerState leftController = VrControllerState.INACTIVE;
		VrControllerState rightController = VrControllerState.INACTIVE;
		VrTrackedPose leftGripPose = VrTrackedPose.INACTIVE;
		VrTrackedPose rightGripPose = VrTrackedPose.INACTIVE;
		if (message.version == 2 && message.controllers != null) {
			leftController = parseController(message.controllers.left);
			rightController = parseController(message.controllers.right);
			leftGripPose = parseGripPose(message.controllers.left);
			rightGripPose = parseGripPose(message.controllers.right);
		}

		return Optional.of(new VrInputFrame(
				message.version,
				hmd,
				leftController,
				rightController,
				leftGripPose,
				rightGripPose,
				receivedAtNanos
		));
	}

	private static VrControllerState parseController(ControllerMessage controller) {
		if (controller == null || controller.active == null || !controller.active) {
			return VrControllerState.INACTIVE;
		}
		if (controller.stick == null || controller.stick.length != 2
				|| !Float.isFinite(controller.stick[0]) || !Float.isFinite(controller.stick[1])) {
			return VrControllerState.INACTIVE;
		}

		float trigger = finiteClamped(controller.trigger, 0.0F, 0.0F, 1.0F);
		float squeeze = finiteClamped(controller.squeeze, 0.0F, 0.0F, 1.0F);
		return new VrControllerState(
				true,
				finiteClamped(controller.stick[0], 0.0F, -1.0F, 1.0F),
				finiteClamped(controller.stick[1], 0.0F, -1.0F, 1.0F),
				trigger,
				squeeze,
				controller.stickClick != null && controller.stickClick,
				controller.a != null && controller.a,
				controller.b != null && controller.b,
				controller.x != null && controller.x,
				controller.y != null && controller.y,
				controller.menu != null && controller.menu
		);
	}

	private static VrTrackedPose parseGripPose(ControllerMessage controller) {
		if (controller == null || controller.gripPose == null) {
			return VrTrackedPose.INACTIVE;
		}

		try {
			GripPoseMessage pose = GSON.fromJson(controller.gripPose, GripPoseMessage.class);
			if (pose == null
					|| !Boolean.TRUE.equals(pose.active)
					|| !Boolean.TRUE.equals(pose.positionValid)
					|| !Boolean.TRUE.equals(pose.positionTracked)
					|| !Boolean.TRUE.equals(pose.orientationValid)
					|| !Boolean.TRUE.equals(pose.orientationTracked)
					|| pose.position == null || pose.position.length != 3
					|| pose.rotation == null || pose.rotation.length != 4) {
				return VrTrackedPose.INACTIVE;
			}

			return new VrTrackedPose(
					true,
					pose.position[0], pose.position[1], pose.position[2],
					pose.rotation[0], pose.rotation[1], pose.rotation[2], pose.rotation[3]
			);
		} catch (RuntimeException exception) {
			// A bad optional pose must not discard its controller inputs, the other
			// hand, or the HMD sample carried by the same v2 datagram.
			return VrTrackedPose.INACTIVE;
		}
	}

	private static float finiteClamped(Float value, float fallback, float minimum, float maximum) {
		if (value == null || !Float.isFinite(value)) {
			return fallback;
		}
		return Math.max(minimum, Math.min(maximum, value));
	}

	private static final class BridgeMessage {
		int version;
		long timestamp;
		HmdMessage hmd;
		ControllersMessage controllers;
	}

	private static final class HmdMessage {
		double[] rotation;
		Boolean active;
	}

	private static final class ControllersMessage {
		ControllerMessage left;
		ControllerMessage right;
	}

	private static final class ControllerMessage {
		Boolean active;
		float[] stick;
		Float trigger;
		Float squeeze;
		Boolean stickClick;
		Boolean a;
		Boolean b;
		Boolean x;
		Boolean y;
		Boolean menu;
		JsonElement gripPose;
	}

	private static final class GripPoseMessage {
		Boolean active;
		Boolean positionValid;
		Boolean positionTracked;
		Boolean orientationValid;
		Boolean orientationTracked;
		double[] position;
		double[] rotation;
	}
}
