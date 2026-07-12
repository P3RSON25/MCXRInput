package dev.mcxrinput.client;

import dev.mcxrinput.input.HmdCameraDeltaTracker;
import dev.mcxrinput.protocol.HeadOrientation;
import dev.mcxrinput.protocol.PoseMath;
import dev.mcxrinput.protocol.VrPose;
import net.minecraft.client.Minecraft;
import net.minecraft.network.chat.Component;

import java.time.Duration;

final class VrCameraController {
	private static final Duration MAXIMUM_POSE_AGE = Duration.ofMillis(250);

	private final VrUdpReceiver receiver;
	private final MCXRInputConfig config;
	private final HmdCameraDeltaTracker deltaTracker = new HmdCameraDeltaTracker();
	private boolean enabled;
	private boolean calibrated;

	VrCameraController(VrUdpReceiver receiver, MCXRInputConfig config) {
		this.receiver = receiver;
		this.config = config;
	}

	void tick(Minecraft client) {
		if (!enabled || !calibrated || client.player == null) {
			deltaTracker.reset();
			return;
		}

		if (MCXRInputClient.isCameraInputBlocked(client)) {
			deltaTracker.reset();
			return;
		}

		VrPose pose = receiver.latestFreshPose(
				MAXIMUM_POSE_AGE, client.isMultiplayerServer());
		if (pose == null) {
			// Never apply catch-up motion after stale or inactive tracking. The next
			// accepted pose becomes a reference without changing the player's view.
			deltaTracker.reset();
			return;
		}

		HeadOrientation head = PoseMath.toHeadOrientation(pose.x(), pose.y(), pose.z(), pose.w());
		HmdCameraDeltaTracker.CameraUpdate update = deltaTracker.update(
				head,
				client.player.getYRot(),
				client.player.getXRot(),
				config.hmdYawSensitivity(),
				config.hmdPitchSensitivity()
		);
		if (!update.hasMovement()) {
			// In particular, do not write the current value back on stationary HMD
			// frames; a vanilla/server rotation must remain entirely authoritative.
			return;
		}

		// This updates the ordinary local player look state. Vanilla remains solely
		// responsible for its normal movement/rotation packets; this mod sends none.
		client.player.setYRot(update.yawDegrees());
		client.player.setXRot(update.pitchDegrees());
	}

	void recenter(Minecraft client) {
		if (client.player == null) {
			return;
		}

		VrPose pose = receiver.latestFreshPose(
				MAXIMUM_POSE_AGE, client.isMultiplayerServer());
		if (pose == null) {
			client.player.sendOverlayMessage(Component.literal("MCXRInput: no fresh headset pose"));
			return;
		}

		reanchor(pose);
		calibrated = true;
		client.player.sendOverlayMessage(Component.literal("MCXRInput: view recentered"));
	}

	private void reanchor(VrPose pose) {
		HeadOrientation head = PoseMath.toHeadOrientation(pose.x(), pose.y(), pose.z(), pose.w());
		deltaTracker.anchor(head);
	}

	void toggle(Minecraft client) {
		enabled = !enabled;
		// Enabling never replays head movement that happened while input was off.
		deltaTracker.reset();
		if (client.player != null) {
			client.player.sendOverlayMessage(
					Component.literal("MCXRInput: VR input " + (enabled ? "enabled" : "disabled"))
			);
		}
	}

	void resetForWorldChange() {
		enabled = false;
		calibrated = false;
		deltaTracker.reset();
	}

	boolean enabled() {
		return enabled;
	}
}
