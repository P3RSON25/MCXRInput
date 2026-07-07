package dev.mcxrinput.client;

import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.components.StringWidget;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

import java.util.Locale;

final class MCXRInputConfigScreen extends Screen {
	private static final double HMD_SENSITIVITY_STEP = 0.1;
	private static final double THRESHOLD_STEP = 0.05;

	private final Screen parent;
	private final MCXRInputConfig config;
	private MCXRInputConfig.Values workingValues;
	private StringWidget yawValue;
	private StringWidget pitchValue;
	private StringWidget deadzoneValue;
	private StringWidget triggerValue;

	MCXRInputConfigScreen(Screen parent, MCXRInputConfig config) {
		super(Component.translatable("screen.mcxrinput.config"));
		this.parent = parent;
		this.config = config;
		this.workingValues = config.snapshot();
	}

	@Override
	protected void init() {
		int centerX = width / 2;
		int left = centerX - 150;
		int y = 44;

		addRenderableOnly(new StringWidget(
				centerX - 100, 16, 200, 20,
				Component.translatable("screen.mcxrinput.config"),
				font
		));

		yawValue = addNumberRow(left, y, "option.mcxrinput.hmd_yaw_sensitivity",
				() -> workingValues.hmdYawSensitivity,
				value -> workingValues.hmdYawSensitivity = value,
				HMD_SENSITIVITY_STEP, 0.1, 3.0);
		y += 28;
		pitchValue = addNumberRow(left, y, "option.mcxrinput.hmd_pitch_sensitivity",
				() -> workingValues.hmdPitchSensitivity,
				value -> workingValues.hmdPitchSensitivity = value,
				HMD_SENSITIVITY_STEP, 0.1, 3.0);
		y += 28;
		deadzoneValue = addNumberRow(left, y, "option.mcxrinput.controller_deadzone",
				() -> workingValues.controllerDeadzone,
				value -> workingValues.controllerDeadzone = value,
				THRESHOLD_STEP, 0.05, 0.95);
		y += 28;
		triggerValue = addNumberRow(left, y, "option.mcxrinput.trigger_threshold",
				() -> workingValues.triggerThreshold,
				value -> workingValues.triggerThreshold = value,
				THRESHOLD_STEP, 0.05, 1.0);

		addRenderableWidget(Button.builder(Component.translatable("controls.reset"), button -> {
			workingValues = MCXRInputConfig.Values.defaults();
			refreshValues();
		}).bounds(centerX - 155, height - 32, 150, 20).build());
		addRenderableWidget(Button.builder(Component.translatable("gui.done"), button -> saveAndClose())
				.bounds(centerX + 5, height - 32, 150, 20).build());
		refreshValues();
	}

	@Override
	public void onClose() {
		saveAndClose();
	}

	private StringWidget addNumberRow(int left, int y, String labelKey, DoubleGetter getter, DoubleSetter setter,
									  double step, double minimum, double maximum) {
		addRenderableOnly(new StringWidget(left, y, 160, 20, Component.translatable(labelKey), font));
		addRenderableWidget(Button.builder(Component.literal("-"), button -> {
			setter.set(clamp(getter.get() - step, minimum, maximum));
			refreshValues();
		}).bounds(left + 170, y, 24, 20).build());
		StringWidget valueWidget = new StringWidget(left + 198, y, 72, 20, Component.empty(), font);
		addRenderableOnly(valueWidget);
		addRenderableWidget(Button.builder(Component.literal("+"), button -> {
			setter.set(clamp(getter.get() + step, minimum, maximum));
			refreshValues();
		}).bounds(left + 276, y, 24, 20).build());
		return valueWidget;
	}

	private void refreshValues() {
		yawValue.setMessage(Component.literal(format(workingValues.hmdYawSensitivity)));
		pitchValue.setMessage(Component.literal(format(workingValues.hmdPitchSensitivity)));
		deadzoneValue.setMessage(Component.literal(format(workingValues.controllerDeadzone)));
		triggerValue.setMessage(Component.literal(format(workingValues.triggerThreshold)));
	}

	private void saveAndClose() {
		config.replaceAndSave(workingValues);
		if (minecraft != null) {
			minecraft.gui.setScreen(parent);
		}
	}

	private static double clamp(double value, double minimum, double maximum) {
		return Math.max(minimum, Math.min(maximum, value));
	}

	private static String format(double value) {
		return String.format(Locale.ROOT, "%.2f", value);
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
