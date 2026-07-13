package dev.mcxrinput.client;

import com.mojang.blaze3d.platform.InputConstants;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientLifecycleEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.keymapping.v1.KeyMappingHelper;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.resources.Identifier;
import org.lwjgl.glfw.GLFW;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;

public final class MCXRInputClient implements ClientModInitializer {
	private static final Logger LOGGER = LoggerFactory.getLogger("MCXRInput");
	private static final KeyMapping.Category CATEGORY = KeyMapping.Category.register(
			Identifier.fromNamespaceAndPath("mcxrinput", "controls")
	);
	private static final KeyMapping RECENTER_KEY = KeyMappingHelper.registerKeyMapping(new KeyMapping(
			"key.mcxrinput.recenter",
			InputConstants.Type.KEYSYM,
			GLFW.GLFW_KEY_R,
			CATEGORY
	));
	private static final KeyMapping TOGGLE_KEY = KeyMappingHelper.registerKeyMapping(new KeyMapping(
			"key.mcxrinput.toggle",
			InputConstants.Type.KEYSYM,
			GLFW.GLFW_KEY_F8,
			CATEGORY
	));
	private static MCXRInputClient activeInstance;

	private VrUdpReceiver receiver;
	private VrCameraController cameraController;
	private VrUtilityWheelController utilityWheelController;
	private VrControllerInputController controllerInputController;
	private VrMenuInputController menuInputController;
	private VrInventoryInputController inventoryInputController;
	private ClientLevel lastLevel;
	private LocalPlayer lastPlayer;
	private boolean toggleAwaitingRelease;

	@Override
	public void onInitializeClient() {
		int port = Integer.getInteger("mcxrinput.port", VrUdpReceiver.DEFAULT_PORT);
		MCXRInputConfig config = MCXRInputConfig.get();
		receiver = new VrUdpReceiver(port);
		cameraController = new VrCameraController(receiver, config);
		utilityWheelController = new VrUtilityWheelController(receiver, config);
		controllerInputController = new VrControllerInputController(receiver, config);
		menuInputController = new VrMenuInputController(receiver, config);
		inventoryInputController = new VrInventoryInputController(receiver, config);
		activeInstance = this;

		try {
			receiver.start();
		} catch (IOException exception) {
			LOGGER.error("Could not start the local VR bridge receiver; VR camera input is unavailable", exception);
		}

		// Apply controller state before Minecraft handles key mappings so physical
		// press edges are consumed in the same gameplay tick. The utility wheel runs
		// first because opening/closing it atomically captures every other VR input.
		ClientTickEvents.START_CLIENT_TICK.register(client -> {
			if (client.level != lastLevel || client.player != lastPlayer) {
				lastLevel = client.level;
				lastPlayer = client.player;
				cameraController.resetForWorldChange();
				releaseAllInputs(client);
				if (client.player != null) {
					String message = client.isMultiplayerServer()
							? "MCXRInput: VR input disabled on join; multiplayer use requires server permission"
							: "MCXRInput: VR input disabled for this world; press F8 to enable";
					client.player.sendOverlayMessage(net.minecraft.network.chat.Component.literal(message));
				}
			}

			// Handle F8 before any controller can inject state this tick. Disabling is
			// atomic and releases every mapping owned by MCXRInput before vanilla polls it.
			boolean toggleClicked = false;
			while (TOGGLE_KEY.consumeClick()) {
				toggleClicked = true;
			}
			if (!TOGGLE_KEY.isDown()) {
				toggleAwaitingRelease = false;
			}
			if (toggleClicked && !toggleAwaitingRelease) {
				toggleAwaitingRelease = true;
				cameraController.toggle(client);
				if (!cameraController.enabled()) {
					releaseAllInputs(client);
				} else if (client.player != null && client.isMultiplayerServer()) {
					client.player.sendOverlayMessage(net.minecraft.network.chat.Component.literal(
							"MCXRInput: enabled manually; vanilla input does not imply server permission"));
				}
			}

			boolean utilityCapturedInput = utilityWheelController.tick(
					client, cameraController.enabled());
			boolean ordinaryInputEnabled = cameraController.enabled() && !utilityCapturedInput;
			controllerInputController.tick(client, ordinaryInputEnabled);
			menuInputController.tick(client, ordinaryInputEnabled);
			inventoryInputController.tick(client, ordinaryInputEnabled);
		});

		ClientTickEvents.END_CLIENT_TICK.register(client -> {
			while (RECENTER_KEY.consumeClick()) {
				cameraController.recenter(client);
			}
		});

		ClientLifecycleEvents.CLIENT_STOPPING.register(client -> {
			activeInstance = null;
			cameraController.resetForWorldChange();
			releaseAllInputs(client);
			receiver.close();
		});
	}

	/**
	 * Compatibility-isolated entry point used by the required GameRenderer mixin.
	 * Only HMD look deltas run here; gameplay keys and discrete actions stay on
	 * Minecraft client ticks.
	 */
	public static void applyHmdCameraForRenderFrame(Minecraft client) {
		MCXRInputClient instance = activeInstance;
		if (instance != null) {
			instance.cameraController.updateForRenderFrame(client);
		}
	}

	private void releaseAllInputs(Minecraft client) {
		utilityWheelController.releaseAll(client);
		controllerInputController.releaseAll(client);
		menuInputController.releaseAll();
		inventoryInputController.releaseAll();
	}

	static boolean isGameplayInputBlocked(Minecraft client) {
		return client.gui.screen() != null || client.gui.overlay() != null;
	}

	static boolean isCameraInputBlocked(Minecraft client) {
		return client.gui.overlay() != null
				|| (client.gui.screen() != null
				&& !(client.gui.screen() instanceof UtilityWheelScreen));
	}
}
