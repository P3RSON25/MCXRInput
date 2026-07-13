package dev.mcxrinput.mixin.client;

import dev.mcxrinput.client.MCXRInputClient;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.GameRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Applies the newest physical HMD delta immediately before Minecraft samples its camera. */
@Mixin(GameRenderer.class)
public abstract class GameRendererMixin {
	@Inject(
			method = "update(Lnet/minecraft/client/DeltaTracker;)V",
			at = @At(
					value = "INVOKE",
					target = "Lnet/minecraft/client/Camera;update(Lnet/minecraft/client/DeltaTracker;)V",
					shift = At.Shift.BEFORE
			),
			require = 1
	)
	private void mcxrinput$applyHmdCameraBeforeCameraUpdate(
			DeltaTracker deltaTracker, CallbackInfo callbackInfo) {
		MCXRInputClient.applyHmdCameraForRenderFrame(Minecraft.getInstance());
	}
}
