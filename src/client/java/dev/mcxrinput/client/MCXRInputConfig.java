package dev.mcxrinput.client;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import dev.mcxrinput.input.ControllerButton;
import dev.mcxrinput.input.ControllerStick;
import net.fabricmc.loader.api.FabricLoader;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.io.Reader;
import java.io.Writer;
import java.nio.file.Files;
import java.nio.file.Path;

final class MCXRInputConfig {
	static final double DEFAULT_HMD_YAW_SENSITIVITY = 1.0;
	static final double DEFAULT_HMD_PITCH_SENSITIVITY = 1.0;
	static final double DEFAULT_CONTROLLER_DEADZONE = 0.35;
	static final double DEFAULT_TRIGGER_THRESHOLD = 0.55;
	static final ControllerStick DEFAULT_MOVEMENT_STICK = ControllerStick.LEFT;
	static final ControllerButton DEFAULT_JUMP_BINDING = ControllerButton.RIGHT_A;
	static final ControllerButton DEFAULT_SNEAK_BINDING = ControllerButton.RIGHT_B;
	static final ControllerButton DEFAULT_SPRINT_BINDING = ControllerButton.LEFT_STICK_CLICK;
	static final ControllerButton DEFAULT_ATTACK_BINDING = ControllerButton.RIGHT_TRIGGER;
	static final ControllerButton DEFAULT_USE_BINDING = ControllerButton.LEFT_TRIGGER;
	static final ControllerStick DEFAULT_MENU_NAVIGATION_STICK = ControllerStick.LEFT;
	static final ControllerButton DEFAULT_MENU_CONFIRM_BINDING = ControllerButton.RIGHT_A;
	static final ControllerButton DEFAULT_MENU_BACK_BINDING = ControllerButton.RIGHT_B;

	private static final Logger LOGGER = LoggerFactory.getLogger("MCXRInput/Config");
	private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();
	private static final int CONFIG_VERSION = 2;
	private static final double MIN_HMD_SENSITIVITY = 0.1;
	private static final double MAX_HMD_SENSITIVITY = 3.0;
	private static final double MIN_CONTROLLER_DEADZONE = 0.05;
	private static final double MAX_CONTROLLER_DEADZONE = 0.95;
	private static final double MIN_TRIGGER_THRESHOLD = 0.05;
	private static final double MAX_TRIGGER_THRESHOLD = 1.0;
	private static final MCXRInputConfig INSTANCE = load();

	private final Path path;
	private Values values;

	private MCXRInputConfig(Path path, Values values) {
		this.path = path;
		this.values = sanitize(values);
	}

	static MCXRInputConfig get() {
		return INSTANCE;
	}

	synchronized Values snapshot() {
		return values.copy();
	}

	synchronized double hmdYawSensitivity() {
		return values.hmdYawSensitivity;
	}

	synchronized double hmdPitchSensitivity() {
		return values.hmdPitchSensitivity;
	}

	synchronized double controllerDeadzone() {
		return values.controllerDeadzone;
	}

	synchronized double triggerThreshold() {
		return values.triggerThreshold;
	}

	synchronized ControllerStick movementStick() {
		return ControllerStick.fromId(values.movementStick, DEFAULT_MOVEMENT_STICK);
	}

	synchronized ControllerButton jumpBinding() {
		return ControllerButton.fromId(values.jumpBinding, DEFAULT_JUMP_BINDING);
	}

	synchronized ControllerButton sneakBinding() {
		return ControllerButton.fromId(values.sneakBinding, DEFAULT_SNEAK_BINDING);
	}

	synchronized ControllerButton sprintBinding() {
		return ControllerButton.fromId(values.sprintBinding, DEFAULT_SPRINT_BINDING);
	}

	synchronized ControllerButton attackBinding() {
		return ControllerButton.fromId(values.attackBinding, DEFAULT_ATTACK_BINDING);
	}

	synchronized ControllerButton useBinding() {
		return ControllerButton.fromId(values.useBinding, DEFAULT_USE_BINDING);
	}

	synchronized ControllerStick menuNavigationStick() {
		return ControllerStick.fromId(values.menuNavigationStick, DEFAULT_MENU_NAVIGATION_STICK);
	}

	synchronized ControllerButton menuConfirmBinding() {
		return ControllerButton.fromId(values.menuConfirmBinding, DEFAULT_MENU_CONFIRM_BINDING);
	}

	synchronized ControllerButton menuBackBinding() {
		return ControllerButton.fromId(values.menuBackBinding, DEFAULT_MENU_BACK_BINDING);
	}

	synchronized void replaceAndSave(Values newValues) {
		values = sanitize(newValues);
		save();
	}

	synchronized void resetAndSave() {
		values = Values.defaults();
		save();
	}

	private synchronized void save() {
		try {
			Files.createDirectories(path.getParent());
			try (Writer writer = Files.newBufferedWriter(path)) {
				GSON.toJson(values, writer);
			}
		} catch (IOException exception) {
			LOGGER.warn("Could not save MCXRInput config to {}", path, exception);
		}
	}

	private static MCXRInputConfig load() {
		Path path = FabricLoader.getInstance().getConfigDir().resolve("mcxrinput.json");
		Values loaded = null;
		if (Files.isRegularFile(path)) {
			try (Reader reader = Files.newBufferedReader(path)) {
				loaded = GSON.fromJson(reader, Values.class);
			} catch (RuntimeException | IOException exception) {
				LOGGER.warn("Could not read MCXRInput config from {}; defaults will be used", path, exception);
			}
		}

		MCXRInputConfig config = new MCXRInputConfig(path, loaded == null ? Values.defaults() : loaded);
		if (loaded == null || !config.values.equals(loaded)) {
			config.save();
		}
		return config;
	}

	private static Values sanitize(Values input) {
		Values values = input == null ? Values.defaults() : input.copy();
		values.configVersion = CONFIG_VERSION;
		values.hmdYawSensitivity = finiteClamped(
				values.hmdYawSensitivity, DEFAULT_HMD_YAW_SENSITIVITY,
				MIN_HMD_SENSITIVITY, MAX_HMD_SENSITIVITY);
		values.hmdPitchSensitivity = finiteClamped(
				values.hmdPitchSensitivity, DEFAULT_HMD_PITCH_SENSITIVITY,
				MIN_HMD_SENSITIVITY, MAX_HMD_SENSITIVITY);
		values.controllerDeadzone = finiteClamped(
				values.controllerDeadzone, DEFAULT_CONTROLLER_DEADZONE,
				MIN_CONTROLLER_DEADZONE, MAX_CONTROLLER_DEADZONE);
		values.triggerThreshold = finiteClamped(
				values.triggerThreshold, DEFAULT_TRIGGER_THRESHOLD,
				MIN_TRIGGER_THRESHOLD, MAX_TRIGGER_THRESHOLD);
		values.movementStick = ControllerStick.fromId(
				values.movementStick, DEFAULT_MOVEMENT_STICK).id();
		values.jumpBinding = ControllerButton.fromId(
				values.jumpBinding, DEFAULT_JUMP_BINDING).id();
		values.sneakBinding = ControllerButton.fromId(
				values.sneakBinding, DEFAULT_SNEAK_BINDING).id();
		values.sprintBinding = ControllerButton.fromId(
				values.sprintBinding, DEFAULT_SPRINT_BINDING).id();
		values.attackBinding = ControllerButton.fromId(
				values.attackBinding, DEFAULT_ATTACK_BINDING).id();
		values.useBinding = ControllerButton.fromId(
				values.useBinding, DEFAULT_USE_BINDING).id();
		values.menuNavigationStick = ControllerStick.fromId(
				values.menuNavigationStick, DEFAULT_MENU_NAVIGATION_STICK).id();
		values.menuConfirmBinding = ControllerButton.fromId(
				values.menuConfirmBinding, DEFAULT_MENU_CONFIRM_BINDING).id();
		values.menuBackBinding = ControllerButton.fromId(
				values.menuBackBinding, DEFAULT_MENU_BACK_BINDING).id();
		return values;
	}

	private static double finiteClamped(double value, double fallback, double minimum, double maximum) {
		if (!Double.isFinite(value)) {
			return fallback;
		}
		return Math.max(minimum, Math.min(maximum, value));
	}

	static final class Values {
		int configVersion = CONFIG_VERSION;
		double hmdYawSensitivity = DEFAULT_HMD_YAW_SENSITIVITY;
		double hmdPitchSensitivity = DEFAULT_HMD_PITCH_SENSITIVITY;
		double controllerDeadzone = DEFAULT_CONTROLLER_DEADZONE;
		double triggerThreshold = DEFAULT_TRIGGER_THRESHOLD;
		String movementStick = DEFAULT_MOVEMENT_STICK.id();
		String jumpBinding = DEFAULT_JUMP_BINDING.id();
		String sneakBinding = DEFAULT_SNEAK_BINDING.id();
		String sprintBinding = DEFAULT_SPRINT_BINDING.id();
		String attackBinding = DEFAULT_ATTACK_BINDING.id();
		String useBinding = DEFAULT_USE_BINDING.id();
		String menuNavigationStick = DEFAULT_MENU_NAVIGATION_STICK.id();
		String menuConfirmBinding = DEFAULT_MENU_CONFIRM_BINDING.id();
		String menuBackBinding = DEFAULT_MENU_BACK_BINDING.id();

		static Values defaults() {
			return new Values();
		}

		Values copy() {
			Values copy = new Values();
			copy.configVersion = configVersion;
			copy.hmdYawSensitivity = hmdYawSensitivity;
			copy.hmdPitchSensitivity = hmdPitchSensitivity;
			copy.controllerDeadzone = controllerDeadzone;
			copy.triggerThreshold = triggerThreshold;
			copy.movementStick = movementStick;
			copy.jumpBinding = jumpBinding;
			copy.sneakBinding = sneakBinding;
			copy.sprintBinding = sprintBinding;
			copy.attackBinding = attackBinding;
			copy.useBinding = useBinding;
			copy.menuNavigationStick = menuNavigationStick;
			copy.menuConfirmBinding = menuConfirmBinding;
			copy.menuBackBinding = menuBackBinding;
			return copy;
		}

		@Override
		public boolean equals(Object object) {
			if (this == object) {
				return true;
			}
			if (!(object instanceof Values other)) {
				return false;
			}
			return configVersion == other.configVersion
					&& Double.compare(hmdYawSensitivity, other.hmdYawSensitivity) == 0
					&& Double.compare(hmdPitchSensitivity, other.hmdPitchSensitivity) == 0
					&& Double.compare(controllerDeadzone, other.controllerDeadzone) == 0
					&& Double.compare(triggerThreshold, other.triggerThreshold) == 0
					&& java.util.Objects.equals(movementStick, other.movementStick)
					&& java.util.Objects.equals(jumpBinding, other.jumpBinding)
					&& java.util.Objects.equals(sneakBinding, other.sneakBinding)
					&& java.util.Objects.equals(sprintBinding, other.sprintBinding)
					&& java.util.Objects.equals(attackBinding, other.attackBinding)
					&& java.util.Objects.equals(useBinding, other.useBinding)
					&& java.util.Objects.equals(menuNavigationStick, other.menuNavigationStick)
					&& java.util.Objects.equals(menuConfirmBinding, other.menuConfirmBinding)
					&& java.util.Objects.equals(menuBackBinding, other.menuBackBinding);
		}

		@Override
		public int hashCode() {
			int result = Integer.hashCode(configVersion);
			result = 31 * result + Double.hashCode(hmdYawSensitivity);
			result = 31 * result + Double.hashCode(hmdPitchSensitivity);
			result = 31 * result + Double.hashCode(controllerDeadzone);
			result = 31 * result + Double.hashCode(triggerThreshold);
			result = 31 * result + java.util.Objects.hashCode(movementStick);
			result = 31 * result + java.util.Objects.hashCode(jumpBinding);
			result = 31 * result + java.util.Objects.hashCode(sneakBinding);
			result = 31 * result + java.util.Objects.hashCode(sprintBinding);
			result = 31 * result + java.util.Objects.hashCode(attackBinding);
			result = 31 * result + java.util.Objects.hashCode(useBinding);
			result = 31 * result + java.util.Objects.hashCode(menuNavigationStick);
			result = 31 * result + java.util.Objects.hashCode(menuConfirmBinding);
			result = 31 * result + java.util.Objects.hashCode(menuBackBinding);
			return result;
		}
	}
}
