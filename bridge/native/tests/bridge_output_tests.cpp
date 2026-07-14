#include <mcxrinput/bridge_output.hpp>

#include <iostream>
#include <limits>
#include <string>

using namespace mcxrinput::native;

namespace {

int failures = 0;

void check(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

BridgeInputReadiness readySignals(bool displayMode) {
	BridgeInputReadiness readiness;
	readiness.sessionRunning = true;
	readiness.sessionFocused = true;
	readiness.frameShouldRender = true;
	readiness.frameSubmitted = true;
	readiness.hmdOrientationValid = true;
	readiness.hmdOrientationTracked = true;
	readiness.actionsSynchronized = true;
	readiness.displayMode = displayMode;
	readiness.hmdPositionValid = true;
	readiness.hmdPositionTracked = true;
	readiness.captureFresh = true;
	return readiness;
}

bool contains(const std::string& text, const std::string& expected) {
	return text.find(expected) != std::string::npos;
}

} // namespace

int main() {
	BridgeInputReadiness controlsOnly = readySignals(false);
	check(bridgeInputIsReady(controlsOnly), "healthy controls-only frame is active");
	controlsOnly.captureFresh = false;
	controlsOnly.hmdPositionTracked = false;
	check(bridgeInputIsReady(controlsOnly),
			"controls-only mode does not depend on capture or position");

	BridgeInputReadiness display = readySignals(true);
	check(bridgeInputIsReady(display), "healthy display frame is active");
	display.sessionFocused = false;
	check(!bridgeInputIsReady(display), "focus loss closes gate");
	display = readySignals(true);
	display.hmdOrientationTracked = false;
	check(!bridgeInputIsReady(display), "orientation tracking loss closes gate");
	display = readySignals(true);
	display.actionsSynchronized = false;
	check(!bridgeInputIsReady(display), "action sync failure closes gate");
	display = readySignals(true);
	display.captureFresh = false;
	check(!bridgeInputIsReady(display), "stale display capture closes gate");
	display = readySignals(true);
	display.frameSubmitted = false;
	check(!bridgeInputIsReady(display), "unsubmitted frame closes gate");
	display = readySignals(true);
	display.stopRequested = true;
	check(!bridgeInputIsReady(display), "stop request cannot re-arm input");

	BridgeControllerSnapshot pressed;
	pressed.active = true;
	pressed.stick = {0.5F, -0.25F};
	pressed.trigger = 1.0F;
	pressed.a = true;
	BridgeGripPoseSnapshot gripPose;
	gripPose.active = true;
	gripPose.positionValid = true;
	gripPose.positionTracked = true;
	gripPose.orientationValid = true;
	gripPose.orientationTracked = false;
	gripPose.pose.position = {0.25F, -0.5F, -0.75F};
	gripPose.pose.orientation = {0.0F, 0.70710677F, 0.0F, 0.70710677F};
	pressed.gripPose = gripPose;
	const std::string tracked = makeBridgeDatagram(
			XrQuaternionf{0.0F, 0.0F, 0.0F, 1.0F}, true, pressed, {}, 122);
	check(contains(tracked,
			"\"gripPose\":{\"active\":true,\"positionValid\":true,"
			"\"positionTracked\":true,\"orientationValid\":true,"
			"\"orientationTracked\":false,\"position\":[0.25,-0.5,-0.75],"
			"\"rotation\":[0,0.707106769,0,0.707106769]}"),
			"tracked grip pose uses the backward-compatible optional schema");
	check(!contains(tracked.substr(tracked.find("\"right\":")), "\"gripPose\""),
			"missing grip pose is omitted");
	BridgeControllerSnapshot poseOnlyController;
	poseOnlyController.gripPose = gripPose;
	const std::string poseOnly = makeBridgeDatagram(
			XrQuaternionf{0.0F, 0.0F, 0.0F, 1.0F}, true,
			poseOnlyController, {}, 122);
	check(contains(poseOnly, "\"left\":{\"active\":false")
			&& contains(poseOnly, "\"gripPose\":{\"active\":true"),
			"tracked grip remains available when button actions are inactive");

	const std::string neutral = makeBridgeDatagram(
			XrQuaternionf{0.0F, 0.0F, 0.0F, 1.0F}, false, pressed, pressed, 123);
	check(!contains(neutral, "\"active\":true"),
			"global inactive frame contains no active HMD or controller");
	check(contains(neutral, "\"stick\":[0,0]"),
			"global inactive frame zeros controller sticks");
	check(contains(neutral, "\"trigger\":0"),
			"global inactive frame zeros controller triggers");
	check(!contains(neutral, "\"a\":true"),
			"global inactive frame clears controller buttons");
	check(!contains(neutral, "\"gripPose\""),
			"global inactive frame omits tracked grip poses");

	BridgeControllerSnapshot inactiveController;
	BridgeGripPoseSnapshot inactiveGrip;
	inactiveGrip.pose.position = {
			std::numeric_limits<float>::quiet_NaN(), 2.0F, 3.0F};
	inactiveController.gripPose = inactiveGrip;
	const std::string inactivePose = makeBridgeDatagram(
			XrQuaternionf{0.0F, 0.0F, 0.0F, 1.0F}, true,
			inactiveController, {}, 123);
	check(contains(inactivePose,
			"\"gripPose\":{\"active\":false,\"positionValid\":false,"
			"\"positionTracked\":false,\"orientationValid\":false,"
			"\"orientationTracked\":false,\"position\":[0,0,0],"
			"\"rotation\":[0,0,0,1]}"),
			"inactive grip keeps false flags and sanitizes unspecified components");
	check(!contains(inactivePose, "nan"),
			"unspecified inactive grip components cannot leak non-finite JSON");

	const float nan = std::numeric_limits<float>::quiet_NaN();
	pressed.stick = {nan, 0.0F};
	const std::string badController = makeBridgeDatagram(
			XrQuaternionf{0.0F, 0.0F, 0.0F, 1.0F}, true, pressed, {}, 124);
	check(contains(badController, "\"hmd\":{\"rotation\":[0,0,0,1],\"active\":true}"),
			"invalid controller does not deactivate valid HMD");
	check(!contains(badController, "nan"), "non-finite controller value never enters JSON");

	pressed.stick = {0.5F, 0.0F};
	gripPose.pose.position.x = nan;
	pressed.gripPose = gripPose;
	const std::string badGrip = makeBridgeDatagram(
			XrQuaternionf{0.0F, 0.0F, 0.0F, 1.0F}, true, pressed, {}, 124);
	check(contains(badGrip, "\"a\":true"),
			"invalid optional grip pose does not neutralize controller buttons");
	check(!contains(badGrip, "\"gripPose\""),
			"invalid optional grip pose is omitted independently");
	check(!contains(badGrip, "nan"),
			"invalid grip pose never enters JSON");

	const std::string badPose = makeBridgeDatagram(
			XrQuaternionf{nan, 0.0F, 0.0F, 1.0F}, true, pressed, pressed, 125);
	check(!contains(badPose, "\"active\":true"),
			"invalid HMD pose neutralizes the entire frame");
	check(!contains(badPose, "nan"), "non-finite HMD value never enters JSON");

	if (failures != 0) {
		std::cerr << failures << " bridge-output test(s) failed.\n";
		return 1;
	}
	std::cout << "All bridge-output tests passed.\n";
	return 0;
}
