package dev.mcxrinput.client;

import dev.mcxrinput.hud.BottomCenterHudTransform;
import dev.mcxrinput.hud.BottomCenterHudTransform.Transform;
import dev.mcxrinput.hud.HudSafeAreaOffsets;
import dev.mcxrinput.hud.HudSafeAreaOffsets.HorizontalAnchor;
import dev.mcxrinput.hud.HudSafeAreaOffsets.Offset;
import dev.mcxrinput.hud.HudSafeAreaOffsets.VerticalAnchor;
import dev.mcxrinput.hud.HudSafeAreaSettings;
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
public final class VrHudSafeArea {
	private static volatile Registration registration;

	private VrHudSafeArea() {
	}

	static void register(MCXRInputConfig config, VrUdpReceiver receiver) {
		registration = new Registration(config, receiver);

		// Bottom-center gameplay/status group.
		wrap(config, receiver, VanillaHudElements.SPECTATOR_MENU, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM);
		wrapFittedBottomCenter(config, receiver, VanillaHudElements.HOTBAR);
		wrapFittedBottomCenter(config, receiver, VanillaHudElements.ARMOR_BAR);
		wrapFittedBottomCenter(config, receiver, VanillaHudElements.HEALTH_BAR);
		wrapFittedBottomCenter(config, receiver, VanillaHudElements.FOOD_BAR);
		wrapFittedBottomCenter(config, receiver, VanillaHudElements.AIR_BAR);
		wrapFittedBottomCenter(config, receiver, VanillaHudElements.MOUNT_HEALTH);
		wrapFittedBottomCenter(config, receiver, VanillaHudElements.INFO_BAR);
		wrapFittedBottomCenter(config, receiver, VanillaHudElements.EXPERIENCE_LEVEL);
		wrap(config, receiver, VanillaHudElements.HELD_ITEM_TOOLTIP, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM);
		wrap(config, receiver, VanillaHudElements.SPECTATOR_TOOLTIP, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM);
		wrap(config, receiver, VanillaHudElements.OVERLAY_MESSAGE, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM);

		// Top-center layers.
		wrap(config, receiver, VanillaHudElements.BOSS_BAR, HorizontalAnchor.CENTER, VerticalAnchor.TOP);
		wrap(config, receiver, VanillaHudElements.PLAYER_LIST, HorizontalAnchor.CENTER, VerticalAnchor.TOP);

		// Edge and corner layers. Applying both axes keeps corner-anchored content
		// together without scaling its text, icons, or item models.
		wrap(config, receiver, VanillaHudElements.CHAT, HorizontalAnchor.LEFT, VerticalAnchor.BOTTOM);
		wrap(config, receiver, VanillaHudElements.MOB_EFFECTS, HorizontalAnchor.RIGHT, VerticalAnchor.TOP);
		wrap(config, receiver, VanillaHudElements.DEMO_TIMER, HorizontalAnchor.RIGHT, VerticalAnchor.TOP);
		wrap(config, receiver, VanillaHudElements.SCOREBOARD, HorizontalAnchor.RIGHT, VerticalAnchor.CENTER);
		wrap(config, receiver, VanillaHudElements.SUBTITLES, HorizontalAnchor.RIGHT, VerticalAnchor.BOTTOM);
	}

	/**
	 * Minecraft 26.2 extracts locator markers after Fabric's INFO_BAR element has
	 * extracted the contextual background. The exact operation wrapper calling
	 * this method gives those details the same visual-only transform; otherwise
	 * the locator background and its markers would disagree. XP and jump
	 * implementations have no later details, so this remains a no-op for their
	 * content.
	 */
	public static void extractFittedContextualDetails(
			GuiGraphicsExtractor graphics,
			DeltaTracker deltaTracker,
			HudElement originalDetails) {
		Registration current = registration;
		if (current == null) {
			originalDetails.extractRenderState(graphics, deltaTracker);
			return;
		}

		HudSafeAreaSettings.Settings settings = resolveSettings(current.config(), current.receiver());
		if (!settings.enabled()) {
			originalDetails.extractRenderState(graphics, deltaTracker);
			return;
		}

		Transform transform = BottomCenterHudTransform.calculate(
				graphics.guiWidth(),
				graphics.guiHeight(),
				settings.horizontalInset(),
				settings.verticalInset());
		extractBottomCenterFitted(
				graphics, deltaTracker, originalDetails, transform);
	}

	private static void wrap(
			MCXRInputConfig config,
			VrUdpReceiver receiver,
			Identifier elementId,
			HorizontalAnchor horizontalAnchor,
			VerticalAnchor verticalAnchor) {
		wrap(config, receiver, elementId, horizontalAnchor, verticalAnchor, false);
	}

	private static void wrapFittedBottomCenter(
			MCXRInputConfig config,
			VrUdpReceiver receiver,
			Identifier elementId) {
		wrap(config, receiver, elementId, HorizontalAnchor.CENTER, VerticalAnchor.BOTTOM, true);
	}

	private static void wrap(
			MCXRInputConfig config,
			VrUdpReceiver receiver,
			Identifier elementId,
			HorizontalAnchor horizontalAnchor,
			VerticalAnchor verticalAnchor,
			boolean fitBottomCenter) {
		HudElementRegistry.replaceElement(elementId, original -> (graphics, deltaTracker) -> {
			// ChatScreen hit-testing remains in vanilla coordinates, so keep its
			// visible chat history unshifted whenever any screen can interact with it.
			boolean interactiveChat = elementId.equals(VanillaHudElements.CHAT)
					&& Minecraft.getInstance().gui.screen() != null;
			HudSafeAreaSettings.Settings settings = resolveSettings(config, receiver);
			if (!settings.enabled() || interactiveChat) {
				original.extractRenderState(graphics, deltaTracker);
				return;
			}
			if (fitBottomCenter) {
				// Centered layers cannot move both edges inward. Give each isolated
				// gameplay/status layer the same affine transform so health, armor,
				// hunger, air, mount health, XP, and the offhand-inclusive hotbar retain
				// their vanilla proportions. This is visual-only and never affects input.
				Transform transform = BottomCenterHudTransform.calculate(
						graphics.guiWidth(),
						graphics.guiHeight(),
						settings.horizontalInset(),
						settings.verticalInset());
				extractBottomCenterFitted(graphics, deltaTracker, original, transform);
				return;
			}

			Offset offset = HudSafeAreaOffsets.calculate(
					graphics.guiWidth(),
					graphics.guiHeight(),
					settings.horizontalInset(),
					settings.verticalInset(),
					horizontalAnchor,
					verticalAnchor);
			extractTranslated(graphics, deltaTracker, original, offset);
		});
	}

	private static HudSafeAreaSettings.Settings resolveSettings(
			MCXRInputConfig config,
			VrUdpReceiver receiver) {
		return HudSafeAreaSettings.resolve(
				config.hudSafeAreaEnabled(),
				config.hudSafeAreaHorizontalInset(),
				config.hudSafeAreaVerticalInset(),
				config.automaticImmersiveHudSafeArea(),
				receiver.latestFreshPresentationOffer());
	}

	private static void extractBottomCenterFitted(
			GuiGraphicsExtractor graphics,
			DeltaTracker deltaTracker,
			HudElement original,
			Transform transform) {
		Matrix3x2fStack pose = graphics.pose();
		pose.pushMatrix();
		try {
			pose.translate((float) transform.translationX(), (float) transform.translationY());
			pose.scale((float) transform.scale(), (float) transform.scale());
			original.extractRenderState(graphics, deltaTracker);
		} finally {
			pose.popMatrix();
		}
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

	private record Registration(MCXRInputConfig config, VrUdpReceiver receiver) {
	}
}
