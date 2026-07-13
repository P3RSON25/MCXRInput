package dev.mcxrinput.client;

import dev.mcxrinput.input.HmdCameraDeltaTracker;
import dev.mcxrinput.protocol.HeadOrientation;
import dev.mcxrinput.protocol.PoseMath;
import dev.mcxrinput.protocol.VrPose;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.network.chat.Component;

import java.time.Duration;

final class VrCameraController {
	private static final Duration MAXIMUM_POSE_AGE = Duration.ofMillis(250);

	private final VrUdpReceiver receiver;
	private final MCXRInputConfig config;
	private final HmdCameraDeltaTracker deltaTracker = new HmdCameraDeltaTracker();
	private boolean enabled;
	private boolean calibrated;
	private ClientLevel enabledLevel;
	private LocalPlayer enabledPlayer;

	VrCameraController(VrUdpReceiver receiver, MCXRInputConfig config) {
		this.receiver = receiver;
		this.config = config;
	}

	void updateForRenderFrame(Minecraft client) {
		if (!enabled) {
			deltaTracker.reset();
			return;
		}
		if (client.level != enabledLevel || client.player != enabledPlayer) {
			// Render frames can occur before the next client-tick world-change check.
			// Fail closed immediately rather than applying an old world's anchor.
			resetForWorldChange();
			return;
		}
		if (!calibrated || client.player == null) {
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
		LocalPlayer player = client.player;
		HmdCameraDeltaTracker.CameraUpdate update = deltaTracker.update(
				head,
				player.getYRot(),
				player.getXRot(),
				player.yRotO,
				player.xRotO,
				config.hmdYawSensitivity(),
				config.hmdPitchSensitivity()
		);
		if (!update.hasMovement()) {
			// In particular, do not write the current value back on stationary HMD
			// frames; a vanilla/server rotation must remain entirely authoritative.
			return;
		}

		// Shift both interpolation endpoints by the same accepted physical delta.
		// This removes tick-rate camera lag without synthesizing intermediate poses.
		// Vanilla remains solely responsible for normal look packets; this mod sends none.
		player.yRotO = PoseMath.wrapDegrees(
				player.yRotO + update.appliedYawDeltaDegrees());
		player.xRotO = PoseMath.clampPitch(
				player.xRotO + update.appliedPitchDeltaDegrees());
		player.setYRot(update.yawDegrees());
		player.setXRot(update.pitchDegrees());
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
		if (enabled) {
			enabledLevel = client.level;
			enabledPlayer = client.player;
		} else {
			enabledLevel = null;
			enabledPlayer = null;
		}
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
		enabledLevel = null;
		enabledPlayer = null;
		deltaTracker.reset();
	}

	boolean enabled() {
		return enabled;
	}
}
