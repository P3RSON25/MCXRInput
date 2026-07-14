package dev.mcxrinput.mixin.client;

import dev.mcxrinput.client.MCXRInputClient;
import dev.mcxrinput.client.TrackedAvatarRenderer;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.GameRenderer;
import net.minecraft.client.renderer.state.GameRenderState;
import net.minecraft.client.renderer.state.level.CameraRenderState;
import org.joml.Matrix4fc;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Applies the newest physical HMD delta immediately before Minecraft samples its camera. */
@Mixin(GameRenderer.class)
public abstract class GameRendererMixin {
	@Shadow
	@Final
	private GameRenderState gameRenderState;

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

	@Inject(
		method = "renderItemInHand(Lnet/minecraft/client/renderer/state/level/CameraRenderState;FLorg/joml/Matrix4fc;)V",
		at = @At("HEAD"),
		cancellable = true,
		require = 1
	)
	private void mcxrinput$suppressVanillaHandsForTrackedAvatar(
			CameraRenderState cameraRenderState,
			float partialTick,
			Matrix4fc projectionMatrix,
			CallbackInfo callbackInfo) {
		if (TrackedAvatarRenderer.replacesVanillaHands(
				gameRenderState.levelRenderState)) {
			callbackInfo.cancel();
		}
	}
}
