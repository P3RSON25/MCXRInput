package dev.mcxrinput.client;

import net.minecraft.client.model.geom.ModelPart;
import net.minecraft.core.Direction;
import net.minecraft.world.entity.HumanoidArm;
import net.minecraft.world.entity.player.PlayerModelType;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

/**
 * Prebaked player-skin cuboids for the tracked-arm renderer.
 *
 * <p>Each part is centered on its bone axis, begins at local Y=0, and extends
 * six skin pixels toward local +Y. Callers position and orient the immutable-use
 * parts with a {@code PoseStack}; they must not mutate the shared
 * {@link ModelPart} pose fields.</p>
 *
 * <p>The normal player arm is twelve pixels long. Splitting the side UVs at six
 * pixels preserves the upper and lower halves of the skin rather than squeezing
 * the complete arm texture onto both bones. The elbow caps intentionally reuse
 * the skin's normal top and bottom pixels because the 64x64 skin format has no
 * dedicated elbow faces.</p>
 */
final class TrackedArmModelParts {
	static final float CANONICAL_SEGMENT_LENGTH_BLOCKS = 6.0F / 16.0F;

	private static final int TEXTURE_WIDTH = 64;
	private static final int TEXTURE_HEIGHT = 64;
	private static final float SEGMENT_LENGTH_PIXELS = 6.0F;
	private static final float ARM_DEPTH_PIXELS = 4.0F;
	private static final float SLEEVE_INFLATION_PIXELS = 0.25F;
	private static final Set<Direction> ALL_FACES = Set.of(Direction.values());
	private static final Set<Direction> SIDE_FACES = Set.of(
			Direction.NORTH,
			Direction.SOUTH,
			Direction.WEST,
			Direction.EAST);
	private static final Set<Direction> CAP_FACES = Set.of(
			Direction.UP,
			Direction.DOWN);
	private static final Map<MeshKey, ModelPart> MESHES = createMeshes();

	private TrackedArmModelParts() {
	}

	/**
	 * Returns a shared, prebaked part. Treat the result as immutable: apply all
	 * translations, rotations, and length scaling to the submission PoseStack.
	 */
	static ModelPart segment(
			PlayerModelType modelType,
			HumanoidArm arm,
			Segment segment,
			boolean sleeveVisible) {
		Objects.requireNonNull(modelType, "modelType");
		Objects.requireNonNull(arm, "arm");
		Objects.requireNonNull(segment, "segment");
		ModelPart part = MESHES.get(new MeshKey(
				modelType, arm, segment, sleeveVisible));
		if (part == null) {
			throw new IllegalArgumentException(
					"Unsupported tracked-arm mesh combination");
		}
		return part;
	}

	private static Map<MeshKey, ModelPart> createMeshes() {
		Map<MeshKey, ModelPart> meshes = new HashMap<>();
		for (PlayerModelType modelType : PlayerModelType.values()) {
			for (HumanoidArm arm : HumanoidArm.values()) {
				for (Segment segment : Segment.values()) {
					meshes.put(
							new MeshKey(modelType, arm, segment, false),
							buildPart(modelType, arm, segment, false));
					meshes.put(
							new MeshKey(modelType, arm, segment, true),
							buildPart(modelType, arm, segment, true));
				}
			}
		}
		return Map.copyOf(meshes);
	}

	private static ModelPart buildPart(
			PlayerModelType modelType,
			HumanoidArm arm,
			Segment segment,
			boolean sleeveVisible) {
		float width = modelType == PlayerModelType.SLIM ? 3.0F : 4.0F;
		float x = -width / 2.0F;
		float z = -ARM_DEPTH_PIXELS / 2.0F;
		TextureOrigin base = baseTexture(arm);
		List<ModelPart.Cube> cubes = new ArrayList<>(sleeveVisible ? 4 : 2);
		appendLayer(cubes, base, segment, x, z, width, 0.0F);
		if (sleeveVisible) {
			appendLayer(
					cubes,
					sleeveTexture(arm),
					segment,
					x,
					z,
					width,
					SLEEVE_INFLATION_PIXELS);
		}
		return new ModelPart(List.copyOf(cubes), Map.of());
	}

	private static void appendLayer(
			List<ModelPart.Cube> cubes,
			TextureOrigin texture,
			Segment segment,
			float x,
			float z,
			float width,
			float inflation) {
		if (segment == Segment.UPPER) {
			cubes.add(cube(
					texture.u(), texture.v(), x, z, width,
					inflation, ALL_FACES));
			return;
		}

		// Cube side UVs start four pixels below their texture origin. Advancing
		// V by six therefore selects rows 10..16, the lower half of a 12px arm.
		cubes.add(cube(
				texture.u(), texture.v() + 6, x, z, width,
				inflation, SIDE_FACES));
		// A second, cap-only cube preserves the normal arm top/bottom UV islands.
		cubes.add(cube(
				texture.u(), texture.v(), x, z, width,
				inflation, CAP_FACES));
	}

	private static ModelPart.Cube cube(
			int textureU,
			int textureV,
			float x,
			float z,
			float width,
			float inflation,
			Set<Direction> visibleFaces) {
		return new ModelPart.Cube(
				textureU,
				textureV,
				x,
				0.0F,
				z,
				width,
				SEGMENT_LENGTH_PIXELS,
				ARM_DEPTH_PIXELS,
				inflation,
				inflation,
				inflation,
				false,
				TEXTURE_WIDTH,
				TEXTURE_HEIGHT,
				visibleFaces);
	}

	private static TextureOrigin baseTexture(HumanoidArm arm) {
		return arm == HumanoidArm.LEFT
				? new TextureOrigin(32, 48)
				: new TextureOrigin(40, 16);
	}

	private static TextureOrigin sleeveTexture(HumanoidArm arm) {
		return arm == HumanoidArm.LEFT
				? new TextureOrigin(48, 48)
				: new TextureOrigin(40, 32);
	}

	enum Segment {
		UPPER,
		LOWER
	}

	private record TextureOrigin(int u, int v) {
	}

	private record MeshKey(
			PlayerModelType modelType,
			HumanoidArm arm,
			Segment segment,
			boolean sleeveVisible) {
	}
}
