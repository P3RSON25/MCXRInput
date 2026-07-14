package dev.mcxrinput.mixin.client;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.mcxrinput.client.VrHudSafeArea;
import net.fabricmc.fabric.api.client.rendering.v1.hud.HudElement;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.Hud;
import net.minecraft.client.gui.contextualbar.ContextualBar;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/** Keeps Minecraft 26.2 locator markers aligned with Fabric's INFO_BAR root. */
@Mixin(Hud.class)
abstract class HudContextualBarMixin {
	// This must remain a composable operation wrapper: a hard redirect could
	// bypass or conflict with another mod extending the locator/contextual bar.
	@WrapOperation(
			method = "extractHotbarAndDecorations(Lnet/minecraft/client/gui/GuiGraphicsExtractor;Lnet/minecraft/client/DeltaTracker;)V",
			at = @At(
					value = "INVOKE",
					target = "Lnet/minecraft/client/gui/contextualbar/ContextualBar;extractRenderState(Lnet/minecraft/client/gui/GuiGraphicsExtractor;Lnet/minecraft/client/DeltaTracker;)V"),
			require = 1)
	private void mcxrinput$fitContextualDetails(
			ContextualBar contextualBar,
			GuiGraphicsExtractor graphics,
			DeltaTracker deltaTracker,
			Operation<Void> original) {
		HudElement originalDetails = (transformedGraphics, transformedDeltaTracker) ->
				original.call(contextualBar, transformedGraphics, transformedDeltaTracker);
		VrHudSafeArea.extractFittedContextualDetails(
				graphics, deltaTracker, originalDetails);
	}
}
