#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define XR_USE_PLATFORM_WIN32

#include <windows.h>
#include <unknwn.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto probeDuration = std::chrono::seconds{15};
constexpr auto statusInterval = std::chrono::milliseconds{500};

enum class ExitCode : int {
	success = 0,
	runtimeUnavailable = 10,
	runtimeNotReady = 11,
	instanceCreationFailed = 20,
	headsetUnavailable = 30,
	sessionCreationFailed = 40,
	spaceOrActionSetupFailed = 41,
	trackingUnavailable = 50,
};

struct ActionState {
	XrActionSet actionSet{XR_NULL_HANDLE};
	XrAction gripPose{XR_NULL_HANDLE};
	XrAction aimPose{XR_NULL_HANDLE};
	XrAction triggerValue{XR_NULL_HANDLE};
	XrAction squeezeValue{XR_NULL_HANDLE};
	XrAction thumbstick{XR_NULL_HANDLE};
};

struct HandState {
	const char* label;
	const char* userPathText;
	XrPath userPath{XR_NULL_PATH};
	XrSpace gripSpace{XR_NULL_HANDLE};
	XrSpace aimSpace{XR_NULL_HANDLE};
};

struct HmdReport {
	bool valid{false};
	bool orientationValid{false};
	bool positionValid{false};
	XrPosef pose{};
};

struct PoseActionReport {
	bool active{false};
	bool orientationValid{false};
	bool positionValid{false};
	XrPosef pose{};
};

struct FloatActionReport {
	bool active{false};
	float value{0.0F};
};

struct Vector2ActionReport {
	bool active{false};
	XrVector2f value{};
};

std::string_view resultName(XrResult result) {
	switch (result) {
	case XR_SUCCESS:
		return "XR_SUCCESS";
	case XR_TIMEOUT_EXPIRED:
		return "XR_TIMEOUT_EXPIRED";
	case XR_SESSION_NOT_FOCUSED:
		return "XR_SESSION_NOT_FOCUSED";
	case XR_FRAME_DISCARDED:
		return "XR_FRAME_DISCARDED";
	case XR_ERROR_RUNTIME_UNAVAILABLE:
		return "XR_ERROR_RUNTIME_UNAVAILABLE";
	case XR_ERROR_RUNTIME_FAILURE:
		return "XR_ERROR_RUNTIME_FAILURE";
	case XR_ERROR_INITIALIZATION_FAILED:
		return "XR_ERROR_INITIALIZATION_FAILED";
	case XR_ERROR_API_VERSION_UNSUPPORTED:
		return "XR_ERROR_API_VERSION_UNSUPPORTED";
	case XR_ERROR_FORM_FACTOR_UNAVAILABLE:
		return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
	case XR_ERROR_INSTANCE_LOST:
		return "XR_ERROR_INSTANCE_LOST";
	case XR_ERROR_SESSION_LOST:
		return "XR_ERROR_SESSION_LOST";
	case XR_ERROR_SESSION_NOT_RUNNING:
		return "XR_ERROR_SESSION_NOT_RUNNING";
	case XR_ERROR_SESSION_NOT_READY:
		return "XR_ERROR_SESSION_NOT_READY";
	case XR_ERROR_SESSION_NOT_STOPPING:
		return "XR_ERROR_SESSION_NOT_STOPPING";
	case XR_ERROR_GRAPHICS_DEVICE_INVALID:
		return "XR_ERROR_GRAPHICS_DEVICE_INVALID";
	case XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING:
		return "XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING";
	case XR_ERROR_PATH_UNSUPPORTED:
		return "XR_ERROR_PATH_UNSUPPORTED";
	case XR_ERROR_REFERENCE_SPACE_UNSUPPORTED:
		return "XR_ERROR_REFERENCE_SPACE_UNSUPPORTED";
	case XR_ERROR_TIME_INVALID:
		return "XR_ERROR_TIME_INVALID";
	case XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED:
		return "XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED";
	case XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED:
		return "XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED";
	case XR_ERROR_ACTIONSET_NOT_ATTACHED:
		return "XR_ERROR_ACTIONSET_NOT_ATTACHED";
	case XR_ERROR_ACTION_TYPE_MISMATCH:
		return "XR_ERROR_ACTION_TYPE_MISMATCH";
	case XR_ERROR_CALL_ORDER_INVALID:
		return "XR_ERROR_CALL_ORDER_INVALID";
	case XR_ERROR_VALIDATION_FAILURE:
		return "XR_ERROR_VALIDATION_FAILURE";
	default:
		return "unknown OpenXR result";
	}
}

std::string_view sessionStateName(XrSessionState state) {
	switch (state) {
	case XR_SESSION_STATE_UNKNOWN:
		return "UNKNOWN";
	case XR_SESSION_STATE_IDLE:
		return "IDLE";
	case XR_SESSION_STATE_READY:
		return "READY";
	case XR_SESSION_STATE_SYNCHRONIZED:
		return "SYNCHRONIZED";
	case XR_SESSION_STATE_VISIBLE:
		return "VISIBLE";
	case XR_SESSION_STATE_FOCUSED:
		return "FOCUSED";
	case XR_SESSION_STATE_STOPPING:
		return "STOPPING";
	case XR_SESSION_STATE_LOSS_PENDING:
		return "LOSS_PENDING";
	case XR_SESSION_STATE_EXITING:
		return "EXITING";
	default:
		return "unknown";
	}
}

void printFailure(std::string_view operation, XrResult result) {
	std::cerr << "MCXRInput OpenXR pose probe: " << operation << " failed ("
			  << resultName(result) << ", " << result << ").\n";
}

template <std::size_t Size>
void copyText(std::string_view text, char (&destination)[Size]) {
	const std::size_t count = std::min(text.size(), Size - 1);
	std::memcpy(destination, text.data(), count);
	destination[count] = '\0';
}

XrPosef identityPose() {
	XrPosef pose{};
	pose.orientation.w = 1.0F;
	return pose;
}

XrResult enumerateExtensions(std::vector<XrExtensionProperties>& extensions) {
	std::uint32_t count = 0;
	XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
	if (XR_FAILED(result)) {
		return result;
	}

	extensions.assign(count, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
	return xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data());
}

bool hasExtension(const std::vector<XrExtensionProperties>& extensions, const char* name) {
	return std::any_of(extensions.begin(), extensions.end(), [name](const XrExtensionProperties& extension) {
		return std::strcmp(extension.extensionName, name) == 0;
	});
}

void printVersion(XrVersion version) {
	std::cout << XR_VERSION_MAJOR(version) << '.'
			  << XR_VERSION_MINOR(version) << '.'
			  << XR_VERSION_PATCH(version);
}

bool stringToPath(XrInstance instance, const char* text, XrPath& path) {
	const XrResult result = xrStringToPath(instance, text, &path);
	if (XR_FAILED(result)) {
		printFailure(std::string{"resolving OpenXR path "} + text, result);
		return false;
	}
	return true;
}

bool loadTimeConverter(XrInstance instance, PFN_xrConvertWin32PerformanceCounterToTimeKHR& converter) {
	PFN_xrVoidFunction function = nullptr;
	const XrResult result = xrGetInstanceProcAddr(
			instance, "xrConvertWin32PerformanceCounterToTimeKHR", &function);
	if (XR_FAILED(result) || function == nullptr) {
		if (XR_FAILED(result)) {
			printFailure("loading Win32-to-OpenXR time conversion", result);
		} else {
			std::cerr << "MCXRInput OpenXR pose probe: runtime did not return a time conversion function.\n";
		}
		return false;
	}

	converter = reinterpret_cast<PFN_xrConvertWin32PerformanceCounterToTimeKHR>(function);
	return true;
}

bool currentXrTime(XrInstance instance, PFN_xrConvertWin32PerformanceCounterToTimeKHR converter, XrTime& time) {
	LARGE_INTEGER counter{};
	if (QueryPerformanceCounter(&counter) == 0) {
		std::cerr << "MCXRInput OpenXR pose probe: QueryPerformanceCounter failed.\n";
		return false;
	}

	const XrResult result = converter(instance, &counter, &time);
	if (XR_FAILED(result)) {
		printFailure("converting current time to OpenXR time", result);
		return false;
	}
	return true;
}

std::string pathToString(XrInstance instance, XrPath path) {
	if (path == XR_NULL_PATH) {
		return "(none)";
	}

	char buffer[XR_MAX_PATH_LENGTH]{};
	std::uint32_t outputCount = 0;
	const XrResult result = xrPathToString(instance, path, XR_MAX_PATH_LENGTH, &outputCount, buffer);
	if (XR_FAILED(result)) {
		return "(unprintable path)";
	}
	return std::string{buffer};
}

void addSuggestedBinding(XrInstance instance, std::vector<XrActionSuggestedBinding>& bindings,
						 XrAction action, const char* pathText) {
	XrPath path = XR_NULL_PATH;
	if (XR_SUCCEEDED(xrStringToPath(instance, pathText, &path))) {
		bindings.push_back(XrActionSuggestedBinding{action, path});
	}
}

void suggestBindings(XrInstance instance, const char* profilePathText,
					 const std::vector<XrActionSuggestedBinding>& bindings) {
	if (bindings.empty()) {
		return;
	}

	XrPath profilePath = XR_NULL_PATH;
	if (XR_FAILED(xrStringToPath(instance, profilePathText, &profilePath))) {
		return;
	}

	XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
	suggested.interactionProfile = profilePath;
	suggested.countSuggestedBindings = static_cast<std::uint32_t>(bindings.size());
	suggested.suggestedBindings = bindings.data();

	const XrResult result = xrSuggestInteractionProfileBindings(instance, &suggested);
	if (XR_FAILED(result)) {
		std::cout << "Controller binding suggestion skipped for " << profilePathText
				  << " (" << resultName(result) << ").\n";
	}
}

void suggestControllerBindings(XrInstance instance, const ActionState& actions) {
	std::vector<XrActionSuggestedBinding> simple;
	addSuggestedBinding(instance, simple, actions.gripPose, "/user/hand/left/input/grip/pose");
	addSuggestedBinding(instance, simple, actions.gripPose, "/user/hand/right/input/grip/pose");
	addSuggestedBinding(instance, simple, actions.aimPose, "/user/hand/left/input/aim/pose");
	addSuggestedBinding(instance, simple, actions.aimPose, "/user/hand/right/input/aim/pose");
	suggestBindings(instance, "/interaction_profiles/khr/simple_controller", simple);

	std::vector<XrActionSuggestedBinding> touch;
	addSuggestedBinding(instance, touch, actions.gripPose, "/user/hand/left/input/grip/pose");
	addSuggestedBinding(instance, touch, actions.gripPose, "/user/hand/right/input/grip/pose");
	addSuggestedBinding(instance, touch, actions.aimPose, "/user/hand/left/input/aim/pose");
	addSuggestedBinding(instance, touch, actions.aimPose, "/user/hand/right/input/aim/pose");
	addSuggestedBinding(instance, touch, actions.triggerValue, "/user/hand/left/input/trigger/value");
	addSuggestedBinding(instance, touch, actions.triggerValue, "/user/hand/right/input/trigger/value");
	addSuggestedBinding(instance, touch, actions.squeezeValue, "/user/hand/left/input/squeeze/value");
	addSuggestedBinding(instance, touch, actions.squeezeValue, "/user/hand/right/input/squeeze/value");
	addSuggestedBinding(instance, touch, actions.thumbstick, "/user/hand/left/input/thumbstick");
	addSuggestedBinding(instance, touch, actions.thumbstick, "/user/hand/right/input/thumbstick");
	suggestBindings(instance, "/interaction_profiles/oculus/touch_controller", touch);
	suggestBindings(instance, "/interaction_profiles/valve/index_controller", touch);
}

XrResult createAction(XrActionSet actionSet, const std::array<XrPath, 2>& subactionPaths,
					  XrActionType type, std::string_view name,
					  std::string_view localizedName, XrAction& action) {
	XrActionCreateInfo createInfo{XR_TYPE_ACTION_CREATE_INFO};
	createInfo.actionType = type;
	copyText(name, createInfo.actionName);
	copyText(localizedName, createInfo.localizedActionName);
	createInfo.countSubactionPaths = static_cast<std::uint32_t>(subactionPaths.size());
	createInfo.subactionPaths = subactionPaths.data();
	return xrCreateAction(actionSet, &createInfo, &action);
}

bool createActions(XrInstance instance, std::array<HandState, 2>& hands, ActionState& actions) {
	for (HandState& hand : hands) {
		if (!stringToPath(instance, hand.userPathText, hand.userPath)) {
			return false;
		}
	}

	const std::array<XrPath, 2> subactionPaths{hands[0].userPath, hands[1].userPath};

	XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
	copyText("mcxrinput_probe", actionSetInfo.actionSetName);
	copyText("MCXRInput Probe", actionSetInfo.localizedActionSetName);
	actionSetInfo.priority = 0;

	XrResult result = xrCreateActionSet(instance, &actionSetInfo, &actions.actionSet);
	if (XR_FAILED(result)) {
		printFailure("creating controller action set", result);
		return false;
	}

	result = createAction(actions.actionSet, subactionPaths, XR_ACTION_TYPE_POSE_INPUT,
						  "grip_pose", "Grip pose", actions.gripPose);
	if (XR_FAILED(result)) {
		printFailure("creating grip pose action", result);
		return false;
	}

	result = createAction(actions.actionSet, subactionPaths, XR_ACTION_TYPE_POSE_INPUT,
						  "aim_pose", "Aim pose", actions.aimPose);
	if (XR_FAILED(result)) {
		printFailure("creating aim pose action", result);
		return false;
	}

	result = createAction(actions.actionSet, subactionPaths, XR_ACTION_TYPE_FLOAT_INPUT,
						  "trigger_value", "Trigger value", actions.triggerValue);
	if (XR_FAILED(result)) {
		printFailure("creating trigger action", result);
		return false;
	}

	result = createAction(actions.actionSet, subactionPaths, XR_ACTION_TYPE_FLOAT_INPUT,
						  "squeeze_value", "Squeeze value", actions.squeezeValue);
	if (XR_FAILED(result)) {
		printFailure("creating squeeze action", result);
		return false;
	}

	result = createAction(actions.actionSet, subactionPaths, XR_ACTION_TYPE_VECTOR2F_INPUT,
						  "thumbstick", "Thumbstick", actions.thumbstick);
	if (XR_FAILED(result)) {
		printFailure("creating thumbstick action", result);
		return false;
	}

	suggestControllerBindings(instance, actions);
	return true;
}

bool createActionSpace(XrSession session, XrAction action, XrPath handPath, XrSpace& space,
					   std::string_view label) {
	XrActionSpaceCreateInfo createInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
	createInfo.action = action;
	createInfo.subactionPath = handPath;
	createInfo.poseInActionSpace = identityPose();

	const XrResult result = xrCreateActionSpace(session, &createInfo, &space);
	if (XR_FAILED(result)) {
		printFailure(std::string{"creating "} + std::string{label}, result);
		return false;
	}
	return true;
}

bool attachActionsAndSpaces(XrSession session, const ActionState& actions, std::array<HandState, 2>& hands) {
	XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
	attachInfo.countActionSets = 1;
	attachInfo.actionSets = &actions.actionSet;

	XrResult result = xrAttachSessionActionSets(session, &attachInfo);
	if (XR_FAILED(result)) {
		printFailure("attaching controller action set", result);
		return false;
	}

	for (HandState& hand : hands) {
		if (!createActionSpace(session, actions.gripPose, hand.userPath, hand.gripSpace,
							   std::string{hand.label} + " grip action space")) {
			return false;
		}
		if (!createActionSpace(session, actions.aimPose, hand.userPath, hand.aimSpace,
							   std::string{hand.label} + " aim action space")) {
			return false;
		}
	}

	return true;
}

bool enumerateViewConfiguration(XrInstance instance, XrSystemId systemId, std::vector<XrView>& views) {
	std::uint32_t viewCount = 0;
	XrResult result = xrEnumerateViewConfigurationViews(
			instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
	if (XR_FAILED(result) || viewCount == 0) {
		if (XR_FAILED(result)) {
			printFailure("enumerating view configuration", result);
		} else {
			std::cerr << "MCXRInput OpenXR pose probe: runtime reported no stereo views.\n";
		}
		return false;
	}

	std::vector<XrViewConfigurationView> viewConfig(viewCount);
	for (XrViewConfigurationView& view : viewConfig) {
		view.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
	}

	result = xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
											  viewCount, &viewCount, viewConfig.data());
	if (XR_FAILED(result)) {
		printFailure("querying view configuration", result);
		return false;
	}

	views.assign(viewCount, XrView{XR_TYPE_VIEW});
	return true;
}

XrEnvironmentBlendMode chooseEnvironmentBlendMode(XrInstance instance, XrSystemId systemId) {
	std::uint32_t count = 0;
	XrResult result = xrEnumerateEnvironmentBlendModes(
			instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr);
	if (XR_FAILED(result) || count == 0) {
		return XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	}

	std::vector<XrEnvironmentBlendMode> modes(count);
	result = xrEnumerateEnvironmentBlendModes(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
											 count, &count, modes.data());
	if (XR_FAILED(result) || modes.empty()) {
		return XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	}
	return modes.front();
}

HmdReport locateHmd(XrSpace viewSpace, XrSpace localSpace, XrTime time) {
	XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
	const XrResult result = xrLocateSpace(viewSpace, localSpace, time, &location);
	if (XR_FAILED(result)) {
		return {};
	}

	HmdReport report{};
	report.orientationValid = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
	report.positionValid = (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
	report.valid = report.orientationValid || report.positionValid;
	report.pose = location.pose;
	return report;
}

PoseActionReport locatePoseAction(XrSession session, XrSpace actionSpace, XrSpace localSpace,
								  XrTime displayTime, XrAction action, XrPath handPath) {
	XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
	getInfo.action = action;
	getInfo.subactionPath = handPath;

	XrActionStatePose actionState{XR_TYPE_ACTION_STATE_POSE};
	XrResult result = xrGetActionStatePose(session, &getInfo, &actionState);
	if (XR_FAILED(result) || actionState.isActive != XR_TRUE) {
		return {};
	}

	XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
	result = xrLocateSpace(actionSpace, localSpace, displayTime, &location);
	if (XR_FAILED(result)) {
		return {};
	}

	PoseActionReport report{};
	report.active = true;
	report.orientationValid = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
	report.positionValid = (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
	report.pose = location.pose;
	return report;
}

FloatActionReport readFloatAction(XrSession session, XrAction action, XrPath handPath) {
	XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
	getInfo.action = action;
	getInfo.subactionPath = handPath;

	XrActionStateFloat actionState{XR_TYPE_ACTION_STATE_FLOAT};
	const XrResult result = xrGetActionStateFloat(session, &getInfo, &actionState);
	if (XR_FAILED(result) || actionState.isActive != XR_TRUE) {
		return {};
	}
	return FloatActionReport{true, actionState.currentState};
}

Vector2ActionReport readVector2Action(XrSession session, XrAction action, XrPath handPath) {
	XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
	getInfo.action = action;
	getInfo.subactionPath = handPath;

	XrActionStateVector2f actionState{XR_TYPE_ACTION_STATE_VECTOR2F};
	const XrResult result = xrGetActionStateVector2f(session, &getInfo, &actionState);
	if (XR_FAILED(result) || actionState.isActive != XR_TRUE) {
		return {};
	}
	return Vector2ActionReport{true, actionState.currentState};
}

std::string currentInteractionProfile(XrInstance instance, XrSession session, XrPath handPath) {
	XrInteractionProfileState state{XR_TYPE_INTERACTION_PROFILE_STATE};
	const XrResult result = xrGetCurrentInteractionProfile(session, handPath, &state);
	if (XR_FAILED(result)) {
		return "(profile query failed)";
	}
	return pathToString(instance, state.interactionProfile);
}

void printVector(const XrVector3f& vector) {
	std::cout << vector.x << ' ' << vector.y << ' ' << vector.z;
}

void printQuaternion(const XrQuaternionf& quaternion) {
	std::cout << quaternion.x << ' ' << quaternion.y << ' '
			  << quaternion.z << ' ' << quaternion.w;
}

void printPoseReport(std::string_view label, const PoseActionReport& report) {
	std::cout << label << '=';
	if (!report.active) {
		std::cout << "inactive";
		return;
	}
	std::cout << (report.orientationValid ? "ori" : "no-ori") << '/'
			  << (report.positionValid ? "pos" : "no-pos");
}

void printFloatReport(std::string_view label, const FloatActionReport& report) {
	std::cout << label << '=';
	if (!report.active) {
		std::cout << "inactive";
		return;
	}
	std::cout << report.value;
}

void printVector2Report(std::string_view label, const Vector2ActionReport& report) {
	std::cout << label << '=';
	if (!report.active) {
		std::cout << "inactive";
		return;
	}
	std::cout << '(' << report.value.x << ' ' << report.value.y << ')';
}

void printStatus(XrInstance instance, XrSession session, const ActionState& actions,
				 const std::array<HandState, 2>& hands, const HmdReport& hmd,
				 XrTime displayTime, XrSpace localSpace, double elapsedSeconds) {
	std::cout << "\n[" << elapsedSeconds << "s] HMD ";
	if (!hmd.valid) {
		std::cout << "tracking=unavailable\n";
	} else {
		std::cout << "tracking="
				  << (hmd.orientationValid ? "orientation" : "no-orientation") << '/'
				  << (hmd.positionValid ? "position" : "no-position")
				  << " position=(";
		printVector(hmd.pose.position);
		std::cout << ") rotation=(";
		printQuaternion(hmd.pose.orientation);
		std::cout << ")\n";
	}

	for (const HandState& hand : hands) {
		PoseActionReport grip = locatePoseAction(session, hand.gripSpace, localSpace, displayTime,
												 actions.gripPose, hand.userPath);
		PoseActionReport aim = locatePoseAction(session, hand.aimSpace, localSpace, displayTime,
												actions.aimPose, hand.userPath);
		FloatActionReport trigger = readFloatAction(session, actions.triggerValue, hand.userPath);
		FloatActionReport squeeze = readFloatAction(session, actions.squeezeValue, hand.userPath);
		Vector2ActionReport thumbstick = readVector2Action(session, actions.thumbstick, hand.userPath);

		std::cout << "  " << hand.label
				  << " profile=" << currentInteractionProfile(instance, session, hand.userPath) << " ";
		printPoseReport("grip", grip);
		std::cout << ' ';
		printPoseReport("aim", aim);
		std::cout << ' ';
		printFloatReport("trigger", trigger);
		std::cout << ' ';
		printFloatReport("squeeze", squeeze);
		std::cout << ' ';
		printVector2Report("stick", thumbstick);
		std::cout << '\n';
	}
}

bool pollEvents(XrInstance instance, XrSessionState& sessionState, bool& shouldExit) {
	XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
	XrResult result = xrPollEvent(instance, &event);
	while (result == XR_SUCCESS) {
		switch (event.type) {
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
			const auto& stateEvent = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
			sessionState = stateEvent.state;
			std::cout << "Session state: " << sessionStateName(sessionState) << '\n';

			if (sessionState == XR_SESSION_STATE_READY) {
				std::cout << "Passive probe mode: not beginning a rendering session.\n";
			} else if (sessionState == XR_SESSION_STATE_EXITING ||
					   sessionState == XR_SESSION_STATE_LOSS_PENDING) {
				shouldExit = true;
			}
			break;
		}
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			std::cerr << "OpenXR runtime reported instance loss pending.\n";
			shouldExit = true;
			break;
		case XR_TYPE_EVENT_DATA_EVENTS_LOST: {
			const auto& lost = *reinterpret_cast<const XrEventDataEventsLost*>(&event);
			std::cerr << "OpenXR runtime dropped " << lost.lostEventCount << " event(s).\n";
			break;
		}
		default:
			break;
		}

		event = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
		result = xrPollEvent(instance, &event);
	}

	if (result != XR_EVENT_UNAVAILABLE && XR_FAILED(result)) {
		printFailure("polling OpenXR events", result);
		return false;
	}

	return true;
}

} // namespace

int main() {
	std::cout << "MCXRInput OpenXR live pose/controller probe\n";
	std::cout << std::fixed << std::setprecision(3);

	std::vector<XrExtensionProperties> extensions;
	XrResult result = enumerateExtensions(extensions);
	if (XR_FAILED(result)) {
		printFailure("loading the active OpenXR runtime", result);
		std::cerr << "Check that SteamVR is installed, running, and selected as the OpenXR runtime.\n";
		return static_cast<int>(ExitCode::runtimeUnavailable);
	}

	if (!hasExtension(extensions, XR_MND_HEADLESS_EXTENSION_NAME)) {
		std::cerr << "SteamVR did not advertise " << XR_MND_HEADLESS_EXTENSION_NAME << ".\n"
				  << "This incremental probe intentionally uses a headless OpenXR session before adding D3D11.\n";
		return static_cast<int>(ExitCode::sessionCreationFailed);
	}

	if (!hasExtension(extensions, XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME)) {
		std::cerr << "SteamVR did not advertise "
				  << XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME << ".\n"
				  << "The passive pose probe needs this extension to ask OpenXR for current-time tracking.\n";
		return static_cast<int>(ExitCode::spaceOrActionSetupFailed);
	}

	XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	copyText("MCXRInput", createInfo.applicationInfo.applicationName);
	createInfo.applicationInfo.applicationVersion = 1;
	copyText("MCXRInput native bridge", createInfo.applicationInfo.engineName);
	createInfo.applicationInfo.engineVersion = 1;
	createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);

	const char* enabledExtensions[] = {
			XR_MND_HEADLESS_EXTENSION_NAME,
			XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME,
	};
	createInfo.enabledExtensionCount = static_cast<std::uint32_t>(
			sizeof(enabledExtensions) / sizeof(enabledExtensions[0]));
	createInfo.enabledExtensionNames = enabledExtensions;

	XrInstance instance = XR_NULL_HANDLE;
	result = xrCreateInstance(&createInfo, &instance);
	if (XR_FAILED(result)) {
		printFailure("creating an OpenXR instance", result);
		if (result == XR_ERROR_RUNTIME_FAILURE || result == XR_ERROR_INITIALIZATION_FAILED) {
			std::cerr << "SteamVR is selected but not ready. Start SteamVR and confirm it sees the headset.\n";
			return static_cast<int>(ExitCode::runtimeNotReady);
		}
		return static_cast<int>(ExitCode::instanceCreationFailed);
	}

	XrSession session = XR_NULL_HANDLE;
	XrSpace localSpace = XR_NULL_HANDLE;
	XrSpace viewSpace = XR_NULL_HANDLE;
	PFN_xrConvertWin32PerformanceCounterToTimeKHR convertWin32CounterToXrTime = nullptr;
	ActionState actions{};
	std::array<HandState, 2> hands{{
			HandState{"left", "/user/hand/left"},
			HandState{"right", "/user/hand/right"},
	}};

	auto cleanup = [&]() {
		for (HandState& hand : hands) {
			if (hand.gripSpace != XR_NULL_HANDLE) {
				xrDestroySpace(hand.gripSpace);
				hand.gripSpace = XR_NULL_HANDLE;
			}
			if (hand.aimSpace != XR_NULL_HANDLE) {
				xrDestroySpace(hand.aimSpace);
				hand.aimSpace = XR_NULL_HANDLE;
			}
		}
		if (viewSpace != XR_NULL_HANDLE) {
			xrDestroySpace(viewSpace);
			viewSpace = XR_NULL_HANDLE;
		}
		if (localSpace != XR_NULL_HANDLE) {
			xrDestroySpace(localSpace);
			localSpace = XR_NULL_HANDLE;
		}
		if (session != XR_NULL_HANDLE) {
			xrDestroySession(session);
			session = XR_NULL_HANDLE;
		}
		if (actions.actionSet != XR_NULL_HANDLE) {
			xrDestroyActionSet(actions.actionSet);
			actions.actionSet = XR_NULL_HANDLE;
		}
		if (instance != XR_NULL_HANDLE) {
			xrDestroyInstance(instance);
			instance = XR_NULL_HANDLE;
		}
	};

	XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
	result = xrGetInstanceProperties(instance, &instanceProperties);
	if (XR_FAILED(result)) {
		printFailure("querying runtime properties", result);
		cleanup();
		return static_cast<int>(ExitCode::runtimeUnavailable);
	}

	std::cout << "Runtime: " << instanceProperties.runtimeName << ' ';
	printVersion(instanceProperties.runtimeVersion);
	std::cout << '\n';
	std::cout << "Using passive " << XR_MND_HEADLESS_EXTENSION_NAME
			  << " polling to avoid taking over the headset display in this milestone.\n";

	if (!loadTimeConverter(instance, convertWin32CounterToXrTime)) {
		cleanup();
		return static_cast<int>(ExitCode::spaceOrActionSetupFailed);
	}

	if (!createActions(instance, hands, actions)) {
		cleanup();
		return static_cast<int>(ExitCode::spaceOrActionSetupFailed);
	}

	XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrSystemId systemId = XR_NULL_SYSTEM_ID;
	result = xrGetSystem(instance, &systemInfo, &systemId);
	if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
		std::cerr << "No head-mounted display is currently available.\n"
				  << "Start SteamVR, connect the Quest through Steam Link, and confirm SteamVR sees the headset.\n";
		cleanup();
		return static_cast<int>(ExitCode::headsetUnavailable);
	}
	if (XR_FAILED(result)) {
		printFailure("requesting the head-mounted display", result);
		cleanup();
		return static_cast<int>(ExitCode::headsetUnavailable);
	}

	XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionInfo.systemId = systemId;
	result = xrCreateSession(instance, &sessionInfo, &session);
	if (XR_FAILED(result)) {
		printFailure("creating a headless OpenXR session", result);
		std::cerr << "If SteamVR rejects this, the next native step is a tiny D3D11 binding; no Minecraft code is needed.\n";
		cleanup();
		return static_cast<int>(ExitCode::sessionCreationFailed);
	}

	XrReferenceSpaceCreateInfo localSpaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	localSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	localSpaceInfo.poseInReferenceSpace = identityPose();
	result = xrCreateReferenceSpace(session, &localSpaceInfo, &localSpace);
	if (XR_FAILED(result)) {
		printFailure("creating local reference space", result);
		cleanup();
		return static_cast<int>(ExitCode::spaceOrActionSetupFailed);
	}

	XrReferenceSpaceCreateInfo viewSpaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	viewSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	viewSpaceInfo.poseInReferenceSpace = identityPose();
	result = xrCreateReferenceSpace(session, &viewSpaceInfo, &viewSpace);
	if (XR_FAILED(result)) {
		printFailure("creating HMD view reference space", result);
		std::cerr << "SteamVR may require the next D3D11 session milestone for live HMD poses.\n";
		cleanup();
		return static_cast<int>(ExitCode::spaceOrActionSetupFailed);
	}

	if (!attachActionsAndSpaces(session, actions, hands)) {
		cleanup();
		return static_cast<int>(ExitCode::spaceOrActionSetupFailed);
	}

	std::cout << "Running for " << probeDuration.count()
			  << " seconds. Move the headset; wake controllers if their lines stay inactive.\n";

	bool shouldExit = false;
	bool sawValidHmdPose = false;
	bool printedActionSyncWarning = false;
	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
	const Clock::time_point start = Clock::now();
	Clock::time_point lastStatus = start - statusInterval;

	while (!shouldExit && Clock::now() - start < probeDuration) {
		if (!pollEvents(instance, sessionState, shouldExit)) {
			cleanup();
			return static_cast<int>(ExitCode::runtimeNotReady);
		}

		XrTime sampleTime = 0;
		if (!currentXrTime(instance, convertWin32CounterToXrTime, sampleTime)) {
			cleanup();
			return static_cast<int>(ExitCode::runtimeNotReady);
		}

		XrActiveActionSet activeActionSet{};
		activeActionSet.actionSet = actions.actionSet;
		activeActionSet.subactionPath = XR_NULL_PATH;

		XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
		syncInfo.countActiveActionSets = 1;
		syncInfo.activeActionSets = &activeActionSet;
		result = xrSyncActions(session, &syncInfo);
		if ((XR_FAILED(result) || result == XR_SESSION_NOT_FOCUSED) && !printedActionSyncWarning) {
			std::cout << "Controller action sync is not active yet ("
					  << resultName(result) << "). HMD polling will continue.\n";
			printedActionSyncWarning = true;
		}

		HmdReport hmd = locateHmd(viewSpace, localSpace, sampleTime);
		sawValidHmdPose = sawValidHmdPose || hmd.orientationValid;

		const Clock::time_point now = Clock::now();
		if (now - lastStatus >= statusInterval) {
			const double elapsedSeconds = std::chrono::duration<double>(now - start).count();
			printStatus(instance, session, actions, hands, hmd, sampleTime, localSpace, elapsedSeconds);
			lastStatus = now;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds{25});
	}

	cleanup();

	if (!sawValidHmdPose) {
		std::cerr << "No valid HMD orientation pose was observed during the probe.\n"
				  << "Confirm SteamVR is focused/running and the headset is awake.\n";
		return static_cast<int>(ExitCode::trackingUnavailable);
	}

	std::cout << "\nLive OpenXR pose/controller probe completed successfully.\n";
	return static_cast<int>(ExitCode::success);
}
