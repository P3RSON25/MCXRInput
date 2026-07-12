package dev.mcxrinput.client;

import com.mojang.blaze3d.platform.InputConstants;
import dev.mcxrinput.input.AnalogButtonLatch;
import dev.mcxrinput.input.ControllerButton;
import dev.mcxrinput.input.ControllerStick;
import dev.mcxrinput.input.GameplayKeyOwnership;
import dev.mcxrinput.input.MovementStickGate;
import dev.mcxrinput.input.StickDpadGesture;
import dev.mcxrinput.input.StickDpadRepeater;
import dev.mcxrinput.protocol.VrControllerState;
import dev.mcxrinput.protocol.VrInputFrame;
import net.fabricmc.fabric.api.client.keymapping.v1.KeyMappingHelper;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import net.minecraft.client.ToggleKeyMapping;
import net.minecraft.network.chat.Component;
import net.minecraft.world.entity.player.Inventory;
import org.lwjgl.glfw.GLFW;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;

final class VrControllerInputController {
	private static final Duration MAXIMUM_FRAME_AGE = Duration.ofMillis(250);
	private static final float BUTTON_RELEASE_MARGIN = 0.10F;

	private final VrUdpReceiver receiver;
	private final MCXRInputConfig config;
	private final List<OwnedKey> ownedKeys = new ArrayList<>();
	private final MovementStickGate movementGate = new MovementStickGate();
	private final StickDpadGesture hotbarNavigation = new StickDpadGesture();
	private final AnalogButtonLatch jumpButton = new AnalogButtonLatch(BUTTON_RELEASE_MARGIN);
	private final AnalogButtonLatch sneakButton = new AnalogButtonLatch(BUTTON_RELEASE_MARGIN);
	private final AnalogButtonLatch sprintButton = new AnalogButtonLatch(BUTTON_RELEASE_MARGIN);
	private final AnalogButtonLatch attackButton = new AnalogButtonLatch(BUTTON_RELEASE_MARGIN);
	private final AnalogButtonLatch useButton = new AnalogButtonLatch(BUTTON_RELEASE_MARGIN);
	private final AnalogButtonLatch inventoryButton = new AnalogButtonLatch(BUTTON_RELEASE_MARGIN);

	VrControllerInputController(VrUdpReceiver receiver, MCXRInputConfig config) {
		this.receiver = receiver;
		this.config = config;
	}

	void tick(Minecraft client, boolean inputEnabled) {
		if (!inputEnabled || client.player == null || MCXRInputClient.isGameplayInputBlocked(client)) {
			releaseAll(client);
			return;
		}

		VrInputFrame frame = receiver.latestFreshFrame(
				MAXIMUM_FRAME_AGE, client.isMultiplayerServer());
		if (frame == null || !frame.hmd().active()) {
			releaseAll(client);
			return;
		}

		VrControllerState left = frame.leftController();
		VrControllerState right = frame.rightController();
		if (!left.active() && !right.active()) {
			releaseAll(client);
			return;
		}

		float deadzone = (float) config.controllerDeadzone();
		ControllerStick movementStick = config.movementStick();
		VrControllerState movement = movementStick.select(left, right);
		boolean movementAccepted = movementGate.accepts(
				movement.active(), movement.stickX(), movement.stickY(), deadzone);
		boolean forward = movementAccepted && movement.stickY() > deadzone;
		boolean back = movementAccepted && movement.stickY() < -deadzone;
		boolean leftMove = movementAccepted && movement.stickX() < -deadzone;
		boolean rightMove = movementAccepted && movement.stickX() > deadzone;

		float buttonThreshold = (float) config.triggerThreshold();
		ControllerButton jumpBinding = config.jumpBinding();
		ControllerButton sneakBinding = config.sneakBinding();
		ControllerButton sprintBinding = config.sprintBinding();
		ControllerButton attackBinding = config.attackBinding();
		ControllerButton useBinding = config.useBinding();
		ControllerButton inventoryBinding = config.inventoryBinding();
		ControllerButton[] gameplayBindings = {
				jumpBinding, sneakBinding, sprintBinding,
				attackBinding, useBinding, inventoryBinding
		};
		boolean jump = bindingDown(jumpBinding, left, right, jumpButton, buttonThreshold);
		boolean sneak = bindingDown(sneakBinding, left, right, sneakButton, buttonThreshold);
		boolean sprint = bindingDown(sprintBinding, left, right, sprintButton, buttonThreshold);
		boolean attack = bindingDown(attackBinding, left, right, attackButton, buttonThreshold);
		boolean use = bindingDown(useBinding, left, right, useButton, buttonThreshold);
		boolean inventory = bindingDown(
				inventoryBinding, left, right, inventoryButton, buttonThreshold);

		ControllerStick hotbarStickSetting = config.hotbarStick();
		VrControllerState hotbarStick = hotbarStickSetting.select(left, right);
		StickDpadRepeater.Direction hotbarDirection = hotbarStickSetting == movementStick
				? null
				: hotbarNavigation.update(
						hotbarStick.active(), hotbarStick.stickX(), 0.0F, deadzone);
		if (hotbarStickSetting == movementStick) {
			hotbarNavigation.suppress();
		}
		if (hotbarDirection == StickDpadRepeater.Direction.RIGHT) {
			selectHotbarOffset(client.player.getInventory(), 1);
		} else if (hotbarDirection == StickDpadRepeater.Direction.LEFT) {
			selectHotbarOffset(client.player.getInventory(), -1);
		}

		// Each mapping below changes only on a fresh controller press/release edge.
		// Minecraft still owns all continuous vanilla behavior while a physical
		// controller input remains held. No timed click or key repeat is generated.
		updateOwnedKey(client, client.options.keyUp, forward, false, true, null);
		updateOwnedKey(client, client.options.keyDown, back, false, true, null);
		updateOwnedKey(client, client.options.keyLeft, leftMove, false, true, null);
		updateOwnedKey(client, client.options.keyRight, rightMove, false, true, null);
		updateOwnedKey(
				client, client.options.keyJump, jump, false,
				isUniqueBinding(jumpBinding, gameplayBindings),
				"Jump controller input is disabled because its controller binding conflicts");
		updateOwnedKey(
				client, client.options.keyShift, sneak, false,
				!client.options.toggleCrouch().get()
						&& isUniqueBinding(sneakBinding, gameplayBindings),
				client.options.toggleCrouch().get()
						? "Sneak controller input is disabled while Toggle Crouch is enabled"
						: "Sneak controller input is disabled because its controller binding conflicts");
		updateOwnedKey(
				client, client.options.keySprint, sprint, false,
				!client.options.toggleSprint().get()
						&& isUniqueBinding(sprintBinding, gameplayBindings),
				client.options.toggleSprint().get()
						? "Sprint controller input is disabled while Toggle Sprint is enabled"
						: "Sprint controller input is disabled because its controller binding conflicts");
		updateOwnedKey(
				client, client.options.keyAttack, attack, true,
				!client.options.toggleAttack().get()
						&& isUniqueBinding(attackBinding, gameplayBindings),
				client.options.toggleAttack().get()
						? "Attack controller input is disabled while Toggle Attack is enabled"
						: "Attack controller input is disabled because its controller binding conflicts");
		updateOwnedKey(
				client, client.options.keyUse, use, true,
				!client.options.toggleUse().get()
						&& isUniqueBinding(useBinding, gameplayBindings),
				client.options.toggleUse().get()
						? "Use controller input is disabled while Toggle Use is enabled"
						: "Use controller input is disabled because its controller binding conflicts");
		updateOwnedKey(
				client, client.options.keyInventory, inventory, true,
				isUniqueBinding(inventoryBinding, gameplayBindings),
				"Inventory controller input is disabled because its controller binding conflicts");
	}

	void releaseAll(Minecraft client) {
		movementGate.suppress();
		hotbarNavigation.suppress();
		jumpButton.suppress();
		sneakButton.suppress();
		sprintButton.suppress();
		attackButton.suppress();
		useButton.suppress();
		inventoryButton.suppress();

		for (OwnedKey ownedKey : ownedKeys) {
			PhysicalState physicalState = physicalState(client, ownedKey.mapping);
			GameplayKeyOwnership.Decision decision = ownedKey.ownership.release(
					physicalState == PhysicalState.DOWN);
			applyDecision(client, ownedKey, decision, physicalState, null);
		}
	}

	private void updateOwnedKey(
			Minecraft client,
			KeyMapping mapping,
			boolean controllerDown,
			boolean clickOnPress,
			boolean actionAllowed,
			String rejectionReason
	) {
		OwnedKey ownedKey = ownedKey(mapping, clickOnPress);
		PhysicalState physicalState = physicalState(client, mapping);
		boolean clickBindingSafe = !clickOnPress || hasUniqueClickBinding(client, mapping);
		boolean physicalStateKnown = physicalState != PhysicalState.UNSUPPORTED;
		boolean accepted = actionAllowed && clickBindingSafe && physicalStateKnown;

		String effectiveRejection = rejectionReason;
		if (actionAllowed && !physicalStateKnown) {
			effectiveRejection = "Controller action disabled for an unsupported scan-code key binding";
		} else if (actionAllowed && !clickBindingSafe) {
			effectiveRejection = mapping.isUnbound()
					? "Controller action disabled because its Minecraft key mapping is unbound"
					: "Controller action disabled because its Minecraft key mapping conflicts";
		}

		GameplayKeyOwnership.Decision decision = ownedKey.ownership.update(
				accepted,
				controllerDown,
				mapping.isDown(),
				physicalState == PhysicalState.DOWN
		);
		applyDecision(client, ownedKey, decision, physicalState, effectiveRejection);
	}

	private static void applyDecision(
			Minecraft client,
			OwnedKey ownedKey,
			GameplayKeyOwnership.Decision decision,
			PhysicalState physicalState,
			String rejectionReason
	) {
		switch (decision) {
			case NONE -> {
				// No logical mutation.
			}
			case RELINQUISH -> {
				// Hand ownership back to a currently held physical keyboard/mouse input.
				// Toggle-mode actions are rejected and must not be flipped here.
				if (physicalState == PhysicalState.DOWN
						&& !vanillaToggleEnabled(client, ownedKey.mapping)
						&& !ownedKey.mapping.isDown()) {
					ownedKey.mapping.setDown(true);
				}
			}
			case PRESS -> {
				ownedKey.mapping.setDown(true);
				if (ownedKey.clickOnPress) {
					// Conflict/unbound checks above ensure this public vanilla helper can
					// increment only the intended mapping's click count once.
					KeyMapping.click(KeyMappingHelper.getBoundKeyOf(ownedKey.mapping));
				}
			}
			case RELEASE -> forceMappingUp(client, ownedKey.mapping);
			case REJECTED_PRESS -> {
				if (client.player != null && rejectionReason != null) {
					client.player.sendOverlayMessage(Component.literal("MCXRInput: " + rejectionReason));
				}
			}
		}
	}

	private static void forceMappingUp(
			Minecraft client,
			KeyMapping mapping
	) {
		if (!mapping.isDown()) {
			return;
		}

		if (mapping instanceof ToggleKeyMapping && vanillaToggleEnabled(client, mapping)) {
			// ToggleKeyMapping ignores setDown(false). A single true transition flips
			// an MCXRInput-owned true state off; toggle-mode presses are otherwise rejected.
			mapping.setDown(true);
		} else {
			mapping.setDown(false);
		}
	}

	private static boolean vanillaToggleEnabled(Minecraft client, KeyMapping mapping) {
		if (mapping == client.options.keyShift) {
			return client.options.toggleCrouch().get();
		}
		if (mapping == client.options.keySprint) {
			return client.options.toggleSprint().get();
		}
		if (mapping == client.options.keyAttack) {
			return client.options.toggleAttack().get();
		}
		return mapping == client.options.keyUse && client.options.toggleUse().get();
	}

	private static PhysicalState physicalState(Minecraft client, KeyMapping mapping) {
		if (mapping.isUnbound()) {
			return PhysicalState.UP;
		}

		InputConstants.Key key = KeyMappingHelper.getBoundKeyOf(mapping);
		if (key.getType() == InputConstants.Type.KEYSYM) {
			return InputConstants.isKeyDown(client.getWindow(), key.getValue())
					? PhysicalState.DOWN : PhysicalState.UP;
		}
		if (key.getType() == InputConstants.Type.MOUSE) {
			return GLFW.glfwGetMouseButton(client.getWindow().handle(), key.getValue()) == GLFW.GLFW_PRESS
					? PhysicalState.DOWN : PhysicalState.UP;
		}
		return PhysicalState.UNSUPPORTED;
	}

	private static boolean hasUniqueClickBinding(Minecraft client, KeyMapping target) {
		if (target.isUnbound()) {
			return false;
		}
		for (KeyMapping candidate : client.options.keyMappings) {
			if (candidate != target && candidate.same(target)) {
				return false;
			}
		}
		return true;
	}

	private static boolean isUniqueBinding(
			ControllerButton target,
			ControllerButton[] gameplayBindings
	) {
		if (target == ControllerButton.NONE) {
			return true;
		}
		int matches = 0;
		for (ControllerButton binding : gameplayBindings) {
			if (binding == target && ++matches > 1) {
				return false;
			}
		}
		return true;
	}

	private static void selectHotbarOffset(Inventory inventory, int offset) {
		// One neutral-to-deflected physical gesture changes one local selected slot.
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
		final GameplayKeyOwnership ownership = new GameplayKeyOwnership();

		OwnedKey(KeyMapping mapping, boolean clickOnPress) {
			this.mapping = mapping;
			this.clickOnPress = clickOnPress;
		}
	}

	private enum PhysicalState {
		UP,
		DOWN,
		UNSUPPORTED
	}
}
