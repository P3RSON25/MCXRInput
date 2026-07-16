package dev.mcxrinput.client;

import net.minecraft.client.model.geom.ModelPart;
import net.minecraft.core.Direction;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

/**
 * Prebaked torso and leg cuboids for the neutral tracked-avatar checkpoint.
 *
 * <p>The meshes reproduce the adult 64x64 player skin UVs and dimensions.
 * Wide and slim player models intentionally share these body meshes in vanilla;
 * only their arm widths differ. No head, nameplate, shadow, armor, or other
 * entity-renderer geometry is present here.</p>
 *
 * <p>Each part begins at local Y=0 and extends twelve skin pixels toward local
 * +Y. Callers apply all placement and orientation through a {@code PoseStack}
 * and must treat the shared parts as immutable.</p>
 */
final class TrackedBodyModelParts {
	private static final int TEXTURE_WIDTH = 64;
	private static final int TEXTURE_HEIGHT = 64;
	private static final float PART_LENGTH_PIXELS = 12.0F;
	private static final float PART_DEPTH_PIXELS = 4.0F;
	private static final float TORSO_WIDTH_PIXELS = 8.0F;
	private static final float LEG_WIDTH_PIXELS = 4.0F;
	private static final float OUTER_LAYER_INFLATION_PIXELS = 0.25F;
	private static final Set<Direction> BODY_FACES = Set.of(
			Direction.NORTH,
			Direction.SOUTH,
			Direction.WEST,
			Direction.EAST,
			Direction.DOWN,
			Direction.UP);
	private static final Map<MeshKey, ModelPart> MESHES = createMeshes();

	private TrackedBodyModelParts() {
	}

	/** Returns one shared base mesh with its optional outer skin layer. */
	static ModelPart part(Part part, boolean outerLayerVisible) {
		Objects.requireNonNull(part, "part");
		ModelPart modelPart = MESHES.get(new MeshKey(part, outerLayerVisible));
		if (modelPart == null) {
			throw new IllegalArgumentException(
					"Unsupported tracked-body mesh combination");
		}
		return modelPart;
	}

	private static Map<MeshKey, ModelPart> createMeshes() {
		Map<MeshKey, ModelPart> meshes = new HashMap<>();
		for (Part part : Part.values()) {
			meshes.put(new MeshKey(part, false), buildPart(part, false));
			meshes.put(new MeshKey(part, true), buildPart(part, true));
		}
		return Map.copyOf(meshes);
	}

	private static ModelPart buildPart(Part part, boolean outerLayerVisible) {
		float width = part == Part.TORSO
				? TORSO_WIDTH_PIXELS
				: LEG_WIDTH_PIXELS;
		float x = -width / 2.0F;
		float z = -PART_DEPTH_PIXELS / 2.0F;
		List<ModelPart.Cube> cubes = new ArrayList<>(
				outerLayerVisible ? 2 : 1);
		TextureOrigin base = baseTexture(part);
		cubes.add(cube(base, x, z, width, 0.0F));
		if (outerLayerVisible) {
			TextureOrigin outer = outerTexture(part);
			cubes.add(cube(
					outer,
					x,
					z,
					width,
					OUTER_LAYER_INFLATION_PIXELS));
		}
		return new ModelPart(List.copyOf(cubes), Map.of());
	}

	private static ModelPart.Cube cube(
			TextureOrigin texture,
			float x,
			float z,
			float width,
			float inflation) {
		return new ModelPart.Cube(
				texture.u(),
				texture.v(),
				x,
				0.0F,
				z,
				width,
				PART_LENGTH_PIXELS,
				PART_DEPTH_PIXELS,
				inflation,
				inflation,
				inflation,
				false,
				TEXTURE_WIDTH,
				TEXTURE_HEIGHT,
				BODY_FACES);
	}

	private static TextureOrigin baseTexture(Part part) {
		return switch (part) {
			case TORSO -> new TextureOrigin(16, 16);
			case LEFT_LEG -> new TextureOrigin(16, 48);
			case RIGHT_LEG -> new TextureOrigin(0, 16);
		};
	}

	private static TextureOrigin outerTexture(Part part) {
		return switch (part) {
			case TORSO -> new TextureOrigin(16, 32);
			case LEFT_LEG -> new TextureOrigin(0, 48);
			case RIGHT_LEG -> new TextureOrigin(0, 32);
		};
	}

	enum Part {
		TORSO,
		LEFT_LEG,
		RIGHT_LEG
	}

	private record TextureOrigin(int u, int v) {
	}

	private record MeshKey(Part part, boolean outerLayerVisible) {
	}
}
