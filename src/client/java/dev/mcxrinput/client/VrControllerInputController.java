package dev.mcxrinput.client;

import dev.mcxrinput.input.AnalogButtonLatch;
import dev.mcxrinput.input.ControllerButton;
import dev.mcxrinput.input.ControllerStick;
import dev.mcxrinput.input.StickDpadRepeater;
import dev.mcxrinput.protocol.VrControllerState;
import dev.mcxrinput.protocol.VrInputFrame;
import net.fabricmc.fabric.api.client.keymapping.v1.KeyMappingHelper;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import net.minecraft.world.entity.player.Inventory;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;

final class VrControllerInputController {
	private static final Duration MAXIMUM_FRAME_AGE = Duration.ofMillis(250);
	private static final float TRIGGER_RELEASE_MARGIN = 0.10F;
	private static final int HOTBAR_INITIAL_REPEAT_DELAY_TICKS = 10;
	private static final int HOTBAR_REPEAT_INTERVAL_TICKS = 4;

	private final VrUdpReceiver receiver;
	private final MCXRInputConfig config;
	private final List<OwnedKey> ownedKeys = new ArrayList<>();
	private final StickDpadRepeater hotbarNavigation = new StickDpadRepeater(
			HOTBAR_INITIAL_REPEAT_DELAY_TICKS, HOTBAR_REPEAT_INTERVAL_TICKS);
	private final AnalogButtonLatch jumpButton = new AnalogButtonLatch(TRIGGER_RELEASE_MARGIN);
	private final AnalogButtonLatch sneakButton = new AnalogButtonLatch(TRIGGER_RELEASE_MARGIN);
	private final AnalogButtonLatch sprintButton = new AnalogButtonLatch(TRIGGER_RELEASE_MARGIN);
	private final AnalogButtonLatch attackButton = new AnalogButtonLatch(TRIGGER_RELEASE_MARGIN);
	private final AnalogButtonLatch useButton = new AnalogButtonLatch(TRIGGER_RELEASE_MARGIN);
	private final AnalogButtonLatch inventoryButton = new AnalogButtonLatch(TRIGGER_RELEASE_MARGIN);

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
		ControllerStick movementStick = config.movementStick();
		VrControllerState movement = movementStick.select(left, right);
		boolean forward = movement.active() && movement.stickY() > deadzone;
		boolean back = movement.active() && movement.stickY() < -deadzone;
		boolean leftMove = movement.active() && movement.stickX() < -deadzone;
		boolean rightMove = movement.active() && movement.stickX() > deadzone;
		float triggerThreshold = (float) config.triggerThreshold();
		boolean jump = bindingDown(config.jumpBinding(), left, right, jumpButton, triggerThreshold);
		boolean sneak = bindingDown(config.sneakBinding(), left, right, sneakButton, triggerThreshold);
		boolean sprint = bindingDown(config.sprintBinding(), left, right, sprintButton, triggerThreshold);
		boolean attack = bindingDown(config.attackBinding(), left, right, attackButton, triggerThreshold);
		boolean use = bindingDown(config.useBinding(), left, right, useButton, triggerThreshold);
		boolean inventory = bindingDown(
				config.inventoryBinding(), left, right, inventoryButton, triggerThreshold);
		VrControllerState hotbarStick = config.hotbarStick().select(left, right);
		StickDpadRepeater.Direction hotbarDirection = hotbarNavigation.update(
				hotbarStick.active(), hotbarStick.stickX(), 0.0F, (float) deadzone);
		if (hotbarDirection == StickDpadRepeater.Direction.RIGHT) {
			selectHotbarOffset(client.player.getInventory(), 1);
		} else if (hotbarDirection == StickDpadRepeater.Direction.LEFT) {
			selectHotbarOffset(client.player.getInventory(), -1);
		}

		// Gameplay actions below deliberately use only vanilla key-mapping state;
		// hotbar selection above is isolated to vanilla's local selected-slot state.
		// Attack/use press edges require a fresh physical control press, while holding
		// the binding follows Minecraft's normal mouse-button behavior.
		setOwnedKey(client.options.keyUp, forward);
		setOwnedKey(client.options.keyDown, back);
		setOwnedKey(client.options.keyLeft, leftMove);
		setOwnedKey(client.options.keyRight, rightMove);
		setOwnedKey(client.options.keyJump, jump);
		setOwnedKey(client.options.keyShift, sneak);
		setOwnedKey(client.options.keySprint, sprint);
		setOwnedKey(client.options.keyAttack, attack, true);
		setOwnedKey(client.options.keyUse, use, true);
		setOwnedKey(client.options.keyInventory, inventory, true);
		applyOwnedKeys();
	}

	void releaseAll() {
		hotbarNavigation.suppress();
		jumpButton.suppress();
		sneakButton.suppress();
		sprintButton.suppress();
		attackButton.suppress();
		useButton.suppress();
		inventoryButton.suppress();
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

	private static void selectHotbarOffset(Inventory inventory, int offset) {
		// This is the same local selected-slot state changed by vanilla number keys
		// and the mouse wheel. Minecraft remains responsible for its normal sync.
		int size = Inventory.getSelectionSize();
		inventory.setSelectedSlot(Math.floorMod(inventory.getSelectedSlot() + offset, size));
	}

	private static boolean bindingDown(
			ControllerButton binding,
			VrControllerState left,
			VrControllerState right,
			AnalogButtonLatch latch,
			float threshold
	) {
		ControllerButton.Sample sample = binding.sample(left, right);
		return latch.update(sample.active(), sample.value(), threshold);
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
