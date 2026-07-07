package dev.mcxrinput.client;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
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

	private static final Logger LOGGER = LoggerFactory.getLogger("MCXRInput/Config");
	private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();
	private static final int CONFIG_VERSION = 1;
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
					&& Double.compare(triggerThreshold, other.triggerThreshold) == 0;
		}

		@Override
		public int hashCode() {
			int result = Integer.hashCode(configVersion);
			result = 31 * result + Double.hashCode(hmdYawSensitivity);
			result = 31 * result + Double.hashCode(hmdPitchSensitivity);
			result = 31 * result + Double.hashCode(controllerDeadzone);
			result = 31 * result + Double.hashCode(triggerThreshold);
			return result;
		}
	}
}
