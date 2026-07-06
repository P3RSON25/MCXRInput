package dev.mcxrinput.client;

import com.mojang.blaze3d.platform.InputConstants;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientLifecycleEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.keymapping.v1.KeyMappingHelper;
import net.minecraft.client.KeyMapping;
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

	private VrUdpReceiver receiver;
	private VrCameraController cameraController;

	@Override
	public void onInitializeClient() {
		int port = Integer.getInteger("mcxrinput.port", VrUdpReceiver.DEFAULT_PORT);
		receiver = new VrUdpReceiver(port);
		cameraController = new VrCameraController(receiver);

		try {
			receiver.start();
		} catch (IOException exception) {
			LOGGER.error("Could not start the local VR bridge receiver; VR camera input is unavailable", exception);
		}

		ClientTickEvents.END_CLIENT_TICK.register(client -> {
			while (RECENTER_KEY.consumeClick()) {
				cameraController.recenter(client);
			}
			while (TOGGLE_KEY.consumeClick()) {
				cameraController.toggle(client);
			}
			cameraController.tick(client);
		});

		ClientLifecycleEvents.CLIENT_STOPPING.register(client -> receiver.close());
	}
}
