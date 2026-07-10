package dev.mcxrinput.client;

import dev.mcxrinput.input.AnalogButtonLatch;
import dev.mcxrinput.protocol.VrControllerState;
import dev.mcxrinput.protocol.VrInputFrame;
import net.fabricmc.fabric.api.client.keymapping.v1.KeyMappingHelper;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;

final class VrControllerInputController {
	private static final Duration MAXIMUM_FRAME_AGE = Duration.ofMillis(250);
	private static final float TRIGGER_RELEASE_MARGIN = 0.10F;

	private final VrUdpReceiver receiver;
	private final MCXRInputConfig config;
	private final List<OwnedKey> ownedKeys = new ArrayList<>();
	private final AnalogButtonLatch attackTrigger = new AnalogButtonLatch(TRIGGER_RELEASE_MARGIN);
	private final AnalogButtonLatch useTrigger = new AnalogButtonLatch(TRIGGER_RELEASE_MARGIN);

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
		float triggerThreshold = (float) config.triggerThreshold();
		boolean attack = attackTrigger.update(right.active(), right.trigger(), triggerThreshold);
		boolean use = useTrigger.update(left.active(), left.trigger(), triggerThreshold);

		// Controller input is deliberately translated only into vanilla key-mapping
		// state. Trigger press edges are generated only by a fresh physical pull;
		// holding a trigger follows Minecraft's normal mouse-button behavior.
		setOwnedKey(client.options.keyUp, forward);
		setOwnedKey(client.options.keyDown, back);
		setOwnedKey(client.options.keyLeft, leftMove);
		setOwnedKey(client.options.keyRight, rightMove);
		setOwnedKey(client.options.keyJump, right.active() && right.a());
		setOwnedKey(client.options.keyShift, right.active() && right.b());
		setOwnedKey(client.options.keySprint, left.active() && left.stickClick());
		setOwnedKey(client.options.keyAttack, attack, true);
		setOwnedKey(client.options.keyUse, use, true);
		applyOwnedKeys();
	}

	void releaseAll() {
		attackTrigger.suppress();
		useTrigger.suppress();
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
		setOwnedKey(mapping, desiredDown, false);
	}

	private void setOwnedKey(KeyMapping mapping, boolean desiredDown, boolean clickOnPress) {
		OwnedKey ownedKey = ownedKey(mapping, clickOnPress);
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
				if (!ownedKey.ownedDown && ownedKey.clickOnPress && !ownedKey.mapping.isUnbound()) {
					KeyMapping.click(KeyMappingHelper.getBoundKeyOf(ownedKey.mapping));
				}
				ownedKey.mapping.setDown(true);
				ownedKey.ownedDown = true;
			}
		}
	}

	private OwnedKey ownedKey(KeyMapping mapping, boolean clickOnPress) {
		for (OwnedKey ownedKey : ownedKeys) {
			if (ownedKey.mapping == mapping) {
				return ownedKey;
			}
		}
		OwnedKey ownedKey = new OwnedKey(mapping, clickOnPress);
		ownedKeys.add(ownedKey);
		return ownedKey;
	}

	private static final class OwnedKey {
		final KeyMapping mapping;
		final boolean clickOnPress;
		boolean desiredDown;
		boolean ownedDown;

		OwnedKey(KeyMapping mapping, boolean clickOnPress) {
			this.mapping = mapping;
			this.clickOnPress = clickOnPress;
		}
	}
}
