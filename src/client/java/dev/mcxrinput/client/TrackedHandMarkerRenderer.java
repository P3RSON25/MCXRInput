package dev.mcxrinput.client;

import dev.mcxrinput.avatar.AvatarViewTransform;
import dev.mcxrinput.avatar.AvatarViewTransform.Point;
import dev.mcxrinput.presentation.PresentationCalibration;
import dev.mcxrinput.presentation.PresentationOffer;
import dev.mcxrinput.presentation.PresentationState;
import dev.mcxrinput.protocol.VrInputFrame;
import dev.mcxrinput.protocol.VrTrackedPose;
import net.fabricmc.fabric.api.client.rendering.v1.RenderStateDataKey;
import net.fabricmc.fabric.api.client.rendering.v1.level.LevelExtractionContext;
import net.fabricmc.fabric.api.client.rendering.v1.level.LevelExtractionEvents;
import net.fabricmc.fabric.api.client.rendering.v1.level.LevelRenderContext;
import net.fabricmc.fabric.api.client.rendering.v1.level.LevelRenderEvents;
import net.minecraft.client.Camera;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.gizmos.DrawableGizmoPrimitives;
import net.minecraft.world.phys.Vec3;
import org.joml.Quaternionf;
import org.joml.Vector3f;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Development-only hardware alignment marker for validated OpenXR grip poses.
 * This is intentionally not the player-avatar renderer: it proves transport,
 * handedness, tracking loss, and projection calibration before model work is
 * allowed to depend on those coordinates.
 */
final class TrackedHandMarkerRenderer {
	static final String DEVELOPMENT_PROPERTY =
			"mcxrinput.development.trackedHandMarkers";
	private static final Logger LOGGER = LoggerFactory.getLogger(
			"MCXRInput/TrackedHandMarkers");
	private static final Duration MAXIMUM_INPUT_AGE = Duration.ofMillis(250L);
	private static final float CUBE_HALF_EXTENT_METRES = 0.025F;
	private static final float AXIS_LENGTH_METRES = 0.12F;
	private static final float AXIS_WIDTH = 2.0F;
	private static final int LEFT_COLOR = 0xFF00D9FF;
	private static final int RIGHT_COLOR = 0xFFFF9F1C;
	private static final int X_AXIS_COLOR = 0xFFFF4040;
	private static final int Y_AXIS_COLOR = 0xFF40FF40;
	private static final int FORWARD_AXIS_COLOR = 0xFF4080FF;
	private static final RenderStateDataKey<MarkerFrame> FRAME_KEY =
			RenderStateDataKey.create(() -> "mcxrinput:tracked_hand_markers");
	private static final AtomicBoolean REGISTERED = new AtomicBoolean();

	private TrackedHandMarkerRenderer() {
	}

	static void register(VrUdpReceiver receiver) {
		if (!Boolean.getBoolean(DEVELOPMENT_PROPERTY)
				|| !REGISTERED.compareAndSet(false, true)) {
			return;
		}
		LevelExtractionEvents.END_EXTRACTION.register(
				context -> extract(context, receiver));
		LevelRenderEvents.COLLECT_SUBMITS.register(
				TrackedHandMarkerRenderer::submit);
		LOGGER.warn(
				"DEVELOPMENT/SINGLEPLAYER ONLY: tracked-hand alignment markers enabled");
	}

	private static void extract(
			LevelExtractionContext context, VrUdpReceiver receiver) {
		// LevelRenderState instances are recycled. Clearing first prevents a marker
		// from surviving any failed gate, screen transition, or tracking loss.
		context.levelState().setData(FRAME_KEY, MarkerFrame.EMPTY);
		Minecraft client = Minecraft.getInstance();
		if (!isWorldViewEligible(client)
				|| context.level() != client.level
				|| context.camera().entity() != client.player) {
			return;
		}

		PresentationOffer offer = receiver.latestFreshPresentationOffer();
		PresentationCalibration calibration =
				receiver.latestFreshPresentationCalibration();
		VrInputFrame input = receiver.latestFreshFrame(MAXIMUM_INPUT_AGE, false);
		if (offer == null || calibration == null || !calibration.matches(offer)
				|| input == null || input.protocolVersion() != 2
				|| !input.hmd().active()) {
			return;
		}

		List<HandMarker> hands = new ArrayList<>(2);
		if (input.leftGripPose().active()) {
			hands.add(buildHand(
					HandSide.LEFT, input.leftGripPose(), LEFT_COLOR,
					calibration.worldViewScale(), context.camera()));
		}
		if (input.rightGripPose().active()) {
			hands.add(buildHand(
					HandSide.RIGHT, input.rightGripPose(), RIGHT_COLOR,
					calibration.worldViewScale(), context.camera()));
		}
		if (hands.isEmpty()) {
			return;
		}

		context.levelState().setData(
				FRAME_KEY, new MarkerFrame(List.copyOf(hands)));
	}

	private static void submit(LevelRenderContext context) {
		MarkerFrame frame = context.levelState().getData(FRAME_KEY);
		if (frame == null || frame.hands().isEmpty()) {
			return;
		}

		DrawableGizmoPrimitives gizmos = new DrawableGizmoPrimitives();
		for (HandMarker hand : frame.hands()) {
			for (Quad quad : hand.quads()) {
				gizmos.addQuad(quad.a(), quad.b(), quad.c(), quad.d(), quad.color());
			}
			for (Line line : hand.lines()) {
				gizmos.addLine(line.start(), line.end(), line.color(), line.width());
			}
		}
		// false selects Minecraft's ordinary depth-tested gizmo phase. These
		// diagnostics must never draw through blocks or act like a controller ray.
		gizmos.submit(
				context.submitNodeCollector(),
				context.levelState().cameraRenderState,
				false);
	}

	private static boolean isWorldViewEligible(Minecraft client) {
		return client.level != null
				&& client.player != null
				&& !client.isMultiplayerServer()
				&& client.options.getCameraType().isFirstPerson()
				&& ImmersivePresentationController.classify(client)
						== PresentationState.WORLD;
	}

	private static HandMarker buildHand(
			HandSide side,
			VrTrackedPose pose,
			int cubeColor,
			float worldViewScale,
			Camera camera) {
		Quaternionf gripRotation = new Quaternionf(
				(float) pose.rotationX(), (float) pose.rotationY(),
				(float) pose.rotationZ(), (float) pose.rotationW());
		Vec3[] cube = new Vec3[8];
		int index = 0;
		for (int z = -1; z <= 1; z += 2) {
			for (int y = -1; y <= 1; y += 2) {
				for (int x = -1; x <= 1; x += 2) {
					cube[index++] = transformPoint(
							pose, gripRotation,
							x * CUBE_HALF_EXTENT_METRES,
							y * CUBE_HALF_EXTENT_METRES,
							z * CUBE_HALF_EXTENT_METRES,
							worldViewScale, camera);
				}
			}
		}

		// Loop order above produces [---,+--,-+-,++-,--+,+-+,-++,+++].
		List<Quad> quads = List.of(
				quad(cube, 0, 2, 3, 1, cubeColor),
				quad(cube, 4, 5, 7, 6, cubeColor),
				quad(cube, 0, 4, 6, 2, cubeColor),
				quad(cube, 1, 3, 7, 5, cubeColor),
				quad(cube, 0, 1, 5, 4, cubeColor),
				quad(cube, 2, 6, 7, 3, cubeColor));

		Vec3 origin = transformPoint(
				pose, gripRotation, 0.0F, 0.0F, 0.0F,
				worldViewScale, camera);
		List<Line> lines = List.of(
				new Line(origin, transformPoint(
						pose, gripRotation, AXIS_LENGTH_METRES, 0.0F, 0.0F,
						worldViewScale, camera), X_AXIS_COLOR, AXIS_WIDTH),
				new Line(origin, transformPoint(
						pose, gripRotation, 0.0F, AXIS_LENGTH_METRES, 0.0F,
						worldViewScale, camera), Y_AXIS_COLOR, AXIS_WIDTH),
				new Line(origin, transformPoint(
						pose, gripRotation, 0.0F, 0.0F, -AXIS_LENGTH_METRES,
						worldViewScale, camera), FORWARD_AXIS_COLOR, AXIS_WIDTH));
		return new HandMarker(side, quads, lines);
	}

	private static Quad quad(
			Vec3[] vertices, int a, int b, int c, int d, int color) {
		return new Quad(vertices[a], vertices[b], vertices[c], vertices[d], color);
	}

	private static Vec3 transformPoint(
			VrTrackedPose pose,
			Quaternionf gripRotation,
			float localX,
			float localY,
			float localZ,
			float worldViewScale,
			Camera camera) {
		Vector3f relative = new Vector3f(localX, localY, localZ)
				.rotate(gripRotation)
				.add(
						(float) pose.positionX(),
						(float) pose.positionY(),
						(float) pose.positionZ());
		Point sourcePoint = AvatarViewTransform.compensateCameraPoint(
				relative.x, relative.y, relative.z, worldViewScale);
		Vector3f worldOffset = new Vector3f(
				(float) sourcePoint.x(),
				(float) sourcePoint.y(),
				(float) sourcePoint.z())
				.rotate(camera.rotation());
		return camera.position().add(new Vec3(worldOffset));
	}

	private enum HandSide {
		LEFT,
		RIGHT
	}

	private record MarkerFrame(List<HandMarker> hands) {
		private static final MarkerFrame EMPTY = new MarkerFrame(List.of());
	}

	private record HandMarker(
			HandSide side,
			List<Quad> quads,
			List<Line> lines) {
	}

	private record Quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, int color) {
	}

	private record Line(Vec3 start, Vec3 end, int color, float width) {
	}
}
