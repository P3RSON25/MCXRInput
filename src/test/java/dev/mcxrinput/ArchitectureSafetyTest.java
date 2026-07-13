package dev.mcxrinput;

import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ArchitectureSafetyTest {
	@Test
	void clientSourcesContainNoCustomGameplayPacketHooks() throws IOException {
		String forbiddenPacketType = "Server" + "bound";
		String forbiddenConnectionHook = "ClientPacket" + "Listener";

		List<Path> violations = productionJavaSources().stream()
					.filter(path -> contains(path, forbiddenPacketType)
							|| contains(path, forbiddenConnectionHook))
					.toList();
		assertTrue(violations.isEmpty(),
				"Production gameplay packet imports/hooks are forbidden: " + violations);
	}

	@Test
	void timedStickRepeatIsRestrictedToOrdinaryMenuFocus() throws IOException {
		List<Path> violations = productionJavaSources().stream()
					.filter(path -> !path.getFileName().toString().equals("VrMenuInputController.java"))
					.filter(path -> contains(path, "new StickDpadRepeater("))
					.toList();
		assertTrue(violations.isEmpty(),
				"Timed stick repeat is allowed only for non-gameplay menu focus: " + violations);
	}

	@Test
	void receiverAppliesBothProtocolAndWorldPolicies() throws IOException {
		String receiver = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "client", "VrUdpReceiver.java"));
		assertTrue(receiver.contains("BridgeProtocolPolicy.accepts("),
				"VrUdpReceiver must apply production protocol-version policy while parsing");
		assertTrue(receiver.contains("BridgeProtocolPolicy.allowsInWorld("),
				"VrUdpReceiver must reject development v1 frames in multiplayer worlds");
	}

	@Test
	void failClosedLifecycleAndToggleGatesRemainWired() throws IOException {
		String controller = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "client",
				"VrControllerInputController.java"));
		for (String option : List.of(
				"toggleCrouch()", "toggleSprint()", "toggleAttack()", "toggleUse()")) {
			assertTrue(controller.contains(option), "Missing fail-closed option gate: " + option);
		}
		assertTrue(count(controller, "releaseAll(client);") >= 3,
				"Disabled, screen/disconnect, stale, and tracking-loss branches must release ownership");

		String initializer = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "client", "MCXRInputClient.java"));
		assertTrue(initializer.contains("cameraController.resetForWorldChange();"));
		assertTrue(initializer.contains("if (!cameraController.enabled())"));
		assertTrue(initializer.contains("ClientLifecycleEvents.CLIENT_STOPPING"));
		assertTrue(count(initializer, "releaseAllInputs(client);") >= 3,
				"World changes, F8 disable, and client shutdown must release all owned input");
	}

	@Test
	void hmdCameraRunsOnceAtTheRequiredRenderFrameHook() throws IOException {
		String mixinConfig = Files.readString(Path.of(
				"src", "client", "resources", "mcxrinput.client.mixins.json"));
		assertTrue(mixinConfig.contains("\"GameRendererMixin\""),
				"The required render-frame camera mixin must remain registered");

		String mixin = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "mixin", "client",
				"GameRendererMixin.java"));
		assertTrue(mixin.contains("method = \"update(Lnet/minecraft/client/DeltaTracker;)V\""));
		assertTrue(mixin.contains(
				"target = \"Lnet/minecraft/client/Camera;update(Lnet/minecraft/client/DeltaTracker;)V\""));
		assertTrue(mixin.contains("shift = At.Shift.BEFORE"));
		assertTrue(mixin.contains("require = 1"));
		assertEquals(1, count(mixin, "MCXRInputClient.applyHmdCameraForRenderFrame("),
				"The render hook must apply the HMD camera at most once per camera update");

		String initializer = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "client", "MCXRInputClient.java"));
		assertFalse(initializer.contains("cameraController.tick(client);"),
				"HMD camera application must not also remain on the 20 Hz client-tick path");
		assertEquals(1, count(initializer, "cameraController.updateForRenderFrame(client);"));

		String camera = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "client", "VrCameraController.java"));
		assertTrue(camera.contains("client.level != enabledLevel || client.player != enabledPlayer"),
				"Render frames must reject an anchor from a replaced world/player identity");
		assertTrue(camera.contains("player.yRotO + update.appliedYawDeltaDegrees()"));
		assertTrue(camera.contains("player.xRotO + update.appliedPitchDeltaDegrees()"));
	}

	@Test
	void immersiveFovLockIsCompatibilityIsolatedAndOfferGated() throws IOException {
		String mixinConfig = Files.readString(Path.of(
				"src", "client", "resources", "mcxrinput.client.mixins.json"));
		assertTrue(mixinConfig.contains("\"CameraFovMixin\""),
				"The isolated Camera FOV mixin must remain registered");

		String mixin = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "mixin", "client",
				"CameraFovMixin.java"));
		assertTrue(mixin.contains("method = \"calculateFov(F)F\""));
		assertTrue(mixin.contains("at = @At(\"RETURN\")"));
		assertTrue(mixin.contains("cancellable = true"));
		assertTrue(mixin.contains("require = 1"));

		String controller = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "client",
				"ImmersivePresentationController.java"));
		assertTrue(controller.contains("latestFreshPresentationOffer()"));
		assertTrue(controller.contains("PresentationState.WORLD"));
		assertFalse(controller.contains("options.fov().set"),
				"Presentation calibration must never rewrite options.txt");
	}

	@Test
	void presentationMessagesStayDisplayOnlyAndUseTheBoundLoopbackSocket() throws IOException {
		String receiver = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "client", "VrUdpReceiver.java"));
		assertTrue(receiver.indexOf("PresentationProtocol.hasPresentationPrefix(")
				< receiver.indexOf("GSON.fromJson("),
				"MCXRD1 messages must be recognized before JSON input frames");
		assertTrue(receiver.contains("activeSocket.send(new DatagramPacket("));
		assertTrue(receiver.contains("\"127.0.0.1\".equals(address.getHostAddress())"));

		String initializer = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "client", "MCXRInputClient.java"));
		assertEquals(1, count(initializer, "presentationController.tick(client);"),
				"Presentation heartbeat must be emitted at most once per client tick");

		String config = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "client", "MCXRInputConfig.java"));
		assertTrue(config.contains("CONFIG_VERSION = 9"));
		assertTrue(config.contains("migrateAutomaticEnabled("));
	}

	private static List<Path> productionJavaSources() throws IOException {
		List<Path> paths = new ArrayList<>();
		for (Path root : List.of(
				Path.of("src", "main", "java"),
				Path.of("src", "client", "java"))) {
			try (var walk = Files.walk(root)) {
				walk.filter(path -> path.toString().endsWith(".java")).forEach(paths::add);
			}
		}
		return List.copyOf(paths);
	}

	private static int count(String text, String needle) {
		return (text.length() - text.replace(needle, "").length()) / needle.length();
	}

	private static boolean contains(Path path, String text) {
		try {
			return Files.readString(path).contains(text);
		} catch (IOException exception) {
			throw new IllegalStateException("Could not inspect " + path, exception);
		}
	}
}
