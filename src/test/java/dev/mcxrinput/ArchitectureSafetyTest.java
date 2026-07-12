package dev.mcxrinput;

import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

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
