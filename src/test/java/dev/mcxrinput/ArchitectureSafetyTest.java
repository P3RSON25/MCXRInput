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
		String inputParser = Files.readString(Path.of(
				"src", "main", "java", "dev", "mcxrinput", "protocol", "BridgeInputParser.java"));
		assertTrue(receiver.contains("BridgeInputParser.parse("));
		assertTrue(inputParser.contains("BridgeProtocolPolicy.accepts("),
				"The receiver's input parser must apply production protocol-version policy");
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
				< receiver.indexOf("BridgeInputParser.parse("),
				"MCXRD1 messages must be recognized before JSON input frames");
		assertTrue(receiver.contains("activeSocket.send(new DatagramPacket("));
		assertTrue(receiver.contains("\"127.0.0.1\".equals(address.getHostAddress())"));
		assertTrue(receiver.contains("PresentationEndpointPolicy.mayAcceptOffer("),
				"A fresh display bridge must retain exclusive endpoint ownership");

		String initializer = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "client", "MCXRInputClient.java"));
		assertEquals(1, count(initializer, "presentationController.tick(client);"),
				"Presentation heartbeat must be emitted at most once per client tick");

		String config = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "client", "MCXRInputConfig.java"));
		assertTrue(config.contains("CONFIG_VERSION = 9"));
		assertTrue(config.contains("migrateAutomaticEnabled("));
	}

	@Test
	void trackedHandMarkerIsDevelopmentOnlyAndFailClosed() throws IOException {
		String renderer = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "client",
				"TrackedHandMarkerRenderer.java"));
		assertTrue(renderer.contains("Boolean.getBoolean(DEVELOPMENT_PROPERTY)"));
		assertTrue(renderer.contains("!client.isMultiplayerServer()"));
		assertTrue(renderer.contains("PresentationState.WORLD"));
		assertTrue(renderer.contains("latestFreshPresentationCalibration()"));
		assertTrue(renderer.contains("latestFreshFrame(MAXIMUM_INPUT_AGE, false)"));
		assertTrue(renderer.contains(
				"context.levelState().setData(FRAME_KEY, MarkerFrame.EMPTY)"),
				"A recycled render state must clear old tracked-hand geometry first");
		assertTrue(renderer.contains("context.levelState().cameraRenderState"));
		assertTrue(renderer.contains("false);"),
				"Development markers must remain depth tested instead of drawing through blocks");
		assertFalse(renderer.contains("setAlwaysOnTop"));
		assertFalse(renderer.contains("Server" + "bound"));
		int submitStart = renderer.indexOf("private static void submit(");
		int submitEnd = renderer.indexOf(
				"private static boolean isWorldViewEligible", submitStart);
		assertTrue(submitStart >= 0 && submitEnd > submitStart);
		String submitMethod = renderer.substring(submitStart, submitEnd);
		assertFalse(submitMethod.contains("Minecraft.getInstance()"),
				"Submission must consume only the immutable extracted render state");
		assertFalse(submitMethod.contains("receiver."),
				"Submission must not mix a newer pose generation with extracted geometry");
	}

	@Test
	void trackedAvatarIsExactFrameCosmeticAndFailClosed() throws IOException {
		String renderer = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "client",
				"TrackedAvatarRenderer.java"));
		assertTrue(renderer.contains("mcxrinput.development.trackedAvatar"));
		assertTrue(renderer.contains("Boolean.getBoolean(DEVELOPMENT_PROPERTY)"));
		assertTrue(renderer.contains("client.isMultiplayerServer()"));
		assertTrue(renderer.contains("PresentationState.WORLD"));
		assertTrue(renderer.contains("latestFreshPresentationCalibration()"));
		assertTrue(renderer.contains("latestFreshFrame(MAXIMUM_INPUT_AGE, false)"));
		assertTrue(renderer.contains("input.protocolVersion() != 2"));
		assertTrue(renderer.contains("!input.hmd().active()"));
		assertTrue(renderer.contains(
				"context.levelState().setData(FRAME_KEY, AvatarFrame.EMPTY)"),
				"A recycled render state must restore vanilla hands before every gate");
		for (String unsupportedState : List.of(
				"isSpectator()", "isInvisible()", "isSleeping()", "isSwimming()",
				"isFallFlying()", "isAutoSpinAttack()", "isScoping()")) {
			assertTrue(renderer.contains(unsupportedState),
					"Missing conservative avatar pose gate: " + unsupportedState);
		}

		assertTrue(renderer.contains("player.getItemHeldByArm(arm)"),
				"Physical controller side must respect Minecraft's configured main arm");
		assertTrue(renderer.contains("getItemModelResolver().updateForTopItem("),
				"Held-item models must be resolved once during extraction");
		int itemResolver = renderer.indexOf("getItemModelResolver().updateForTopItem(");
		int nullOwner = renderer.indexOf("null,", itemResolver);
		int deterministicSeed = renderer.indexOf(
				"player.getId() + displayContext.ordinal()", itemResolver);
		assertTrue(itemResolver >= 0 && nullOwner > itemResolver
				&& deterministicSeed > nullOwner,
				"Tracked items must not observe live attack/use owner predicates");
		assertFalse(renderer.contains("updateForLiving("));
		assertTrue(renderer.contains("MAXIMUM_REACH_CLAMP_METRES = 0.05F"));
		assertTrue(renderer.contains("ReachStatus.CLAMPED_TOO_CLOSE"));
		assertTrue(renderer.contains(
				"solution.requestedDistance() - solution.solvedDistance()"),
				"Overreach tolerance must be explicit and bounded instead of stretching");
		assertTrue(renderer.contains("solution.wrist().subtract(vec(wristOffset))"),
				"A bounded clamped arm must keep its rigid item attached to the solved wrist");
		assertTrue(renderer.contains("RenderTypes.entityTranslucent(frame.skinTexture())"));
		assertFalse(renderer.contains("setAlwaysOnTop"));

		int submitStart = renderer.indexOf("private static void submit(");
		int submitEnd = renderer.indexOf("private static void submitArm(", submitStart);
		assertTrue(submitStart >= 0 && submitEnd > submitStart);
		String submitMethod = renderer.substring(submitStart, submitEnd);
		assertFalse(submitMethod.contains("Minecraft.getInstance()"),
				"Submission must use only its exact immutable extracted frame");
		assertFalse(submitMethod.contains("receiver."),
				"Submission must not combine different receiver generations");
		for (String gameplayCall : List.of(
				".attack(", ".swing(", ".startUsingItem(", ".setDown(")) {
			assertFalse(renderer.contains(gameplayCall),
					"Cosmetic avatar renderer must not create gameplay input: " + gameplayCall);
		}

		String modelParts = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "client",
				"TrackedArmModelParts.java"));
		assertTrue(modelParts.contains("PlayerModelType.SLIM"));
		assertTrue(modelParts.contains("new TextureOrigin(40, 16)"));
		assertTrue(modelParts.contains("new TextureOrigin(40, 32)"));
		assertTrue(modelParts.contains("new TextureOrigin(32, 48)"));
		assertTrue(modelParts.contains("new TextureOrigin(48, 48)"));
		assertTrue(modelParts.contains("texture.v() + 6"),
				"The lower segment must use the lower six rows of each arm texture");
		assertTrue(modelParts.contains("SLEEVE_INFLATION_PIXELS = 0.25F"));
	}

	@Test
	void trackedAvatarSuppressesVanillaHandsForTheExactRenderState() throws IOException {
		String mixin = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "mixin", "client",
				"GameRendererMixin.java"));
		assertTrue(mixin.contains(
				"renderItemInHand(Lnet/minecraft/client/renderer/state/level/CameraRenderState;"
						+ "FLorg/joml/Matrix4fc;)V"));
		assertTrue(mixin.contains("at = @At(\"HEAD\")"));
		assertTrue(mixin.contains("cancellable = true"));
		assertTrue(mixin.contains("require = 1"));
		assertTrue(mixin.contains("gameRenderState.levelRenderState"),
				"Vanilla suppression must inspect the exact extracted level render state");
		assertEquals(1, count(mixin, "TrackedAvatarRenderer.replacesVanillaHands("));
		assertEquals(1, count(mixin, "callbackInfo.cancel();"));
	}

	@Test
	void survivalHudLayersShareTheBottomCenterFit() throws IOException {
		String hud = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "client", "VrHudSafeArea.java"));
		for (String element : List.of(
				"HOTBAR",
				"ARMOR_BAR",
				"HEALTH_BAR",
				"FOOD_BAR",
				"AIR_BAR",
				"MOUNT_HEALTH",
				"INFO_BAR",
				"EXPERIENCE_LEVEL")) {
			assertTrue(hud.contains(
					"wrapFittedBottomCenter(config, receiver, VanillaHudElements." + element + ");"),
					"Bottom-center survival HUD element lost its shared fit: " + element);
		}
		assertEquals(8, count(hud,
				"wrapFittedBottomCenter(config, receiver, VanillaHudElements."),
				"Only the isolated vanilla bottom-center gameplay/status layers should be fitted");

		String mixinConfig = Files.readString(Path.of(
				"src", "client", "resources", "mcxrinput.client.mixins.json"));
		assertTrue(mixinConfig.contains("\"HudContextualBarMixin\""),
				"The exact locator-detail alignment hook must remain registered");
		String mixin = Files.readString(Path.of(
				"src", "client", "java", "dev", "mcxrinput", "mixin", "client",
				"HudContextualBarMixin.java"));
		assertTrue(mixin.contains(
				"method = \"extractHotbarAndDecorations(Lnet/minecraft/client/gui/GuiGraphicsExtractor;"
						+ "Lnet/minecraft/client/DeltaTracker;)V\""));
		assertTrue(mixin.contains(
				"target = \"Lnet/minecraft/client/gui/contextualbar/ContextualBar;"
						+ "extractRenderState(Lnet/minecraft/client/gui/GuiGraphicsExtractor;"
						+ "Lnet/minecraft/client/DeltaTracker;)V\""));
		assertTrue(mixin.contains("@WrapOperation("),
				"Contextual details must compose with other HUD/waypoint wrappers");
		assertTrue(mixin.contains("Operation<Void> original"));
		assertEquals(1, count(mixin, "original.call(contextualBar,"),
				"The wrapped contextual operation must be forwarded exactly once");
		assertFalse(mixin.contains("contextualBar.extractRenderState("),
				"Calling the contextual bar directly would bypass other operation wrappers");
		assertTrue(mixin.contains("require = 1"));
		assertEquals(1, count(mixin, "VrHudSafeArea.extractFittedContextualDetails("),
				"Contextual detail extraction must be wrapped exactly once");
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
