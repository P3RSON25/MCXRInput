package dev.mcxrinput.client;

import com.mojang.blaze3d.vertex.PoseStack;
import com.mojang.math.Axis;
import dev.mcxrinput.avatar.TwoBoneIkSolver;
import dev.mcxrinput.avatar.TwoBoneIkSolver.Basis;
import dev.mcxrinput.avatar.TwoBoneIkSolver.ReachStatus;
import dev.mcxrinput.avatar.TwoBoneIkSolver.Solution;
import dev.mcxrinput.avatar.TwoBoneIkSolver.Vec3;
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
import net.minecraft.client.model.geom.ModelPart;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.client.renderer.item.ItemStackRenderState;
import net.minecraft.client.renderer.rendertype.RenderTypes;
import net.minecraft.client.renderer.state.level.LevelRenderState;
import net.minecraft.client.renderer.texture.OverlayTexture;
import net.minecraft.resources.Identifier;
import net.minecraft.world.entity.HumanoidArm;
import net.minecraft.world.entity.Pose;
import net.minecraft.world.entity.player.PlayerModelPart;
import net.minecraft.world.entity.player.PlayerModelType;
import net.minecraft.world.entity.player.PlayerSkin;
import net.minecraft.world.item.ItemDisplayContext;
import net.minecraft.world.item.ItemStack;
import org.joml.Matrix3f;
import org.joml.Quaternionf;
import org.joml.Vector3f;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Development-only first-person cosmetic arms driven by validated OpenXR grip
 * poses. The extracted frame contains all skin, item, pose, and lighting data;
 * submission never observes a newer player or receiver generation.
 *
 * <p>This renderer is deliberately presentation-only. It does not read attack,
 * use, or swing state and never changes Minecraft input or packet behavior.</p>
 */
public final class TrackedAvatarRenderer {
	static final String DEVELOPMENT_PROPERTY =
			"mcxrinput.development.trackedAvatar";
	private static final Logger LOGGER = LoggerFactory.getLogger(
			"MCXRInput/TrackedAvatar");
	private static final Duration MAXIMUM_INPUT_AGE = Duration.ofMillis(250L);
	private static final long FAILURE_LOG_INTERVAL_NANOS = Duration.ofSeconds(5L).toNanos();
	private static final float UPPER_ARM_LENGTH_METRES = 0.375F;
	private static final float LOWER_ARM_LENGTH_METRES = 0.375F;
	private static final float MAXIMUM_REACH_CLAMP_METRES = 0.05F;
	private static final float HALF_SHOULDER_WIDTH_METRES = 0.19F;
	private static final float SHOULDER_DROP_METRES = 0.22F;
	private static final float SHOULDER_BACK_METRES = 0.04F;
	private static final float ELBOW_OUTWARD_METRES = 0.48F;
	private static final float ELBOW_DROP_METRES = 0.20F;
	private static final float ELBOW_BACK_METRES = 0.12F;
	private static final float GRIP_TO_WRIST_METRES = 0.055F;
	private static final float MINIMUM_GRIP_FORWARD_METRES = 0.08F;
	private static final RenderStateDataKey<AvatarFrame> FRAME_KEY =
			RenderStateDataKey.create(() -> "mcxrinput:tracked_avatar");
	private static final AtomicBoolean REGISTERED = new AtomicBoolean();
	private static final AtomicLong LAST_FAILURE_LOG = new AtomicLong();

	private TrackedAvatarRenderer() {
	}

	static void register(VrUdpReceiver receiver) {
		if (!Boolean.getBoolean(DEVELOPMENT_PROPERTY)
				|| !REGISTERED.compareAndSet(false, true)) {
			return;
		}
		LevelExtractionEvents.END_EXTRACTION.register(
				context -> extract(context, receiver));
		LevelRenderEvents.COLLECT_SUBMITS.register(TrackedAvatarRenderer::submit);
		LOGGER.warn(
				"DEVELOPMENT/SINGLEPLAYER ONLY: tracked cosmetic arms and rigid held items enabled");
	}

	private static void extract(
			LevelExtractionContext context, VrUdpReceiver receiver) {
		// Render states are recycled. Clear before every gate so screens, tracking
		// loss, disconnects, and extraction failures restore vanilla hands.
		context.levelState().setData(FRAME_KEY, AvatarFrame.EMPTY);
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

		try {
			LocalPlayer player = client.player;
			Camera camera = context.camera();
			Quaternionf cameraRotation = new Quaternionf(camera.rotation());
			ShoulderFrame shoulders = shoulderFrame(cameraRotation);
			PlayerSkin skin = player.getSkin();
			int packedLight = client.getEntityRenderDispatcher().getPackedLightCoords(
					player,
					context.deltaTracker().getGameTimeDeltaPartialTick(false));

			List<ArmFrame> arms = new ArrayList<>(2);
			extractArm(
					client, player, HumanoidArm.LEFT, input.leftGripPose(),
					shoulders, arms);
			extractArm(
					client, player, HumanoidArm.RIGHT, input.rightGripPose(),
					shoulders, arms);

			// Per-hand loss intentionally leaves that cosmetic hand absent while
			// keeping vanilla suppression active. Falling back one hand would create
			// an unrelated untracked hand beside the remaining tracked arm.
			context.levelState().setData(FRAME_KEY, new AvatarFrame(
					true,
					List.copyOf(arms),
					skin.body().texturePath(),
					skin.model(),
					cameraRotation,
					calibration.worldViewScale(),
					packedLight));
		} catch (RuntimeException exception) {
			logFailure("Could not extract the tracked cosmetic avatar; using vanilla hands", exception);
		}
	}

	private static void extractArm(
			Minecraft client,
			LocalPlayer player,
			HumanoidArm arm,
			VrTrackedPose grip,
			ShoulderFrame shoulders,
			List<ArmFrame> output) {
		if (!grip.active() || grip.positionZ() > -MINIMUM_GRIP_FORWARD_METRES) {
			return;
		}

		Quaternionf gripRotation = new Quaternionf(
				(float) grip.rotationX(),
				(float) grip.rotationY(),
				(float) grip.rotationZ(),
				(float) grip.rotationW());
		Vector3f wristOffset = new Vector3f(0.0F, 0.0F, GRIP_TO_WRIST_METRES)
				.rotate(gripRotation);
		Vec3 wristTarget = new Vec3(
				grip.positionX() + wristOffset.x,
				grip.positionY() + wristOffset.y,
				grip.positionZ() + wristOffset.z);
		Vec3 shoulder = arm == HumanoidArm.LEFT
				? shoulders.leftShoulder()
				: shoulders.rightShoulder();
		Vec3 elbowPole = arm == HumanoidArm.LEFT
				? shoulders.leftElbowPole()
				: shoulders.rightElbowPole();
		Solution solution = TwoBoneIkSolver.solve(
				shoulder,
				wristTarget,
				elbowPole,
				UPPER_ARM_LENGTH_METRES,
				LOWER_ARM_LENGTH_METRES);
		if (solution.reachStatus() == ReachStatus.CLAMPED_TOO_CLOSE
				|| (solution.reachStatus() == ReachStatus.CLAMPED_TOO_FAR
						&& solution.requestedDistance() - solution.solvedDistance()
								> MAXIMUM_REACH_CLAMP_METRES)) {
			return;
		}

		Basis lowerBasis = applyGripTwist(solution.lowerBasis(), gripRotation);
		// Within the small overreach tolerance the solver preserves fixed arm
		// lengths and moves the cosmetic attachment inward with the solved wrist.
		// This prevents tracking noise at full extension from flashing the arm,
		// without stretching it or leaving the rigid item detached.
		Vec3 itemGripPosition = solution.wrist().subtract(vec(wristOffset));
		ItemStackRenderState itemState = new ItemStackRenderState();
		ItemStack heldItem = player.getItemHeldByArm(arm);
		if (!heldItem.isEmpty()) {
			ItemDisplayContext displayContext = arm == HumanoidArm.LEFT
					? ItemDisplayContext.THIRD_PERSON_LEFT_HAND
					: ItemDisplayContext.THIRD_PERSON_RIGHT_HAND;
			// A null ItemOwner deliberately prevents live attack/use predicates from
			// driving this checkpoint's item model. Level and stack state remain
			// available, while placement stays rigid at the tracked grip.
			client.getItemModelResolver().updateForTopItem(
					itemState,
					heldItem.copy(),
					displayContext,
					player.level(),
					null,
					player.getId() + displayContext.ordinal());
		}

		boolean sleeveVisible = player.isModelPartShown(
				arm == HumanoidArm.LEFT
						? PlayerModelPart.LEFT_SLEEVE
						: PlayerModelPart.RIGHT_SLEEVE);
		output.add(new ArmFrame(
				arm,
				solution,
				lowerBasis,
				grip,
				itemGripPosition,
				sleeveVisible,
				itemState));
	}

	private static void submit(LevelRenderContext context) {
		AvatarFrame frame = context.levelState().getData(FRAME_KEY);
		if (frame == null || !frame.replaceVanillaHands()) {
			return;
		}

		for (ArmFrame arm : frame.arms()) {
			try {
				submitArm(context, frame, arm);
			} catch (RuntimeException exception) {
				// Keep the exact frame's vanilla suppression active. A partially
				// submitted tracked rig plus vanilla hands would be more misleading
				// than omitting the failed cosmetic hand for this one frame.
				logFailure("Could not submit one tracked cosmetic arm", exception);
			}
		}
	}

	private static void submitArm(
			LevelRenderContext context,
			AvatarFrame frame,
			ArmFrame arm) {
		Solution solution = arm.solution();
		submitSegment(
				context,
				frame,
				arm,
				TrackedArmModelParts.Segment.UPPER,
				solution.shoulder(),
				solution.upperBasis(),
				distance(solution.shoulder(), solution.elbow()));
		submitSegment(
				context,
				frame,
				arm,
				TrackedArmModelParts.Segment.LOWER,
				solution.elbow(),
				arm.lowerBasis(),
				distance(solution.elbow(), solution.wrist()));

		if (!arm.itemState().isEmpty()) {
			PoseStack poseStack = context.poseStack();
			poseStack.pushPose();
			try {
				applyViewTransform(poseStack, frame);
				VrTrackedPose grip = arm.grip();
				Vec3 itemGripPosition = arm.itemGripPosition();
				poseStack.translate(
						itemGripPosition.x(),
						itemGripPosition.y(),
						itemGripPosition.z());
				poseStack.mulPose(new Quaternionf(
						(float) grip.rotationX(),
						(float) grip.rotationY(),
						(float) grip.rotationZ(),
						(float) grip.rotationW()));
				// Correct the model's facing around the OpenXR grip's longitudinal
				// axis. This must precede Minecraft's held-item calibration below:
				// placing the same rotation afterward changes axes and turns the item
				// upside down instead of making asymmetric tool heads face outward.
				poseStack.mulPose(Axis.ZP.rotationDegrees(180.0F));
				// Minecraft's third-person held-item model expects its handle axis in
				// the conventional player-hand orientation. The item remains rigidly
				// attached to the tracked (or bounded solved) grip pose; no swing/use
				// state is sampled.
				poseStack.mulPose(Axis.XP.rotationDegrees(-90.0F));
				poseStack.mulPose(Axis.YP.rotationDegrees(180.0F));
				arm.itemState().submit(
						poseStack,
						context.submitNodeCollector(),
						frame.packedLight(),
						OverlayTexture.NO_OVERLAY,
						0);
			} finally {
				poseStack.popPose();
			}
		}
	}

	private static void submitSegment(
			LevelRenderContext context,
			AvatarFrame frame,
			ArmFrame arm,
			TrackedArmModelParts.Segment segment,
			Vec3 start,
			Basis basis,
			double length) {
		PoseStack poseStack = context.poseStack();
		poseStack.pushPose();
		try {
			applyViewTransform(poseStack, frame);
			poseStack.translate(start.x(), start.y(), start.z());
			poseStack.mulPose(basisQuaternion(basis));
			poseStack.scale(
					1.0F,
					(float) (length / TrackedArmModelParts.CANONICAL_SEGMENT_LENGTH_BLOCKS),
					1.0F);

			ModelPart modelPart = TrackedArmModelParts.segment(
					frame.modelType(), arm.arm(), segment, arm.sleeveVisible());
			context.submitNodeCollector().submitModelPart(
					modelPart,
					poseStack,
					RenderTypes.entityTranslucent(frame.skinTexture()),
					frame.packedLight(),
					OverlayTexture.NO_OVERLAY,
					null);
		} finally {
			poseStack.popPose();
		}
	}

	private static void applyViewTransform(PoseStack poseStack, AvatarFrame frame) {
		poseStack.mulPose(frame.cameraRotation());
		float inverseScale = 1.0F / frame.worldViewScale();
		poseStack.scale(inverseScale, inverseScale, 1.0F);
	}

	private static ShoulderFrame shoulderFrame(Quaternionf cameraRotation) {
		Quaternionf inverseCamera = new Quaternionf(cameraRotation).conjugate();
		Vector3f upVector = new Vector3f(0.0F, 1.0F, 0.0F)
				.rotate(inverseCamera)
				.normalize();
		Vec3 up = vec(upVector);
		Vec3 down = up.scale(-1.0);
		Vec3 right = Vec3.UNIT_X;
		Vec3 back = right.cross(up).normalized();
		Vec3 shoulderCenter = down.scale(SHOULDER_DROP_METRES)
				.add(back.scale(SHOULDER_BACK_METRES));
		Vec3 leftShoulder = shoulderCenter.add(right.scale(-HALF_SHOULDER_WIDTH_METRES));
		Vec3 rightShoulder = shoulderCenter.add(right.scale(HALF_SHOULDER_WIDTH_METRES));
		Vec3 poleOffset = down.scale(ELBOW_DROP_METRES)
				.add(back.scale(ELBOW_BACK_METRES));
		return new ShoulderFrame(
				leftShoulder,
				rightShoulder,
				leftShoulder.add(poleOffset).add(right.scale(-ELBOW_OUTWARD_METRES)),
				rightShoulder.add(poleOffset).add(right.scale(ELBOW_OUTWARD_METRES)));
	}

	private static Basis applyGripTwist(Basis fallback, Quaternionf gripRotation) {
		Vec3 longitudinal = fallback.yAxis();
		Vector3f gripRightVector = new Vector3f(1.0F, 0.0F, 0.0F)
				.rotate(gripRotation);
		Vec3 gripRight = vec(gripRightVector);
		Vec3 projected = gripRight.subtract(
				longitudinal.scale(gripRight.dot(longitudinal)));
		if (projected.length() < 1.0e-6) {
			return fallback;
		}
		Vec3 xAxis = projected.normalized();
		return new Basis(xAxis, longitudinal, xAxis.cross(longitudinal).normalized());
	}

	private static Quaternionf basisQuaternion(Basis basis) {
		Matrix3f matrix = new Matrix3f();
		matrix.setColumn(0, vector(basis.xAxis()));
		matrix.setColumn(1, vector(basis.yAxis()));
		matrix.setColumn(2, vector(basis.zAxis()));
		return new Quaternionf().setFromNormalized(matrix).normalize();
	}

	private static boolean isWorldViewEligible(Minecraft client) {
		if (client.level == null || client.player == null
				|| client.isMultiplayerServer()
				|| !client.options.getCameraType().isFirstPerson()
				|| ImmersivePresentationController.classify(client)
						!= PresentationState.WORLD) {
			return false;
		}
		LocalPlayer player = client.player;
		Pose pose = player.getPose();
		return !player.isSpectator()
				&& !player.isInvisible()
				&& !player.isSleeping()
				&& !player.isSwimming()
				&& !player.isFallFlying()
				&& !player.isAutoSpinAttack()
				&& !player.isScoping()
				&& (pose == Pose.STANDING || pose == Pose.CROUCHING);
	}

	/** Used by the GameRenderer mixin for this exact extracted render state. */
	public static boolean replacesVanillaHands(LevelRenderState levelRenderState) {
		AvatarFrame frame = levelRenderState.getData(FRAME_KEY);
		return frame != null && frame.replaceVanillaHands();
	}

	private static double distance(Vec3 first, Vec3 second) {
		return first.subtract(second).length();
	}

	private static Vec3 vec(Vector3f vector) {
		return new Vec3(vector.x, vector.y, vector.z);
	}

	private static Vector3f vector(Vec3 vector) {
		return new Vector3f((float) vector.x(), (float) vector.y(), (float) vector.z());
	}

	private static void logFailure(String message, RuntimeException exception) {
		long now = System.nanoTime();
		long previous = LAST_FAILURE_LOG.get();
		if ((previous == 0L || now - previous >= FAILURE_LOG_INTERVAL_NANOS)
				&& LAST_FAILURE_LOG.compareAndSet(previous, now)) {
			LOGGER.error(message, exception);
		}
	}

	private record AvatarFrame(
			boolean replaceVanillaHands,
			List<ArmFrame> arms,
			Identifier skinTexture,
			PlayerModelType modelType,
			Quaternionf cameraRotation,
			float worldViewScale,
			int packedLight) {
		private static final AvatarFrame EMPTY = new AvatarFrame(
				false,
				List.of(),
				null,
				null,
				new Quaternionf(),
				1.0F,
				0);
	}

	private record ArmFrame(
			HumanoidArm arm,
			Solution solution,
			Basis lowerBasis,
			VrTrackedPose grip,
			Vec3 itemGripPosition,
			boolean sleeveVisible,
			ItemStackRenderState itemState) {
	}

	private record ShoulderFrame(
			Vec3 leftShoulder,
			Vec3 rightShoulder,
			Vec3 leftElbowPole,
			Vec3 rightElbowPole) {
	}
}
