#pragma once

#include <mcxrinput/half_sbs_renderer.hpp>
#include <mcxrinput/window_capture.hpp>

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>

namespace mcxrinput::native {

enum class BridgeEyeOrder {
	leftRight,
	rightLeft,
};

struct BridgeOptions {
	std::uint16_t port{28771};
	bool help{false};
	bool listWindows{false};
	std::optional<std::filesystem::path> executable;
	std::optional<HWND> window;
	float rollCoverageDegrees{15.0F};
	float sourceVerticalFovDegrees{110.0F};
	float worldViewScale{1.0F};
	float menuDistanceMeters{1.5F};
	float menuWidthMeters{1.6F};
	HalfSbsFitMode fit{HalfSbsFitMode::cover};
	BridgeEyeOrder eyeOrder{BridgeEyeOrder::leftRight};

	[[nodiscard]] bool displayEnabled() const noexcept {
		return executable.has_value() || window.has_value();
	}
};

bool parseBridgeOptions(
		int argc, wchar_t** argv, BridgeOptions& options, std::wostream& errors);
void printBridgeUsage(std::wostream& output);

} // namespace mcxrinput::native
