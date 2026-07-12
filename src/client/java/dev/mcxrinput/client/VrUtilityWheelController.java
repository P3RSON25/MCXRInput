package dev.mcxrinput.client;

import com.mojang.blaze3d.platform.InputConstants;
import dev.mcxrinput.input.AnalogButtonLatch;
import dev.mcxrinput.input.ControllerButton;
import dev.mcxrinput.input.UtilityWheelSelection;
import dev.mcxrinput.protocol.VrControllerState;
import dev.mcxrinput.protocol.VrInputFrame;
import net.minecraft.client.CameraType;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.components.ChatComponent;
import net.minecraft.client.gui.screens.Screen;
import net.fabricmc.fabric.api.client.keymapping.v1.KeyMappingHelper;
import org.lwjgl.glfw.GLFW;

import java.time.Duration;

/**
 * Owns the utility wheel's physical-button state and four client utility actions.
 * The wheel never sends gameplay packets or repeats an action without a fresh press.
 */
final class VrUtilityWheelController {
	private static final Duration MAXIMUM_FRAME_AGE = Duration.ofMillis(250);
	private static final float BUTTON_RELEASE_MARGIN = 0.10F;
	private static final float MINIMUM_SELECTION_THRESHOLD = 0.55F;

	private final VrUdpReceiver receiver;
	private final MCXRInputConfig config;
	private final AnalogButtonLatch wheelButton = new AnalogButtonLatch(BUTTON_RELEASE_MARGIN);
	private final AnalogButtonLatch backButton = new AnalogButtonLatch(BUTTON_RELEASE_MARGIN);

	private UtilityWheelScreen wheelScreen;
	private UtilityWheelSelection.Action selectedAction;
	private boolean wheelWasDown;
	private boolean backWasDown;
	private boolean ownsPlayerList;

	VrUtilityWheelController(VrUdpReceiver receiver, MCXRInputConfig config) {
		this.receiver = receiver;
		this.config = config;
	}

	/**
	 * Updates utility state before the other input controllers run.
	 *
	 * @return true when this tick must not reach gameplay/menu/inventory controllers
	 */
	boolean tick(Minecraft client, boolean inputEnabled) {
		boolean capturesInput = false;
		Screen currentScreen = client.gui.screen();

		// A keyboard/mouse close or another screen replacing the wheel is a cancel,
		// never an implicit selection. Leave the replacement screen untouched.
		if (wheelScreen != null && currentScreen != wheelScreen) {
			clearWheelState();
			suppressButtonEdges();
			capturesInput = true;
		}

		boolean hadOwnedUi = wheelScreen != null || ownsPlayerList;
		if (!inputEnabled || client.player == null || client.gui.overlay() != null) {
			releaseAll(client);
			return capturesInput || hadOwnedUi;
		}

		VrInputFrame frame = receiver.latestFreshFrame(
				MAXIMUM_FRAME_AGE, client.isMultiplayerServer());
		if (frame == null || !frame.hmd().active()
				|| (!frame.leftController().active() && !frame.rightController().active())) {
			releaseAll(client);
			return capturesInput || hadOwnedUi;
		}

		VrControllerState left = frame.leftController();
		VrControllerState right = frame.rightController();
		ControllerButton utilityBinding = config.utilityWheelBinding();
		float buttonThreshold = (float) config.triggerThreshold();
		ControllerButton.Sample utilitySample = utilityBinding.sample(left, right);
		boolean wheelDown = wheelButton.update(
				utilitySample.active(), utilitySample.value(), buttonThreshold);

		ControllerButton configuredBackBinding = config.menuBackBinding();
		boolean backDown;
		if (configuredBackBinding == utilityBinding) {
			// A remap may intentionally reuse the button. In that case release of the
			// utility binding still selects, and its press closes an open player list.
			backButton.suppress();
			backDown = false;
		} else {
			backDown = bindingDown(
					configuredBackBinding, left, right, backButton, buttonThreshold);
		}

		// Losing the controller that owns the hold must cancel rather than looking
		// like a physical release. The wheel also needs a tracked right stick to
		// select from; both cases fail closed with no delayed action.
		if ((wheelScreen != null && (!utilitySample.active() || !right.active()))
				|| (ownsPlayerList && !utilitySample.active())) {
			releaseAll(client);
			return true;
		}

		boolean wheelPressed = wheelDown && !wheelWasDown;
		boolean wheelReleased = !wheelDown && wheelWasDown;
		boolean backPressed = backDown && !backWasDown;
		wheelWasDown = wheelDown;
		backWasDown = backDown;

		if (ownsPlayerList) {
			if (currentScreen != null || !wheelDown || backPressed) {
				closePlayerList(client);
				suppressButtonEdges();
				return true;
			}

			// The vanilla player-list key stays down only while the same physical
			// utility press remains held. It is never reasserted on a timer.
			return true;
		}

		if (wheelScreen != null) {
			capturesInput = true;
			updateSelection(right);

			if (backPressed) {
				closeWheelScreen(client);
				suppressButtonEdges();
				return true;
			}
			if (selectedAction == UtilityWheelSelection.Action.PLAYER_LIST && wheelDown) {
				openPlayerList(client);
				return true;
			}
			if (wheelReleased) {
				UtilityWheelSelection.Action action = selectedAction;
				closeWheelScreen(client);
				if (action != null) {
					performAction(client, action);
				}
				return true;
			}
			return true;
		}

		if (currentScreen == null && wheelPressed) {
			if (right.active()) {
				openWheel(client, right);
			} else {
				// Consume the configured utility press even when its selection stick is
				// unavailable; it must not leak into a duplicate gameplay binding.
				suppressButtonEdges();
			}
			return true;
		}

		return capturesInput;
	}

	void releaseAll(Minecraft client) {
		if (wheelScreen != null && client.gui.screen() == wheelScreen) {
			client.gui.setScreen(null);
		}
		clearWheelState();
		closePlayerList(client);
		suppressButtonEdges();
	}

	private void openWheel(Minecraft client, VrControllerState rightController) {
		wheelScreen = new UtilityWheelScreen();
		selectedAction = null;
		updateSelection(rightController);
		client.gui.setScreen(wheelScreen);
	}

	private void updateSelection(VrControllerState rightController) {
		if (!rightController.active()) {
			return;
		}

		float threshold = Math.max(
				(float) config.controllerDeadzone(), MINIMUM_SELECTION_THRESHOLD);
		UtilityWheelSelection.select(
				rightController.stickX(), rightController.stickY(), threshold)
				.ifPresent(action -> {
					selectedAction = action;
					wheelScreen.setSelectedAction(action);
				});
	}

	private void closeWheelScreen(Minecraft client) {
		if (wheelScreen != null && client.gui.screen() == wheelScreen) {
			// Minecraft 26.2 ignores pauseGame while another screen is installed,
			// so every action transition removes the cosmetic overlay first.
			client.gui.setScreen(null);
		}
		clearWheelState();
	}

	private void clearWheelState() {
		if (wheelScreen != null) {
			wheelScreen.clearSelectedAction();
		}
		wheelScreen = null;
		selectedAction = null;
	}

	private void performAction(Minecraft client, UtilityWheelSelection.Action action) {
		switch (action) {
			case PAUSE -> client.pauseGame(false);
			case CHAT -> client.gui.openChatScreen(ChatComponent.ChatMethod.MESSAGE);
			case PLAYER_LIST -> {
				// Player-list viewing begins while the wheel press is still held above.
			}
			case PERSPECTIVE -> cyclePerspective(client);
		}
	}

	private void openPlayerList(Minecraft client) {
		PhysicalMappingState physicalState = physicalMappingState(
				client, client.options.keyPlayerList);
		if (physicalState == PhysicalMappingState.UNSUPPORTED) {
			closeWheelScreen(client);
			suppressButtonEdges();
			if (client.player != null) {
				client.player.sendOverlayMessage(net.minecraft.network.chat.Component.literal(
						"MCXRInput: player list disabled for an unsupported scan-code key binding"));
			}
			return;
		}
		if (client.options.keyPlayerList.isDown()
				|| physicalState == PhysicalMappingState.DOWN) {
			// Never claim or later release an independently held player-list mapping.
			closeWheelScreen(client);
			suppressButtonEdges();
			return;
		}

		closeWheelScreen(client);
		ownsPlayerList = true;
		client.options.keyPlayerList.setDown(true);
	}

	private void closePlayerList(Minecraft client) {
		if (!ownsPlayerList) {
			return;
		}

		ownsPlayerList = false;
		boolean physicallyDown = physicalMappingState(client, client.options.keyPlayerList)
				== PhysicalMappingState.DOWN;
		if (!physicallyDown) {
			client.options.keyPlayerList.setDown(false);
		} else if (!client.options.keyPlayerList.isDown()) {
			client.options.keyPlayerList.setDown(true);
		}
	}

	private static PhysicalMappingState physicalMappingState(
			Minecraft client,
			net.minecraft.client.KeyMapping mapping
	) {
		if (mapping.isUnbound()) {
			return PhysicalMappingState.UP;
		}
		InputConstants.Key key = KeyMappingHelper.getBoundKeyOf(mapping);
		if (key.getType() == InputConstants.Type.KEYSYM) {
			return InputConstants.isKeyDown(client.getWindow(), key.getValue())
					? PhysicalMappingState.DOWN : PhysicalMappingState.UP;
		}
		if (key.getType() == InputConstants.Type.MOUSE) {
			return GLFW.glfwGetMouseButton(client.getWindow().handle(), key.getValue()) == GLFW.GLFW_PRESS
					? PhysicalMappingState.DOWN : PhysicalMappingState.UP;
		}
		return PhysicalMappingState.UNSUPPORTED;
	}

	private static void cyclePerspective(Minecraft client) {
		// This mirrors Minecraft 26.2's own perspective key handler exactly.
		CameraType previous = client.options.getCameraType();
		client.options.setCameraType(previous.cycle());
		CameraType current = client.options.getCameraType();
		if (previous.isFirstPerson() != current.isFirstPerson()) {
			client.gameRenderer.checkEntityPostEffect(
					current.isFirstPerson() ? client.getCameraEntity() : null);
		}
	}

	private void suppressButtonEdges() {
		wheelButton.suppress();
		backButton.suppress();
		wheelWasDown = false;
		backWasDown = false;
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

	private enum PhysicalMappingState {
		UP,
		DOWN,
		UNSUPPORTED
	}
}
