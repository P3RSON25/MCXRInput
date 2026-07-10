package dev.mcxrinput.mixin.client;

import net.minecraft.client.MouseHandler;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

/** Lets the inventory D-pad snap Minecraft's ordinary mouse position to a slot. */
@Mixin(MouseHandler.class)
public interface MouseHandlerAccessor {
	@Invoker("onMove")
	void mcxrinput$onMove(long window, double x, double y);
}
