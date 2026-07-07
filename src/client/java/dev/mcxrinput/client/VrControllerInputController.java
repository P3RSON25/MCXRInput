package dev.mcxrinput.client;

import dev.mcxrinput.protocol.VrControllerState;
import dev.mcxrinput.protocol.VrInputFrame;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;

final class VrControllerInputController {
	private static final Duration MAXIMUM_FRAME_AGE = Duration.ofMillis(250);

	private final VrUdpReceiver receiver;
	private final MCXRInputConfig config;
	private final List<OwnedKey> ownedKeys = new ArrayList<>();

	VrControllerInputController(VrUdpReceiver receiver, MCXRInputConfig config) {
		this.receiver = receiver;
		this.config = config;
	}

	void tick(Minecraft client, boolean inputEnabled) {
		if (!inputEnabled || client.player == null || MCXRInputClient.isGameplayInputBlocked(client)) {
			releaseAll();
			return;
		}

		VrInputFrame frame = receiver.latestFreshFrame(MAXIMUM_FRAME_AGE);
		if (frame == null || !frame.hmd().active()) {
			releaseAll();
			return;
		}

		VrControllerState left = frame.leftController();
		VrControllerState right = frame.rightController();
		if (!left.active() && !right.active()) {
			releaseAll();
			return;
		}

		double deadzone = config.controllerDeadzone();
		boolean forward = left.active() && left.stickY() > deadzone;
		boolean back = left.active() && left.stickY() < -deadzone;
		boolean leftMove = left.active() && left.stickX() < -deadzone;
		boolean rightMove = left.active() && left.stickX() > deadzone;

		// Controller input is deliberately translated only into vanilla key-mapping
		// state. It sends no packets and performs no repeated clicks or automation.
		setOwnedKey(client.options.keyUp, forward);
		setOwnedKey(client.options.keyDown, back);
		setOwnedKey(client.options.keyLeft, leftMove);
		setOwnedKey(client.options.keyRight, rightMove);
		setOwnedKey(client.options.keyJump, right.active() && right.a());
		setOwnedKey(client.options.keyShift, right.active() && right.b());
		setOwnedKey(client.options.keySprint, left.active() && left.stickClick());
		applyOwnedKeys();
	}

	void releaseAll() {
		boolean released = false;
		for (OwnedKey ownedKey : ownedKeys) {
			if (ownedKey.ownedDown) {
				ownedKey.mapping.setDown(false);
				ownedKey.ownedDown = false;
				released = true;
			}
			ownedKey.desiredDown = false;
		}
		if (released) {
			KeyMapping.setAll();
		}
	}

	private void setOwnedKey(KeyMapping mapping, boolean desiredDown) {
		OwnedKey ownedKey = ownedKey(mapping);
		ownedKey.desiredDown = desiredDown;
	}

	private void applyOwnedKeys() {
		boolean released = false;
		for (OwnedKey ownedKey : ownedKeys) {
			if (ownedKey.ownedDown && !ownedKey.desiredDown) {
				ownedKey.mapping.setDown(false);
				ownedKey.ownedDown = false;
				released = true;
			}
		}
		if (released) {
			KeyMapping.setAll();
		}

		for (OwnedKey ownedKey : ownedKeys) {
			if (ownedKey.desiredDown) {
				ownedKey.mapping.setDown(true);
				ownedKey.ownedDown = true;
			}
		}
	}

	private OwnedKey ownedKey(KeyMapping mapping) {
		for (OwnedKey ownedKey : ownedKeys) {
			if (ownedKey.mapping == mapping) {
				return ownedKey;
			}
		}
		OwnedKey ownedKey = new OwnedKey(mapping);
		ownedKeys.add(ownedKey);
		return ownedKey;
	}

	private static final class OwnedKey {
		final KeyMapping mapping;
		boolean desiredDown;
		boolean ownedDown;

		OwnedKey(KeyMapping mapping) {
			this.mapping = mapping;
		}
	}
}
