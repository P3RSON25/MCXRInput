package dev.mcxrinput.client;

import dev.mcxrinput.hud.HudSafeAreaOffsets;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.components.StringWidget;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

import java.util.Locale;

/** Mod Menu sub-screen for the optional immersive-display HUD safe area. */
final class MCXRInputHudConfigScreen extends Screen {
	private static final double INSET_STEP = 0.01;

	private final Screen parent;
	private final MCXRInputConfig.Values values;
	private Button enabledButton;
	private StringWidget horizontalInsetValue;
	private StringWidget verticalInsetValue;

	MCXRInputHudConfigScreen(Screen parent, MCXRInputConfig.Values values) {
		super(Component.translatable("screen.mcxrinput.hud_safe_area"));
		this.parent = parent;
		this.values = values;
	}

	@Override
	protected void init() {
		int centerX = width / 2;
		int left = centerX - 150;
		int y = 48;

		addRenderableOnly(new StringWidget(
				centerX - 100, 16, 200, 20,
				Component.translatable("screen.mcxrinput.hud_safe_area"), font));

		addRenderableOnly(new StringWidget(
				left, y, 230, 20,
				Component.translatable("option.mcxrinput.hud_safe_area_enabled"), font));
		enabledButton = addRenderableWidget(Button.builder(Component.empty(), button -> {
			values.hudSafeAreaEnabled = !values.hudSafeAreaEnabled;
			refreshValues();
		}).bounds(left + 236, y, 64, 20).build());
		y += 32;

		horizontalInsetValue = addInsetRow(
				left, y, "option.mcxrinput.hud_safe_area_horizontal_inset",
				() -> values.hudSafeAreaHorizontalInset,
				value -> values.hudSafeAreaHorizontalInset = value);
		y += 32;
		verticalInsetValue = addInsetRow(
				left, y, "option.mcxrinput.hud_safe_area_vertical_inset",
				() -> values.hudSafeAreaVerticalInset,
				value -> values.hudSafeAreaVerticalInset = value);

		addRenderableWidget(Button.builder(Component.translatable("controls.reset"), button -> {
			values.hudSafeAreaEnabled = MCXRInputConfig.DEFAULT_HUD_SAFE_AREA_ENABLED;
			values.hudSafeAreaHorizontalInset =
					MCXRInputConfig.DEFAULT_HUD_SAFE_AREA_HORIZONTAL_INSET;
			values.hudSafeAreaVerticalInset =
					MCXRInputConfig.DEFAULT_HUD_SAFE_AREA_VERTICAL_INSET;
			refreshValues();
		}).bounds(centerX - 155, height - 32, 150, 20).build());
		addRenderableWidget(Button.builder(Component.translatable("gui.done"), button -> closeToParent())
				.bounds(centerX + 5, height - 32, 150, 20).build());

		refreshValues();
	}

	@Override
	public void onClose() {
		closeToParent();
	}

	private StringWidget addInsetRow(
			int left, int y, String labelKey, DoubleGetter getter, DoubleSetter setter) {
		addRenderableOnly(new StringWidget(
				left, y, 160, 20, Component.translatable(labelKey), font));
		addRenderableWidget(Button.builder(Component.literal("-"), button -> {
			setter.set(clampInset(getter.get() - INSET_STEP));
			refreshValues();
		}).bounds(left + 170, y, 24, 20).build());
		StringWidget valueWidget = new StringWidget(
				left + 198, y, 72, 20, Component.empty(), font);
		addRenderableOnly(valueWidget);
		addRenderableWidget(Button.builder(Component.literal("+"), button -> {
			setter.set(clampInset(getter.get() + INSET_STEP));
			refreshValues();
		}).bounds(left + 276, y, 24, 20).build());
		return valueWidget;
	}

	private void refreshValues() {
		enabledButton.setMessage(Component.translatable(
				values.hudSafeAreaEnabled ? "options.on" : "options.off"));
		horizontalInsetValue.setMessage(Component.literal(formatInset(
				values.hudSafeAreaHorizontalInset)));
		verticalInsetValue.setMessage(Component.literal(formatInset(
				values.hudSafeAreaVerticalInset)));
	}

	private void closeToParent() {
		if (minecraft != null) {
			minecraft.gui.setScreen(parent);
		}
	}

	private static double clampInset(double value) {
		return HudSafeAreaOffsets.sanitizeInset(value, HudSafeAreaOffsets.MIN_INSET);
	}

	private static String formatInset(double value) {
		return String.format(Locale.ROOT, "%.0f%%", value * 100.0);
	}

	@FunctionalInterface
	private interface DoubleGetter {
		double get();
	}

	@FunctionalInterface
	private interface DoubleSetter {
		void set(double value);
	}
}
