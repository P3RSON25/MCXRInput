package dev.mcxrinput.client;

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
	private boolean enabled = true;
	private boolean calibrated;
	private float baseHmdYaw;
	private float baseHmdPitch;
	private float baseMinecraftYaw;
	private float baseMinecraftPitch;
	private boolean suspendedForScreen;

	VrCameraController(VrUdpReceiver receiver, MCXRInputConfig config) {
		this.receiver = receiver;
		this.config = config;
	}

	void tick(Minecraft client) {
		if (!enabled || !calibrated || client.player == null) {
			return;
		}

		if (MCXRInputClient.isCameraInputBlocked(client)) {
			suspendedForScreen = true;
			return;
		}

		VrPose pose = receiver.latestFreshPose(MAXIMUM_POSE_AGE);
		if (pose == null) {
			return;
		}

		if (suspendedForScreen) {
			reanchor(client, pose);
			suspendedForScreen = false;
			return;
		}

		HeadOrientation head = PoseMath.toHeadOrientation(pose.x(), pose.y(), pose.z(), pose.w());
		float yawDelta = (float) (PoseMath.wrapDegrees(head.yawDegrees() - baseHmdYaw)
				* config.hmdYawSensitivity());
		float pitchDelta = (float) ((head.pitchDegrees() - baseHmdPitch)
				* config.hmdPitchSensitivity());
		float targetYaw = PoseMath.wrapDegrees(baseMinecraftYaw + yawDelta);
		float targetPitch = PoseMath.clampPitch(baseMinecraftPitch + pitchDelta);

		// This updates the ordinary local player look state. Vanilla remains solely
		// responsible for its normal movement/rotation packets; this mod sends none.
		client.player.setYRot(targetYaw);
		client.player.setXRot(targetPitch);
	}

	void recenter(Minecraft client) {
		if (client.player == null) {
			return;
		}

		VrPose pose = receiver.latestFreshPose(MAXIMUM_POSE_AGE);
		if (pose == null) {
			client.player.sendOverlayMessage(Component.literal("MCXRInput: no fresh headset pose"));
			return;
		}

		reanchor(client, pose);
		calibrated = true;
		suspendedForScreen = false;
		client.player.sendOverlayMessage(Component.literal("MCXRInput: view recentered"));
	}

	private void reanchor(Minecraft client, VrPose pose) {
		HeadOrientation head = PoseMath.toHeadOrientation(pose.x(), pose.y(), pose.z(), pose.w());
		baseHmdYaw = head.yawDegrees();
		baseHmdPitch = head.pitchDegrees();
		baseMinecraftYaw = client.player.getYRot();
		baseMinecraftPitch = client.player.getXRot();
	}

	void toggle(Minecraft client) {
		enabled = !enabled;
		if (client.player != null) {
			client.player.sendOverlayMessage(
					Component.literal("MCXRInput: VR camera " + (enabled ? "enabled" : "disabled"))
			);
		}
	}

	boolean enabled() {
		return enabled;
	}
}
