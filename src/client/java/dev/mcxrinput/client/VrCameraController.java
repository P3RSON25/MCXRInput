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
	private boolean enabled = true;
	private boolean calibrated;
	private float baseHmdYaw;
	private float baseHmdPitch;
	private float baseMinecraftYaw;
	private float baseMinecraftPitch;

	VrCameraController(VrUdpReceiver receiver) {
		this.receiver = receiver;
	}

	void tick(Minecraft client) {
		if (!enabled || !calibrated || client.player == null) {
			return;
		}

		VrPose pose = receiver.latestFreshPose(MAXIMUM_POSE_AGE);
		if (pose == null) {
			return;
		}

		HeadOrientation head = PoseMath.toHeadOrientation(pose.x(), pose.y(), pose.z(), pose.w());
		float yawDelta = PoseMath.wrapDegrees(head.yawDegrees() - baseHmdYaw);
		float pitchDelta = head.pitchDegrees() - baseHmdPitch;
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

		HeadOrientation head = PoseMath.toHeadOrientation(pose.x(), pose.y(), pose.z(), pose.w());
		baseHmdYaw = head.yawDegrees();
		baseHmdPitch = head.pitchDegrees();
		baseMinecraftYaw = client.player.getYRot();
		baseMinecraftPitch = client.player.getXRot();
		calibrated = true;
		client.player.sendOverlayMessage(Component.literal("MCXRInput: view recentered"));
	}

	void toggle(Minecraft client) {
		enabled = !enabled;
		if (client.player != null) {
			client.player.sendOverlayMessage(
					Component.literal("MCXRInput: VR camera " + (enabled ? "enabled" : "disabled"))
			);
		}
	}
}
