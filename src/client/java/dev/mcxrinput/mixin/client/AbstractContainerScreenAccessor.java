package dev.mcxrinput.mixin.client;

import net.minecraft.client.gui.screens.inventory.AbstractContainerScreen;
import net.minecraft.world.inventory.ContainerInput;
import net.minecraft.world.inventory.Slot;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
import org.spongepowered.asm.mixin.gen.Invoker;

/** Minimal access to Minecraft's existing container-screen interaction path. */
@Mixin(AbstractContainerScreen.class)
public interface AbstractContainerScreenAccessor {
	@Accessor("leftPos")
	int mcxrinput$leftPos();

	@Accessor("topPos")
	int mcxrinput$topPos();

	@Accessor("hoveredSlot")
	Slot mcxrinput$hoveredSlot();

	@Invoker("slotClicked")
	void mcxrinput$slotClicked(Slot slot, int slotId, int mouseButton, ContainerInput input);
}
