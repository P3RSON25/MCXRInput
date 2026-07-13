package dev.mcxrinput.mixin.client;

import dev.mcxrinput.client.MCXRInputClient;
import net.minecraft.client.Camera;
import net.minecraft.client.Minecraft;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Keeps Minecraft's captured world FOV equal to the fresh bridge calibration. */
@Mixin(Camera.class)
public abstract class CameraFovMixin {
	@Inject(method = "calculateFov(F)F", at = @At("RETURN"), cancellable = true, require = 1)
	private void mcxrinput$applyImmersiveCaptureFov(
			float partialTick, CallbackInfoReturnable<Float> callbackInfo) {
		callbackInfo.setReturnValue(MCXRInputClient.applyImmersivePresentationFov(
				Minecraft.getInstance(), callbackInfo.getReturnValueF()));
	}
}
