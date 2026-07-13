package dev.mcxrinput.client;

import dev.mcxrinput.hud.HudSafeAreaOffsets;
import dev.mcxrinput.hud.HudSafeAreaOffsets.HorizontalAnchor;
import dev.mcxrinput.hud.HudSafeAreaOffsets.Offset;
import dev.mcxrinput.hud.HudSafeAreaOffsets.VerticalAnchor;
import net.fabricmc.fabric.api.client.rendering.v1.hud.HudElement;
import net.fabricmc.fabric.api.client.rendering.v1.hud.HudElementRegistry;
import net.fabricmc.fabric.api.client.rendering.v1.hud.VanillaHudElements;
import net.minecraft.client.Minecraft;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.resources.Identifier;
import org.joml.Matrix3x2fStack;

/**
 * Moves selected noninteractive vanilla HUD layers into a conservative visible
 * region. Screens and full-frame overlays deliberately remain untouched.
 */
final class VrHudSafeArea {
	private VrHudSafeArea() {
	}

	static void register(MCXRInputConfig config) {
		// Bottom-center gameplay/status group.
		wrap(config, VanillaHudElements.SPECTATOR_MENU, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM);
		wrap(config, VanillaHudElements.HOTBAR, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM);
		wrap(config, VanillaHudElements.ARMOR_BAR, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM);
		wrap(config, VanillaHudElements.HEALTH_BAR, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM);
		wrap(config, VanillaHudElements.FOOD_BAR, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM);
		wrap(config, VanillaHudElements.AIR_BAR, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM);
		wrap(config, VanillaHudElements.MOUNT_HEALTH, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM);
		wrap(config, VanillaHudElements.INFO_BAR, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM);
		wrap(config, VanillaHudElements.EXPERIENCE_LEVEL, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM);
		wrap(config, VanillaHudElements.HELD_ITEM_TOOLTIP, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM);
		wrap(config, VanillaHudElements.SPECTATOR_TOOLTIP, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM);
		wrap(config, VanillaHudElements.OVERLAY_MESSAGE, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM);

		// Top-center layers.
		wrap(config, VanillaHudElements.BOSS_BAR, HorizontalAnchor.CENTER, VerticalAnchor.TOP);
		wrap(config, VanillaHudElements.PLAYER_LIST, HorizontalAnchor.CENTER, VerticalAnchor.TOP);

		// Edge and corner layers. Applying both axes keeps corner-anchored content
		// together without scaling its text, icons, or item models.
		wrap(config, VanillaHudElements.CHAT, HorizontalAnchor.LEFT, VerticalAnchor.BOTTOM);
		wrap(config, VanillaHudElements.MOB_EFFECTS, HorizontalAnchor.RIGHT, VerticalAnchor.TOP);
		wrap(config, VanillaHudElements.DEMO_TIMER, HorizontalAnchor.RIGHT, VerticalAnchor.TOP);
		wrap(config, VanillaHudElements.SCOREBOARD, HorizontalAnchor.RIGHT, VerticalAnchor.CENTER);
		wrap(config, VanillaHudElements.SUBTITLES, HorizontalAnchor.RIGHT, VerticalAnchor.BOTTOM);
	}

	private static void wrap(
			MCXRInputConfig config,
			Identifier elementId,
			HorizontalAnchor horizontalAnchor,
			VerticalAnchor verticalAnchor) {
		HudElementRegistry.replaceElement(elementId, original -> (graphics, deltaTracker) -> {
			// ChatScreen hit-testing remains in vanilla coordinates, so keep its
			// visible chat history unshifted whenever any screen can interact with it.
			boolean interactiveChat = elementId.equals(VanillaHudElements.CHAT)
					&& Minecraft.getInstance().gui.screen() != null;
			if (!config.hudSafeAreaEnabled() || interactiveChat) {
				original.extractRenderState(graphics, deltaTracker);
				return;
			}

			Offset offset = HudSafeAreaOffsets.calculate(
					graphics.guiWidth(),
					graphics.guiHeight(),
					config.hudSafeAreaHorizontalInset(),
					config.hudSafeAreaVerticalInset(),
					horizontalAnchor,
					verticalAnchor);
			extractTranslated(graphics, deltaTracker, original, offset);
		});
	}

	private static void extractTranslated(
			GuiGraphicsExtractor graphics,
			DeltaTracker deltaTracker,
			HudElement original,
			Offset offset) {
		Matrix3x2fStack pose = graphics.pose();
		pose.pushMatrix();
		try {
			pose.translate((float) offset.x(), (float) offset.y());
			original.extractRenderState(graphics, deltaTracker);
		} finally {
			// A failing third-party wrapper must not leak our transform into later HUD layers.
			pose.popMatrix();
		}
	}
}
