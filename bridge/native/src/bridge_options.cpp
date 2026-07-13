#include <mcxrinput/bridge_options.hpp>

#include <charconv>
#include <cmath>
#include <ostream>
#include <string>
#include <string_view>

namespace mcxrinput::native {
namespace {

bool narrowAscii(std::wstring_view text, std::string& output) {
	output.clear();
	output.reserve(text.size());
	for (wchar_t character : text) {
		if (character < 0 || character > 127) {
			return false;
		}
		output.push_back(static_cast<char>(character));
	}
	return true;
}

bool parsePort(std::wstring_view text, std::uint16_t& output) {
	std::string narrow;
	if (!narrowAscii(text, narrow)) {
		return false;
	}
	int candidate = 0;
	const auto parsed = std::from_chars(
			narrow.data(), narrow.data() + narrow.size(), candidate);
	if (parsed.ec != std::errc{} || parsed.ptr != narrow.data() + narrow.size()
			|| candidate < 1024 || candidate > 65535) {
		return false;
	}
	output = static_cast<std::uint16_t>(candidate);
	return true;
}

bool parseFloat(
		std::wstring_view text, float minimum, float maximum, float& output) {
	std::string narrow;
	if (!narrowAscii(text, narrow)) {
		return false;
	}
	float candidate = 0.0F;
	const auto parsed = std::from_chars(
			narrow.data(), narrow.data() + narrow.size(), candidate);
	if (parsed.ec != std::errc{} || parsed.ptr != narrow.data() + narrow.size()
			|| !std::isfinite(candidate) || candidate < minimum || candidate > maximum) {
		return false;
	}
	output = candidate;
	return true;
}

} // namespace

void printBridgeUsage(std::wostream& output) {
	output
			<< L"Usage:\n"
			<< L"  MCXRInputOpenXRBridge.exe [--port 28771]\n"
			<< L"  MCXRInputOpenXRBridge.exe (--executable <absolute-path> | --window <0xHWND>)\n"
			<< L"      [--port 28771] [--eye-order lr|rl] [--fit cover|stretch]\n"
			<< L"      [--source-vfov-deg <30..110>] [--roll-coverage-deg <0..45>]\n"
			<< L"      [--menu-distance-m <0.25..5>] [--menu-width-m <0.25..4>]\n"
			<< L"  MCXRInputOpenXRBridge.exe --list-windows [--executable <absolute-path>]\n\n"
			<< L"With no window selector, the bridge retains its controls-only dark OpenXR\n"
			<< L"session. A window selector enables automatic immersive-world/finite-menu\n"
			<< L"ReShade half-SBS display in the same OpenXR session as input and UDP.\n";
}

bool parseBridgeOptions(
		int argc, wchar_t** argv, BridgeOptions& options, std::wostream& errors) {
	bool portSeen = false;
	bool executableSeen = false;
	bool windowSeen = false;
	bool rollCoverageSeen = false;
	bool sourceVerticalFovSeen = false;
	bool menuDistanceSeen = false;
	bool menuWidthSeen = false;
	bool fitSeen = false;
	bool eyeOrderSeen = false;

	for (int index = 1; index < argc; ++index) {
		const std::wstring_view argument{argv[index]};
		if (argument == L"--help" || argument == L"-h") {
			if (options.help) {
				errors << L"Duplicate --help option.\n";
				return false;
			}
			options.help = true;
		} else if (argument == L"--list-windows") {
			if (options.listWindows) {
				errors << L"Duplicate --list-windows option.\n";
				return false;
			}
			options.listWindows = true;
		} else if (argument == L"--port") {
			if (portSeen || index + 1 >= argc
					|| !parsePort(argv[++index], options.port)) {
				errors << L"Expected one --port value from 1024 to 65535.\n";
				return false;
			}
			portSeen = true;
		} else if (argument == L"--executable") {
			if (executableSeen || index + 1 >= argc) {
				errors << L"Expected one --executable followed by an absolute path.\n";
				return false;
			}
			executableSeen = true;
			options.executable = std::filesystem::path{argv[++index]};
		} else if (argument == L"--window") {
			if (windowSeen || index + 1 >= argc) {
				errors << L"Expected one --window followed by a hexadecimal handle.\n";
				return false;
			}
			HWND candidate = nullptr;
			if (!parseWindowHandle(argv[++index], candidate)) {
				errors << L"Expected --window in hexadecimal form, for example 0x123ABC.\n";
				return false;
			}
			windowSeen = true;
			options.window = candidate;
		} else if (argument == L"--roll-coverage-deg") {
			if (rollCoverageSeen || index + 1 >= argc
					|| !parseFloat(argv[++index], 0.0F, 45.0F,
							options.rollCoverageDegrees)) {
				errors << L"Expected one --roll-coverage-deg value from 0 to 45.\n";
				return false;
			}
			rollCoverageSeen = true;
		} else if (argument == L"--source-vfov-deg") {
			if (sourceVerticalFovSeen || index + 1 >= argc
					|| !parseFloat(argv[++index], 30.0F, 110.0F,
							options.sourceVerticalFovDegrees)) {
				errors << L"Expected one --source-vfov-deg value from 30 to 110.\n";
				return false;
			}
			sourceVerticalFovSeen = true;
		} else if (argument == L"--menu-distance-m") {
			if (menuDistanceSeen || index + 1 >= argc
					|| !parseFloat(argv[++index], 0.25F, 5.0F,
							options.menuDistanceMeters)) {
				errors << L"Expected one --menu-distance-m value from 0.25 to 5.\n";
				return false;
			}
			menuDistanceSeen = true;
		} else if (argument == L"--menu-width-m") {
			if (menuWidthSeen || index + 1 >= argc
					|| !parseFloat(argv[++index], 0.25F, 4.0F,
							options.menuWidthMeters)) {
				errors << L"Expected one --menu-width-m value from 0.25 to 4.\n";
				return false;
			}
			menuWidthSeen = true;
		} else if (argument == L"--fit") {
			if (fitSeen || index + 1 >= argc) {
				errors << L"Expected one --fit value: cover or stretch.\n";
				return false;
			}
			const std::wstring_view value{argv[++index]};
			if (value == L"cover") {
				options.fit = HalfSbsFitMode::cover;
			} else if (value == L"stretch") {
				options.fit = HalfSbsFitMode::stretch;
			} else {
				errors << L"Expected --fit to be cover or stretch.\n";
				return false;
			}
			fitSeen = true;
		} else if (argument == L"--eye-order") {
			if (eyeOrderSeen || index + 1 >= argc) {
				errors << L"Expected one --eye-order value: lr or rl.\n";
				return false;
			}
			const std::wstring_view value{argv[++index]};
			if (value == L"lr") {
				options.eyeOrder = BridgeEyeOrder::leftRight;
			} else if (value == L"rl") {
				options.eyeOrder = BridgeEyeOrder::rightLeft;
			} else {
				errors << L"Expected --eye-order to be lr or rl.\n";
				return false;
			}
			eyeOrderSeen = true;
		} else {
			errors << L"Unknown argument: " << argument << L'\n';
			return false;
		}
	}

	if (options.help) {
		if (argc != 2) {
			errors << L"--help cannot be combined with other options.\n";
			return false;
		}
		return true;
	}
	if (options.executable) {
		if (options.executable->empty() || !options.executable->is_absolute()) {
			errors << L"--executable must be an absolute path.\n";
			return false;
		}
		*options.executable = options.executable->lexically_normal();
	}
	if (options.listWindows) {
		if (windowSeen || portSeen || rollCoverageSeen || sourceVerticalFovSeen
				|| menuDistanceSeen || menuWidthSeen || fitSeen || eyeOrderSeen) {
			errors << L"--list-windows accepts only the optional --executable filter.\n";
			return false;
		}
		return true;
	}
	if (executableSeen && windowSeen) {
		errors << L"Display mode accepts exactly one of --executable or --window.\n";
		return false;
	}
	if (!options.displayEnabled()
			&& (rollCoverageSeen || sourceVerticalFovSeen || menuDistanceSeen
					|| menuWidthSeen || fitSeen || eyeOrderSeen)) {
		errors << L"Display tuning options require --executable or --window.\n";
		return false;
	}
	return true;
}

} // namespace mcxrinput::native
