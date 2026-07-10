package dev.mcxrinput.mixin.client;

import net.minecraft.client.gui.screens.inventory.CreativeModeInventoryScreen;
import net.minecraft.world.item.CreativeModeTab;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

/** Exposes vanilla Creative tab geometry for snapped controller navigation. */
@Mixin(CreativeModeInventoryScreen.class)
public interface CreativeModeInventoryScreenAccessor {
	@Invoker("getTabX")
	int mcxrinput$getTabX(CreativeModeTab tab);

	@Invoker("getTabY")
	int mcxrinput$getTabY(CreativeModeTab tab);
}
