#include <mcxrinput/bridge_options.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace mcxrinput::native;

namespace {

int failures = 0;

void check(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

bool parse(std::vector<std::wstring> arguments, BridgeOptions& options) {
	std::vector<wchar_t*> pointers;
	pointers.reserve(arguments.size());
	for (std::wstring& argument : arguments) {
		pointers.push_back(argument.data());
	}
	std::wostringstream errors;
	return parseBridgeOptions(
			static_cast<int>(pointers.size()), pointers.data(), options, errors);
}

} // namespace

int main() {
	BridgeOptions options;
	check(parse({L"bridge"}, options), "no arguments preserve controls-only mode");
	check(!options.displayEnabled() && options.port == 28771,
			"controls-only defaults are preserved");

	options = {};
	check(parse({L"bridge", L"--port", L"28772"}, options),
			"controls-only custom port parses");
	check(options.port == 28772, "custom port is retained");

	options = {};
	check(parse({L"bridge", L"--executable", L"C:\\游戏\\javaw.exe"}, options),
			"absolute Unicode executable path parses");
	check(options.displayEnabled() && options.executable.has_value(),
			"executable selector enables display");
	check(options.rollCoverageDegrees == 15.0F,
			"production display defaults to validated 15-degree roll coverage");
	check(options.sourceVerticalFovDegrees == 110.0F
			&& options.worldViewScale == 1.0F,
			"production display defaults preserve the validated one-to-one view");
	check(options.menuDistanceMeters == 1.5F && options.menuWidthMeters == 1.6F,
			"finite menu screen preserves the proven geometry defaults");

	options = {};
	check(parse({L"bridge", L"--window", L"0x123ABC", L"--fit", L"stretch",
			L"--source-vfov-deg", L"90", L"--roll-coverage-deg", L"10",
			L"--eye-order", L"rl", L"--menu-distance-m", L"2.25",
			L"--menu-width-m", L"2.5"}, options),
			"window selector and display tuning parse");
	check(options.window.has_value() && options.fit == HalfSbsFitMode::stretch
			&& options.eyeOrder == BridgeEyeOrder::rightLeft
			&& options.menuDistanceMeters == 2.25F
			&& options.menuWidthMeters == 2.5F,
			"display tuning values are retained");

	options = {};
	check(parse({L"bridge", L"--window", L"0x123ABC", L"--fit", L"cover",
			L"--source-vfov-deg", L"130", L"--world-view-scale", L"0.70"}, options),
			"bounded wider-view tuning parses in cover mode");
	check(options.sourceVerticalFovDegrees == 130.0F
			&& options.worldViewScale == 0.70F,
			"wider-view source FOV and scale are retained");

	options = {};
	check(parse({L"bridge", L"--list-windows", L"--executable",
			L"C:\\Java\\javaw.exe"}, options),
			"window listing accepts executable filter");

	options = {};
	check(!parse({L"bridge", L"--fit", L"cover"}, options),
			"display tuning without selector is rejected");
	options = {};
	check(!parse({L"bridge", L"--window", L"0x1", L"--executable",
			L"C:\\Java\\javaw.exe"}, options),
			"conflicting selectors are rejected");
	options = {};
	check(!parse({L"bridge", L"--port", L"80"}, options),
			"privileged/invalid port is rejected");
	options = {};
	check(!parse({L"bridge", L"--port", L"28771", L"--port", L"28772"}, options),
			"duplicate port is rejected");
	options = {};
	check(!parse({L"bridge", L"--list-windows", L"--port", L"28771"}, options),
			"window listing rejects runtime options");
	options = {};
	check(!parse({L"bridge", L"--executable", L"C:\\Java\\javaw.exe",
			L"--menu-distance-m", L"0.1"}, options),
			"menu distance below the safe bound is rejected");
	options = {};
	check(!parse({L"bridge", L"--executable", L"C:\\Java\\javaw.exe",
			L"--menu-width-m", L"4.1"}, options),
			"menu width above the safe bound is rejected");
	options = {};
	check(!parse({L"bridge", L"--executable", L"C:\\Java\\javaw.exe",
			L"--menu-width-m", L"1.6", L"--menu-width-m", L"2.0"}, options),
			"duplicate menu geometry is rejected");
	options = {};
	check(!parse({L"bridge", L"--menu-width-m", L"1.6"}, options),
			"menu geometry without display mode is rejected");
	options = {};
	check(!parse({L"bridge", L"--executable", L"C:\\Java\\javaw.exe",
			L"--source-vfov-deg", L"130.001"}, options),
			"source FOV above the display-protocol bound is rejected");
	options = {};
	check(!parse({L"bridge", L"--executable", L"C:\\Java\\javaw.exe",
			L"--world-view-scale", L"0.699"}, options),
			"world view scale below the conservative source-capacity bound is rejected");
	options = {};
	check(!parse({L"bridge", L"--executable", L"C:\\Java\\javaw.exe",
			L"--world-view-scale", L"1.001"}, options),
			"world view scale above one-to-one is rejected");
	options = {};
	check(!parse({L"bridge", L"--executable", L"C:\\Java\\javaw.exe",
			L"--world-view-scale", L"0.9", L"--world-view-scale", L"0.95"}, options),
			"duplicate world view scale is rejected");
	options = {};
	check(!parse({L"bridge", L"--executable", L"C:\\Java\\javaw.exe",
			L"--fit", L"stretch", L"--world-view-scale", L"0.9"}, options),
			"stretch rejects an ambiguous non-default world view scale");
	options = {};
	check(!parse({L"bridge", L"--help", L"--port", L"28771"}, options),
			"help cannot be combined");

	if (failures != 0) {
		std::cerr << failures << " bridge-options test(s) failed.\n";
		return 1;
	}
	std::cout << "All bridge-options tests passed.\n";
	return 0;
}
