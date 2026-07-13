#include <mcxrinput/bridge_output.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace mcxrinput::native {
namespace {

BridgeControllerSnapshot sanitizeController(BridgeControllerSnapshot controller) noexcept {
	if (!controller.active
			|| !std::isfinite(controller.stick.x)
			|| !std::isfinite(controller.stick.y)
			|| !std::isfinite(controller.trigger)
			|| !std::isfinite(controller.squeeze)) {
		return {};
	}
	controller.stick.x = std::clamp(controller.stick.x, -1.0F, 1.0F);
	controller.stick.y = std::clamp(controller.stick.y, -1.0F, 1.0F);
	controller.trigger = std::clamp(controller.trigger, 0.0F, 1.0F);
	controller.squeeze = std::clamp(controller.squeeze, 0.0F, 1.0F);
	return controller;
}

void appendControllerJson(
		std::ostringstream& json, const BridgeControllerSnapshot& controller) {
	json << "{\"active\":" << (controller.active ? "true" : "false")
		 << ",\"stick\":[" << controller.stick.x << ',' << controller.stick.y << ']'
		 << ",\"trigger\":" << controller.trigger
		 << ",\"squeeze\":" << controller.squeeze
		 << ",\"stickClick\":" << (controller.stickClick ? "true" : "false")
		 << ",\"a\":" << (controller.a ? "true" : "false")
		 << ",\"b\":" << (controller.b ? "true" : "false")
		 << ",\"x\":" << (controller.x ? "true" : "false")
		 << ",\"y\":" << (controller.y ? "true" : "false")
		 << ",\"menu\":" << (controller.menu ? "true" : "false")
		 << '}';
}

} // namespace

bool bridgeInputIsReady(const BridgeInputReadiness& readiness) noexcept {
	if (readiness.stopRequested
			|| !readiness.sessionRunning
			|| !readiness.sessionFocused
			|| !readiness.frameShouldRender
			|| !readiness.frameSubmitted
			|| !readiness.hmdOrientationValid
			|| !readiness.hmdOrientationTracked
			|| !readiness.actionsSynchronized) {
		return false;
	}
	if (readiness.displayMode
			&& (!readiness.hmdPositionValid
					|| !readiness.hmdPositionTracked
					|| !readiness.captureFresh)) {
		return false;
	}
	return true;
}

bool bridgeQuaternionIsPlausible(const XrQuaternionf& orientation) noexcept {
	if (!std::isfinite(orientation.x)
			|| !std::isfinite(orientation.y)
			|| !std::isfinite(orientation.z)
			|| !std::isfinite(orientation.w)) {
		return false;
	}
	const double normSquared = static_cast<double>(orientation.x) * orientation.x
			+ static_cast<double>(orientation.y) * orientation.y
			+ static_cast<double>(orientation.z) * orientation.z
			+ static_cast<double>(orientation.w) * orientation.w;
	// OpenXR supplies a unit quaternion. Keep a generous finite tolerance while
	// rejecting zero/degenerate or wildly corrupt values before JSON publication.
	return normSquared >= 0.25 && normSquared <= 4.0;
}

std::int64_t bridgeTimestampNanos() noexcept {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string makeBridgeDatagram(
		XrQuaternionf orientation,
		bool globalActive,
		BridgeControllerSnapshot leftController,
		BridgeControllerSnapshot rightController,
		std::int64_t timestampNanos) {
	if (!globalActive || !bridgeQuaternionIsPlausible(orientation)) {
		globalActive = false;
		orientation = XrQuaternionf{0.0F, 0.0F, 0.0F, 1.0F};
		leftController = {};
		rightController = {};
	} else {
		leftController = sanitizeController(leftController);
		rightController = sanitizeController(rightController);
	}

	std::ostringstream json;
	json.imbue(std::locale::classic());
	json << std::setprecision(9)
		 << "{\"version\":2,\"timestamp\":" << timestampNanos
		 << ",\"hmd\":{\"rotation\":["
		 << orientation.x << ','
		 << orientation.y << ','
		 << orientation.z << ','
		 << orientation.w << "],\"active\":"
		 << (globalActive ? "true" : "false")
		 << "},\"controllers\":{\"left\":";
	appendControllerJson(json, leftController);
	json << ",\"right\":";
	appendControllerJson(json, rightController);
	json << "}}";
	return json.str();
}

std::string makeNeutralBridgeDatagram(std::int64_t timestampNanos) {
	return makeBridgeDatagram(
			XrQuaternionf{0.0F, 0.0F, 0.0F, 1.0F}, false, {}, {}, timestampNanos);
}

} // namespace mcxrinput::native
