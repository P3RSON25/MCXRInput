package dev.mcxrinput.presentation;

/** Main-thread Minecraft presentation state reported to the native bridge. */
public enum PresentationState {
	WORLD,
	SCREEN,
	OVERLAY,
	NO_WORLD;

	public static PresentationState classify(
			boolean worldAvailable,
			boolean playerAvailable,
			boolean screenOpen,
			boolean overlayOpen) {
		if (!worldAvailable || !playerAvailable) {
			return NO_WORLD;
		}
		if (overlayOpen) {
			return OVERLAY;
		}
		if (screenOpen) {
			return SCREEN;
		}
		return WORLD;
	}
}
