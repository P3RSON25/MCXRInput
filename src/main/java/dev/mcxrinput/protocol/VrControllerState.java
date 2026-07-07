package dev.mcxrinput.protocol;

/**
 * Validated controller input from the local bridge.
 *
 * <p>These values are physical user input only. The client may translate them
 * into ordinary Minecraft key mappings, but must not generate autonomous or
 * repeated gameplay actions from them.</p>
 */
public record VrControllerState(
		boolean active,
		float stickX,
		float stickY,
		float trigger,
		float squeeze,
		boolean stickClick,
		boolean a,
		boolean b,
		boolean x,
		boolean y,
		boolean menu
) {
	public static final VrControllerState INACTIVE = new VrControllerState(
			false, 0.0F, 0.0F, 0.0F, 0.0F,
			false, false, false, false, false, false
	);
}
