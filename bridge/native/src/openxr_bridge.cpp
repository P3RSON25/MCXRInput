#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <mcxrinput/bridge_options.hpp>
#include <mcxrinput/bridge_output.hpp>
#include <mcxrinput/half_sbs_renderer.hpp>
#include <mcxrinput/immersive_projection.hpp>
#include <mcxrinput/openxr_d3d11.hpp>
#include <mcxrinput/window_capture.hpp>

#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace mcxrinput::native;

namespace {

constexpr std::string_view loopbackAddress = "127.0.0.1";
constexpr XrFormFactor formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
constexpr auto initialCaptureTimeout = std::chrono::seconds{10};
// Production input never stays enabled behind a capture older than the Fabric
// receiver's own 250 ms stale-input cutoff.
constexpr auto maximumCaptureAge = std::chrono::milliseconds{250};
constexpr auto maximumCaptureStarvation = std::chrono::seconds{5};
constexpr auto firstDisplayTimeout = std::chrono::seconds{30};
constexpr auto shutdownGrace = std::chrono::seconds{5};
constexpr auto statusInterval = std::chrono::seconds{1};

std::atomic_bool stopRequested{false};

template <typename Function>
class ScopeExit {
public:
	explicit ScopeExit(Function function) : function_(std::move(function)) {}
	ScopeExit(const ScopeExit&) = delete;
	ScopeExit& operator=(const ScopeExit&) = delete;
	~ScopeExit() noexcept {
		if (active_) {
			try {
				function_();
			} catch (...) {
				// Emergency cleanup is best-effort and must never mask the original
				// exception or terminate stack unwinding.
			}
		}
	}
	void release() noexcept { active_ = false; }

private:
	Function function_;
	bool active_{true};
};

template <typename Function>
ScopeExit<Function> makeScopeExit(Function function) {
	return ScopeExit<Function>{std::move(function)};
}

enum class ExitCode : int {
	success = 0,
	usage = 1,
	openXrRuntime = 2,
	noHeadset = 3,
	openXrSession = 4,
	network = 5,
	windowSelection = 6,
	capture = 7,
	invalidStereo = 8,
	rendering = 9,
};

struct HandState {
	std::string_view label;
	std::string_view userPathText;
	XrPath userPath{XR_NULL_PATH};
};

struct ActionState {
	XrActionSet actionSet{XR_NULL_HANDLE};
	XrAction triggerValue{XR_NULL_HANDLE};
	XrAction squeezeValue{XR_NULL_HANDLE};
	XrAction thumbstick{XR_NULL_HANDLE};
	XrAction thumbstickClick{XR_NULL_HANDLE};
	XrAction aClick{XR_NULL_HANDLE};
	XrAction bClick{XR_NULL_HANDLE};
	XrAction xClick{XR_NULL_HANDLE};
	XrAction yClick{XR_NULL_HANDLE};
	XrAction menuClick{XR_NULL_HANDLE};
};

struct FloatActionReport {
	XrResult result{XR_ERROR_RUNTIME_FAILURE};
	bool querySucceeded{false};
	bool active{false};
	float value{0.0F};
};

struct Vector2ActionReport {
	XrResult result{XR_ERROR_RUNTIME_FAILURE};
	bool querySucceeded{false};
	bool active{false};
	XrVector2f value{0.0F, 0.0F};
};

struct BooleanActionReport {
	XrResult result{XR_ERROR_RUNTIME_FAILURE};
	bool querySucceeded{false};
	bool active{false};
	bool value{false};
};

struct ControllerReadResult {
	XrResult result{XR_SUCCESS};
	bool querySucceeded{false};
	BridgeControllerSnapshot snapshot;
};

struct HeadCenterSample {
	XrResult result{XR_ERROR_RUNTIME_FAILURE};
	bool locateSucceeded{false};
	bool orientationValid{false};
	bool orientationTracked{false};
	bool positionValid{false};
	bool positionTracked{false};
	XrPosef pose{{0.0F, 0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F}};
};

BOOL WINAPI handleConsoleControl(DWORD controlType) {
	switch (controlType) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		stopRequested.store(true);
		return TRUE;
	default:
		return FALSE;
	}
}

template <std::size_t Size>
void copyText(std::string_view text, char (&target)[Size]) {
	static_assert(Size > 0);
	const std::size_t count = std::min(text.size(), Size - 1);
	std::memcpy(target, text.data(), count);
	target[count] = '\0';
}

std::string_view sessionStateName(XrSessionState state) {
	switch (state) {
	case XR_SESSION_STATE_UNKNOWN: return "UNKNOWN";
	case XR_SESSION_STATE_IDLE: return "IDLE";
	case XR_SESSION_STATE_READY: return "READY";
	case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
	case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
	case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
	case XR_SESSION_STATE_STOPPING: return "STOPPING";
	case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
	case XR_SESSION_STATE_EXITING: return "EXITING";
	default: return "unrecognized";
	}
}

bool stringToPath(XrInstance instance, std::string_view text, XrPath& path) {
	const std::string copy{text};
	const XrResult result = xrStringToPath(instance, copy.c_str(), &path);
	if (XR_FAILED(result)) {
		printFailure(std::string{"resolving OpenXR path "} + copy, result);
		return false;
	}
	return true;
}

void addSuggestedBinding(
		XrInstance instance,
		std::vector<XrActionSuggestedBinding>& bindings,
		XrAction action,
		std::string_view bindingPathText) {
	XrPath bindingPath = XR_NULL_PATH;
	if (stringToPath(instance, bindingPathText, bindingPath)) {
		bindings.push_back(XrActionSuggestedBinding{action, bindingPath});
	}
}

void suggestBindings(
		XrInstance instance,
		std::string_view profilePathText,
		const std::vector<XrActionSuggestedBinding>& bindings) {
	if (bindings.empty()) {
		return;
	}
	XrPath profilePath = XR_NULL_PATH;
	if (!stringToPath(instance, profilePathText, profilePath)) {
		return;
	}
	XrInteractionProfileSuggestedBinding suggested{
			XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
	suggested.interactionProfile = profilePath;
	suggested.suggestedBindings = bindings.data();
	suggested.countSuggestedBindings = static_cast<std::uint32_t>(bindings.size());
	const XrResult result = xrSuggestInteractionProfileBindings(instance, &suggested);
	if (XR_FAILED(result)) {
		std::cout << "Controller binding suggestion skipped for " << profilePathText
				  << " (" << resultToString(result) << ").\n";
	}
}

void suggestControllerBindings(XrInstance instance, const ActionState& actions) {
	std::vector<XrActionSuggestedBinding> touch;
	addSuggestedBinding(instance, touch, actions.triggerValue, "/user/hand/left/input/trigger/value");
	addSuggestedBinding(instance, touch, actions.triggerValue, "/user/hand/right/input/trigger/value");
	addSuggestedBinding(instance, touch, actions.squeezeValue, "/user/hand/left/input/squeeze/value");
	addSuggestedBinding(instance, touch, actions.squeezeValue, "/user/hand/right/input/squeeze/value");
	addSuggestedBinding(instance, touch, actions.thumbstick, "/user/hand/left/input/thumbstick");
	addSuggestedBinding(instance, touch, actions.thumbstick, "/user/hand/right/input/thumbstick");
	addSuggestedBinding(instance, touch, actions.thumbstickClick, "/user/hand/left/input/thumbstick/click");
	addSuggestedBinding(instance, touch, actions.thumbstickClick, "/user/hand/right/input/thumbstick/click");
	addSuggestedBinding(instance, touch, actions.xClick, "/user/hand/left/input/x/click");
	addSuggestedBinding(instance, touch, actions.yClick, "/user/hand/left/input/y/click");
	addSuggestedBinding(instance, touch, actions.menuClick, "/user/hand/left/input/menu/click");
	addSuggestedBinding(instance, touch, actions.aClick, "/user/hand/right/input/a/click");
	addSuggestedBinding(instance, touch, actions.bClick, "/user/hand/right/input/b/click");
	suggestBindings(instance, "/interaction_profiles/oculus/touch_controller", touch);

	std::vector<XrActionSuggestedBinding> index;
	addSuggestedBinding(instance, index, actions.triggerValue, "/user/hand/left/input/trigger/value");
	addSuggestedBinding(instance, index, actions.triggerValue, "/user/hand/right/input/trigger/value");
	addSuggestedBinding(instance, index, actions.squeezeValue, "/user/hand/left/input/squeeze/value");
	addSuggestedBinding(instance, index, actions.squeezeValue, "/user/hand/right/input/squeeze/value");
	addSuggestedBinding(instance, index, actions.thumbstick, "/user/hand/left/input/thumbstick");
	addSuggestedBinding(instance, index, actions.thumbstick, "/user/hand/right/input/thumbstick");
	addSuggestedBinding(instance, index, actions.thumbstickClick, "/user/hand/left/input/thumbstick/click");
	addSuggestedBinding(instance, index, actions.thumbstickClick, "/user/hand/right/input/thumbstick/click");
	addSuggestedBinding(instance, index, actions.aClick, "/user/hand/left/input/a/click");
	addSuggestedBinding(instance, index, actions.aClick, "/user/hand/right/input/a/click");
	addSuggestedBinding(instance, index, actions.bClick, "/user/hand/left/input/b/click");
	addSuggestedBinding(instance, index, actions.bClick, "/user/hand/right/input/b/click");
	suggestBindings(instance, "/interaction_profiles/valve/index_controller", index);
}

bool createAction(
		XrActionSet actionSet,
		XrActionType type,
		std::string_view name,
		std::string_view localizedName,
		const std::array<XrPath, 2>& subactionPaths,
		XrAction& action) {
	XrActionCreateInfo createInfo{XR_TYPE_ACTION_CREATE_INFO};
	createInfo.actionType = type;
	copyText(name, createInfo.actionName);
	copyText(localizedName, createInfo.localizedActionName);
	createInfo.countSubactionPaths = static_cast<std::uint32_t>(subactionPaths.size());
	createInfo.subactionPaths = subactionPaths.data();
	const XrResult result = xrCreateAction(actionSet, &createInfo, &action);
	if (XR_FAILED(result)) {
		printFailure(std::string{"creating controller action "} + std::string{name}, result);
		return false;
	}
	return true;
}

bool createActions(
		XrInstance instance,
		std::array<HandState, 2>& hands,
		ActionState& actions) {
	for (HandState& hand : hands) {
		if (!stringToPath(instance, hand.userPathText, hand.userPath)) {
			return false;
		}
	}
	const std::array<XrPath, 2> subactionPaths{hands[0].userPath, hands[1].userPath};

	XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
	copyText("mcxrinput_controls", actionSetInfo.actionSetName);
	copyText("MCXRInput Controls", actionSetInfo.localizedActionSetName);
	actionSetInfo.priority = 0;
	XrResult result = xrCreateActionSet(instance, &actionSetInfo, &actions.actionSet);
	if (XR_FAILED(result)) {
		printFailure("creating controller action set", result);
		return false;
	}

	const struct {
		XrActionType type;
		std::string_view name;
		std::string_view localizedName;
		XrAction& action;
	} actionInfos[] = {
			{XR_ACTION_TYPE_FLOAT_INPUT, "trigger_value", "Trigger value", actions.triggerValue},
			{XR_ACTION_TYPE_FLOAT_INPUT, "squeeze_value", "Squeeze value", actions.squeezeValue},
			{XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick", actions.thumbstick},
			{XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_click", "Thumbstick click", actions.thumbstickClick},
			{XR_ACTION_TYPE_BOOLEAN_INPUT, "a_click", "A button", actions.aClick},
			{XR_ACTION_TYPE_BOOLEAN_INPUT, "b_click", "B button", actions.bClick},
			{XR_ACTION_TYPE_BOOLEAN_INPUT, "x_click", "X button", actions.xClick},
			{XR_ACTION_TYPE_BOOLEAN_INPUT, "y_click", "Y button", actions.yClick},
			{XR_ACTION_TYPE_BOOLEAN_INPUT, "menu_click", "Menu button", actions.menuClick},
	};
	for (const auto& actionInfo : actionInfos) {
		if (!createAction(
				actions.actionSet, actionInfo.type, actionInfo.name,
				actionInfo.localizedName, subactionPaths, actionInfo.action)) {
			return false;
		}
	}
	suggestControllerBindings(instance, actions);
	return true;
}

bool attachActionSet(XrSession session, const ActionState& actions) {
	XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
	attachInfo.countActionSets = 1;
	attachInfo.actionSets = &actions.actionSet;
	const XrResult result = xrAttachSessionActionSets(session, &attachInfo);
	if (result != XR_SUCCESS) {
		printFailure("attaching controller action set", result);
		return false;
	}
	return true;
}

void destroyActions(ActionState& actions) {
	const XrAction actionHandles[] = {
			actions.triggerValue,
			actions.squeezeValue,
			actions.thumbstick,
			actions.thumbstickClick,
			actions.aClick,
			actions.bClick,
			actions.xClick,
			actions.yClick,
			actions.menuClick,
	};
	for (XrAction action : actionHandles) {
		if (action != XR_NULL_HANDLE) {
			xrDestroyAction(action);
		}
	}
	if (actions.actionSet != XR_NULL_HANDLE) {
		xrDestroyActionSet(actions.actionSet);
	}
	actions = {};
}

bool pollEvents(
		XrInstance instance,
		XrSession session,
		bool& sessionRunning,
		XrSessionState& sessionState,
		bool& shouldExit,
		bool& runtimeLoss) {
	XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
	XrResult result = xrPollEvent(instance, &event);
	while (result == XR_SUCCESS) {
		if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
			const auto& changed =
					reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
			sessionState = changed.state;
			std::cout << "Session state: " << sessionStateName(sessionState) << '\n';
			if (sessionState == XR_SESSION_STATE_READY && !sessionRunning) {
				XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
				beginInfo.primaryViewConfigurationType = primaryStereoViewConfiguration;
				const XrResult beginResult = xrBeginSession(session, &beginInfo);
				if (beginResult == XR_SESSION_LOSS_PENDING) {
					std::cerr << "OpenXR session loss became pending while beginning the session.\n";
					runtimeLoss = true;
					shouldExit = true;
				} else if (beginResult != XR_SUCCESS) {
					printFailure("beginning OpenXR bridge session", beginResult);
					return false;
				} else {
					sessionRunning = true;
					std::cout << "OpenXR bridge session running.\n";
				}
			} else if (sessionState == XR_SESSION_STATE_STOPPING) {
				if (sessionRunning) {
					const XrResult endResult = xrEndSession(session);
					sessionRunning = false;
					if (endResult == XR_SESSION_LOSS_PENDING) {
						std::cerr << "OpenXR session loss became pending while ending the session.\n";
						runtimeLoss = true;
						shouldExit = true;
					} else if (endResult != XR_SUCCESS) {
						printFailure("ending OpenXR bridge session", endResult);
						return false;
					}
				}
			} else if (sessionState == XR_SESSION_STATE_EXITING) {
				shouldExit = true;
			} else if (sessionState == XR_SESSION_STATE_LOSS_PENDING) {
				runtimeLoss = true;
				shouldExit = true;
			}
		} else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
			std::cerr << "OpenXR runtime reported instance loss pending.\n";
			runtimeLoss = true;
			shouldExit = true;
		} else if (event.type == XR_TYPE_EVENT_DATA_EVENTS_LOST) {
			const auto& lost = reinterpret_cast<const XrEventDataEventsLost&>(event);
			std::cerr << "OpenXR runtime dropped " << lost.lostEventCount << " event(s).\n";
		}
		event = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
		result = xrPollEvent(instance, &event);
	}
	if (result != XR_EVENT_UNAVAILABLE) {
		printFailure("polling OpenXR bridge events", result);
		return false;
	}
	return true;
}

bool createReferenceSpace(
		XrSession session, XrReferenceSpaceType type, XrSpace& space) {
	XrReferenceSpaceCreateInfo info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	info.referenceSpaceType = type;
	info.poseInReferenceSpace.orientation.w = 1.0F;
	const XrResult result = xrCreateReferenceSpace(session, &info, &space);
	if (result != XR_SUCCESS) {
		printFailure(
				type == XR_REFERENCE_SPACE_TYPE_LOCAL
						? "creating LOCAL reference space"
						: "creating VIEW reference space",
				result);
		return false;
	}
	return true;
}

HeadCenterSample locateHeadCenter(
		XrSession session, XrSpace viewSpace, XrSpace localSpace, XrTime displayTime) {
	(void)session;
	XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
	const XrResult result = xrLocateSpace(viewSpace, localSpace, displayTime, &location);
	HeadCenterSample sample;
	sample.result = result;
	if (result != XR_SUCCESS) {
		return sample;
	}
	sample.locateSucceeded = true;
	sample.orientationValid =
			(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
	sample.orientationTracked =
			(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
	sample.positionValid =
			(location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
	sample.positionTracked =
			(location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
	sample.pose = location.pose;
	return sample;
}

XrResult locateViews(
		XrSession session,
		XrSpace localSpace,
		XrTime displayTime,
		std::vector<XrView>& views,
		XrViewState& viewState,
		bool& countMatches) {
	for (XrView& view : views) {
		view = XrView{XR_TYPE_VIEW};
	}
	viewState = XrViewState{XR_TYPE_VIEW_STATE};
	XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
	locateInfo.viewConfigurationType = primaryStereoViewConfiguration;
	locateInfo.displayTime = displayTime;
	locateInfo.space = localSpace;
	std::uint32_t outputCount = 0;
	const XrResult result = xrLocateViews(
			session, &locateInfo, &viewState,
			static_cast<std::uint32_t>(views.size()), &outputCount, views.data());
	if (result != XR_SUCCESS) {
		return result;
	}
	countMatches = outputCount == views.size();
	return result;
}

FloatActionReport readFloatAction(
		XrSession session, XrAction action, XrPath handPath) {
	XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
	getInfo.action = action;
	getInfo.subactionPath = handPath;
	XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
	const XrResult result = xrGetActionStateFloat(session, &getInfo, &state);
	if (result != XR_SUCCESS) {
		return {result, false, false, 0.0F};
	}
	return {result, true, state.isActive == XR_TRUE, state.currentState};
}

Vector2ActionReport readVector2Action(
		XrSession session, XrAction action, XrPath handPath) {
	XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
	getInfo.action = action;
	getInfo.subactionPath = handPath;
	XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
	const XrResult result = xrGetActionStateVector2f(session, &getInfo, &state);
	if (result != XR_SUCCESS) {
		return {result, false, false, {0.0F, 0.0F}};
	}
	return {result, true, state.isActive == XR_TRUE, state.currentState};
}

BooleanActionReport readBooleanAction(
		XrSession session, XrAction action, XrPath handPath) {
	XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
	getInfo.action = action;
	getInfo.subactionPath = handPath;
	XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
	const XrResult result = xrGetActionStateBoolean(session, &getInfo, &state);
	if (result != XR_SUCCESS) {
		return {result, false, false, false};
	}
	return {result, true, state.isActive == XR_TRUE, state.currentState == XR_TRUE};
}

ControllerReadResult readControllerSnapshot(
		XrSession session, const ActionState& actions, XrPath handPath) {
	const FloatActionReport trigger = readFloatAction(session, actions.triggerValue, handPath);
	const FloatActionReport squeeze = readFloatAction(session, actions.squeezeValue, handPath);
	const Vector2ActionReport stick = readVector2Action(session, actions.thumbstick, handPath);
	const BooleanActionReport stickClick =
			readBooleanAction(session, actions.thumbstickClick, handPath);
	const BooleanActionReport a = readBooleanAction(session, actions.aClick, handPath);
	const BooleanActionReport b = readBooleanAction(session, actions.bClick, handPath);
	const BooleanActionReport x = readBooleanAction(session, actions.xClick, handPath);
	const BooleanActionReport y = readBooleanAction(session, actions.yClick, handPath);
	const BooleanActionReport menu = readBooleanAction(session, actions.menuClick, handPath);
	const bool succeeded = trigger.querySucceeded && squeeze.querySucceeded
			&& stick.querySucceeded && stickClick.querySucceeded && a.querySucceeded
			&& b.querySucceeded && x.querySucceeded && y.querySucceeded
			&& menu.querySucceeded;
	if (!succeeded) {
		const XrResult results[]{
				trigger.result, squeeze.result, stick.result, stickClick.result,
				a.result, b.result, x.result, y.result, menu.result};
		for (XrResult queryResult : results) {
			if (queryResult != XR_SUCCESS) {
				return {queryResult, false, {}};
			}
		}
		return {XR_ERROR_RUNTIME_FAILURE, false, {}};
	}

	BridgeControllerSnapshot snapshot;
	snapshot.active = trigger.active || squeeze.active || stick.active || stickClick.active
			|| a.active || b.active || x.active || y.active || menu.active;
	snapshot.trigger = trigger.active ? trigger.value : 0.0F;
	snapshot.squeeze = squeeze.active ? squeeze.value : 0.0F;
	snapshot.stick = stick.active ? stick.value : XrVector2f{0.0F, 0.0F};
	snapshot.stickClick = stickClick.active && stickClick.value;
	snapshot.a = a.active && a.value;
	snapshot.b = b.active && b.value;
	snapshot.x = x.active && x.value;
	snapshot.y = y.active && y.value;
	snapshot.menu = menu.active && menu.value;
	return {XR_SUCCESS, true, snapshot};
}

class UdpSender {
public:
	UdpSender() = default;
	UdpSender(const UdpSender&) = delete;
	UdpSender& operator=(const UdpSender&) = delete;
	~UdpSender() { close(); }

	bool open(std::uint16_t port) {
		WSADATA data{};
		const int startupResult = WSAStartup(MAKEWORD(2, 2), &data);
		if (startupResult != 0) {
			std::cerr << "WSAStartup failed (" << startupResult << ").\n";
			return false;
		}
		started_ = true;
		socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (socket_ == INVALID_SOCKET) {
			printSocketError("creating UDP socket");
			return false;
		}
		target_ = {};
		target_.sin_family = AF_INET;
		target_.sin_port = htons(port);
		if (inet_pton(AF_INET, loopbackAddress.data(), &target_.sin_addr) != 1) {
			std::cerr << "Could not parse loopback address " << loopbackAddress << ".\n";
			return false;
		}
		port_ = port;
		return true;
	}

	bool send(std::string_view payload) const {
		const int sent = ::sendto(
				socket_, payload.data(), static_cast<int>(payload.size()), 0,
				reinterpret_cast<const sockaddr*>(&target_),
				static_cast<int>(sizeof(target_)));
		return sent == static_cast<int>(payload.size());
	}

	[[nodiscard]] std::uint16_t port() const noexcept { return port_; }

	static void printSocketError(std::string_view operation) {
		std::cerr << "MCXRInput OpenXR bridge: " << operation
				  << " failed (Winsock error " << WSAGetLastError() << ").\n";
	}

private:
	void close() noexcept {
		if (socket_ != INVALID_SOCKET) {
			closesocket(socket_);
			socket_ = INVALID_SOCKET;
		}
		if (started_) {
			WSACleanup();
			started_ = false;
		}
	}

	bool started_{false};
	SOCKET socket_{INVALID_SOCKET};
	sockaddr_in target_{};
	std::uint16_t port_{28771};
};

class BridgePublisher {
public:
	explicit BridgePublisher(UdpSender& sender) : sender_(sender) {}

	bool publish(
			XrQuaternionf orientation,
			bool active,
			BridgeControllerSnapshot left,
			BridgeControllerSnapshot right) {
		const bool publishedActive = active && bridgeQuaternionIsPlausible(orientation);
		const std::string datagram = makeBridgeDatagram(
				orientation, publishedActive, left, right, bridgeTimestampNanos());
		if (!sender_.send(datagram)) {
			UdpSender::printSocketError("sending UDP datagram");
			return false;
		}
		++sentFrames_;
		if (publishedActive) {
			++activeFrames_;
			lastOrientation_ = orientation;
		} else {
			++inactiveFrames_;
			lastOrientation_ = XrQuaternionf{0.0F, 0.0F, 0.0F, 1.0F};
		}
		lastActive_ = publishedActive;
		hasPublished_ = true;
		return true;
	}

	bool publishNeutral(bool force) {
		if (!force && hasPublished_ && !lastActive_) {
			return true;
		}
		return publish(
				XrQuaternionf{0.0F, 0.0F, 0.0F, 1.0F}, false, {}, {});
	}

	void printStatus() const {
		std::cout << "UDP frames=" << sentFrames_
				  << " active=" << activeFrames_
				  << " inactive=" << inactiveFrames_
				  << " last=" << (lastActive_ ? "active" : "inactive")
				  << " rotation=(" << lastOrientation_.x << ' '
				  << lastOrientation_.y << ' ' << lastOrientation_.z << ' '
				  << lastOrientation_.w << ')';
	}

	[[nodiscard]] bool lastActive() const noexcept { return lastActive_; }

private:
	UdpSender& sender_;
	std::uint64_t sentFrames_{0};
	std::uint64_t activeFrames_{0};
	std::uint64_t inactiveFrames_{0};
	XrQuaternionf lastOrientation_{0.0F, 0.0F, 0.0F, 1.0F};
	bool lastActive_{false};
	bool hasPublished_{false};
};

std::optional<ExitCode> waitForInitialCapture(
		WindowCaptureSource& capture, ID3D11DeviceContext* context) {
	const auto deadline = std::chrono::steady_clock::now() + initialCaptureTimeout;
	bool invalidFrameObserved = false;
	while (!stopRequested.load() && std::chrono::steady_clock::now() < deadline) {
		const WindowCaptureUpdate update = capture.poll(context);
		if (update == WindowCaptureUpdate::frameReady
				&& capture.hasFreshFrame(maximumCaptureAge)) {
			return std::nullopt;
		}
		if (update == WindowCaptureUpdate::windowClosed) {
			std::wcerr << L"The selected Minecraft window closed before capture began.\n";
			return ExitCode::windowSelection;
		}
		if (update == WindowCaptureUpdate::invalidStereoFrame) {
			if (!invalidFrameObserved) {
				std::wcerr << capture.lastError() << L" Waiting for a valid frame.\n";
				invalidFrameObserved = true;
			}
			continue;
		}
		if (update == WindowCaptureUpdate::failure) {
			std::wcerr << capture.lastError() << L'\n';
			return ExitCode::capture;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	if (stopRequested.load()) {
		return ExitCode::success;
	}
	std::wcerr << L"No fresh half-SBS capture frame arrived within ten seconds.\n";
	return invalidFrameObserved ? ExitCode::invalidStereo : ExitCode::capture;
}

void printProjectionCalibration(
		const ImmersiveProjectionCalibration& calibration) {
	constexpr float radiansToDegrees = 57.29577951308232F;
	for (std::size_t index = 0; index < calibration.fovs.size(); ++index) {
		const ProjectionFov fov = calibration.fovs[index];
		const SourceUvTransform uv = calibration.sourceMappings[index];
		std::cout << (index == 0 ? "Frozen left" : "Frozen right")
				  << " projection FOV degrees [L R U D]=["
				  << fov.angleLeft * radiansToDegrees << ' '
				  << fov.angleRight * radiansToDegrees << ' '
				  << fov.angleUp * radiansToDegrees << ' '
				  << fov.angleDown * radiansToDegrees
				  << "] UV [scaleX scaleY offsetX offsetY]=["
				  << uv.scaleX << ' ' << uv.scaleY << ' '
				  << uv.offsetX << ' ' << uv.offsetY << "]\n";
	}
}

void printProjectionFailure(
		ImmersiveProjectionBuildResult result,
		const BridgeOptions& options,
		const ImmersiveProjectionFitDiagnostics& diagnostics) {
	if (result == ImmersiveProjectionBuildResult::eyePlaneCrossing) {
		std::cerr << "The requested roll coverage and runtime eye cant make a projection "
					 "ray reach the eye plane. Reduce --roll-coverage-deg; changing image "
					 "fit cannot correct this.\n";
	} else if (result == ImmersiveProjectionBuildResult::insufficientSourceFov) {
		std::cerr << "The configured " << options.sourceVerticalFovDegrees
				  << "-degree source vertical FOV cannot cover SteamVR's expanded "
					 "projection frustum.\n";
		if (diagnostics.valid) {
			constexpr float step = 0.001F;
			const float requiredLeft = std::ceil(
					diagnostics.requiredSourceVerticalFovDegrees[0] / step) * step;
			const float requiredRight = std::ceil(
					diagnostics.requiredSourceVerticalFovDegrees[1] / step) * step;
			std::cerr << std::fixed << std::setprecision(3)
					  << "  Left eye requires at least " << requiredLeft
					  << " degrees source VFOV for +/-" << options.rollCoverageDegrees
					  << " degrees roll.\n"
					  << "  Right eye requires at least " << requiredRight
					  << " degrees source VFOV for +/-" << options.rollCoverageDegrees
					  << " degrees roll.\n";
			const MaximumRollCoverage left = diagnostics.maximumRollCoverages[0];
			const MaximumRollCoverage right = diagnostics.maximumRollCoverages[1];
			if (left.supportsZeroCoverage && right.supportsZeroCoverage) {
				const float bothEyes = std::floor(std::min(left.degrees, right.degrees) / step)
						* step;
				std::cerr << "  At " << options.sourceVerticalFovDegrees
						  << " degrees source VFOV, both eyes support at most +/-"
						  << bothEyes << " degrees fixed roll.\n"
							 "Retry with --roll-coverage-deg no greater than that "
							 "conservatively rounded value. The bridge will not auto-clamp.\n";
			} else {
				std::cerr << "  At least one eye cannot fit even with zero fixed-roll "
							 "coverage.\n";
			}
			std::cerr << std::defaultfloat << std::setprecision(6);
		}
		std::cerr << "Reduce --roll-coverage-deg or use the explicitly distorted "
					 "--fit stretch comparison.\n";
	} else if (result == ImmersiveProjectionBuildResult::frozenFovExceeded) {
		std::cerr << "SteamVR's required view FOV grew outside the projection "
					 "calibration frozen at first display; refusing a zoom change.\n";
	} else if (result == ImmersiveProjectionBuildResult::sourceAspectChanged) {
		std::cerr << "The decoded source aspect changed after projection calibration; "
					 "refusing to change the frozen FOV mapping.\n";
	}
}

int runBridge(const BridgeOptions& options, const WindowCandidate* selectedWindow) {
	const bool displayMode = selectedWindow != nullptr;
	std::cout << "MCXRInput unified OpenXR HMD/controller bridge\n";
	std::cout << std::fixed << std::setprecision(3);

	UdpSender sender;
	if (!sender.open(options.port)) {
		return static_cast<int>(ExitCode::network);
	}
	BridgePublisher publisher{sender};
	std::cout << "Sending protocol v2 HMD/controller input to "
			  << loopbackAddress << ':' << sender.port() << '\n';
	if (!publisher.publishNeutral(true)) {
		return static_cast<int>(ExitCode::network);
	}

	XrInstance instance = XR_NULL_HANDLE;
	XrSession session = XR_NULL_HANDLE;
	XrSpace localSpace = XR_NULL_HANDLE;
	XrSpace viewSpace = XR_NULL_HANDLE;
	D3DState d3d;
	ActionState actions;
	std::array<HandState, 2> hands{{
			HandState{"left", "/user/hand/left"},
			HandState{"right", "/user/hand/right"},
	}};
	std::vector<SwapchainBundle> controlsSwapchains;
	std::array<SwapchainBundle, 2> displaySwapchains;
	WindowCaptureSource capture;
	HalfSbsRenderer renderer;

	auto cleanup = [&]() -> bool {
		capture.stop();
		bool gpuDrained = true;
		if (d3d.context != nullptr) {
			gpuDrained = waitForD3D11GpuIdle(d3d.device.Get(), d3d.context.Get());
			d3d.context->ClearState();
			d3d.context->Flush();
		}
		renderer.reset();
		for (SwapchainBundle& swapchain : displaySwapchains) {
			swapchain.reset();
		}
		controlsSwapchains.clear();
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
		if (instance != XR_NULL_HANDLE) {
			destroyActions(actions);
			xrDestroyInstance(instance);
			instance = XR_NULL_HANDLE;
		}
		return gpuDrained;
	};
	const std::string emergencyNeutral =
			makeNeutralBridgeDatagram(bridgeTimestampNanos());
	auto emergencyCleanup = makeScopeExit([&]() noexcept {
		// The payload is prebuilt so an allocation failure in the live loop cannot
		// prevent the final physical-input release attempt.
		sender.send(emergencyNeutral);
		try {
			cleanup();
		} catch (...) {
		}
	});

	auto finish = [&](ExitCode requestedCode) -> int {
		const bool neutralSent = publisher.publishNeutral(true);
		const bool cleanupSucceeded = cleanup();
		emergencyCleanup.release();
		if (!neutralSent) {
			return static_cast<int>(ExitCode::network);
		}
		if (!cleanupSucceeded && requestedCode == ExitCode::success) {
			return static_cast<int>(ExitCode::rendering);
		}
		return static_cast<int>(requestedCode);
	};

	std::vector<XrExtensionProperties> extensions;
	if (!enumerateInstanceExtensions(extensions)
			|| !hasExtension(extensions, XR_KHR_D3D11_ENABLE_EXTENSION_NAME)) {
		std::cerr << "The active OpenXR runtime does not provide D3D11 support.\n";
		return finish(ExitCode::openXrRuntime);
	}

	XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	copyText("MCXRInput", instanceInfo.applicationInfo.applicationName);
	instanceInfo.applicationInfo.applicationVersion = 1;
	copyText("MCXRInput native bridge", instanceInfo.applicationInfo.engineName);
	instanceInfo.applicationInfo.engineVersion = 1;
	// SteamVR rejected the newest header version during the earlier probe; the
	// bridge deliberately requests only the OpenXR 1.0 surface it uses.
	instanceInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	const char* enabledExtensions[]{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
	instanceInfo.enabledExtensionCount = 1;
	instanceInfo.enabledExtensionNames = enabledExtensions;
	XrResult result = xrCreateInstance(&instanceInfo, &instance);
	if (XR_FAILED(result)) {
		printFailure("creating an OpenXR bridge instance", result);
		std::cerr << "Check that SteamVR is installed, running, and selected as the "
					 "OpenXR runtime.\n";
		return finish(ExitCode::openXrRuntime);
	}

	XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
	result = xrGetInstanceProperties(instance, &instanceProperties);
	if (XR_SUCCEEDED(result)) {
		std::cout << "Runtime: " << instanceProperties.runtimeName << ' '
				  << XR_VERSION_MAJOR(instanceProperties.runtimeVersion) << '.'
				  << XR_VERSION_MINOR(instanceProperties.runtimeVersion) << '.'
				  << XR_VERSION_PATCH(instanceProperties.runtimeVersion) << '\n';
	}
	if (!createActions(instance, hands, actions)) {
		return finish(ExitCode::openXrSession);
	}

	XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
	systemInfo.formFactor = formFactor;
	XrSystemId systemId = XR_NULL_SYSTEM_ID;
	result = xrGetSystem(instance, &systemInfo, &systemId);
	if (XR_FAILED(result)) {
		printFailure("requesting a head-mounted display system", result);
		std::cerr << "No OpenXR HMD is available. Confirm SteamVR sees the headset.\n";
		return finish(ExitCode::noHeadset);
	}
	XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
	result = xrGetSystemProperties(instance, systemId, &systemProperties);
	if (XR_FAILED(result)) {
		printFailure("querying OpenXR system properties", result);
		return finish(ExitCode::openXrSession);
	}
	if (displayMode && systemProperties.graphicsProperties.maxLayerCount < 1) {
		std::cerr << "Immersive display requires one OpenXR composition layer.\n";
		return finish(ExitCode::openXrSession);
	}

	std::vector<XrViewConfigurationView> viewConfigs;
	if (!enumerateViewConfigurationViews(instance, systemId, viewConfigs)) {
		return finish(ExitCode::openXrSession);
	}
	if (displayMode && viewConfigs.size() != displaySwapchains.size()) {
		std::cerr << "Immersive display requires exactly two PRIMARY_STEREO views.\n";
		return finish(ExitCode::openXrSession);
	}

	PFN_xrGetD3D11GraphicsRequirementsKHR getRequirements = nullptr;
	if (!loadD3D11RequirementsFunction(instance, getRequirements)) {
		return finish(ExitCode::openXrSession);
	}
	XrGraphicsRequirementsD3D11KHR graphicsRequirements{
			XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
	result = getRequirements(instance, systemId, &graphicsRequirements);
	if (XR_FAILED(result)) {
		printFailure("querying OpenXR D3D11 requirements", result);
		return finish(ExitCode::openXrSession);
	}
	if (!createD3D11Device(graphicsRequirements, d3d)) {
		return finish(ExitCode::openXrSession);
	}

	std::uint32_t initialCaptureWidth = 0;
	std::uint32_t initialCaptureHeight = 0;
	if (displayMode) {
		if (!capture.start(selectedWindow->window, d3d.device.Get())) {
			std::wcerr << capture.lastError() << L'\n';
			return finish(ExitCode::capture);
		}
		if (const auto initialFailure = waitForInitialCapture(capture, d3d.context.Get())) {
			return finish(*initialFailure);
		}
		const WindowCaptureFrame& initialFrame = capture.latestFrame();
		if (!initialFrame.texture || initialFrame.combinedWidth < 2
				|| initialFrame.height == 0) {
			return finish(ExitCode::invalidStereo);
		}
		initialCaptureWidth = initialFrame.combinedWidth;
		initialCaptureHeight = initialFrame.height;
	}

	XrGraphicsBindingD3D11KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
	graphicsBinding.device = d3d.device.Get();
	XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionInfo.next = &graphicsBinding;
	sessionInfo.systemId = systemId;
	result = xrCreateSession(instance, &sessionInfo, &session);
	if (XR_FAILED(result)) {
		printFailure("creating D3D11 OpenXR bridge session", result);
		return finish(ExitCode::openXrSession);
	}
	if (!attachActionSet(session, actions)
			|| !createReferenceSpace(session, XR_REFERENCE_SPACE_TYPE_LOCAL, localSpace)
			|| !createReferenceSpace(session, XR_REFERENCE_SPACE_TYPE_VIEW, viewSpace)) {
		return finish(ExitCode::openXrSession);
	}

	XrEnvironmentBlendMode blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	std::int64_t swapchainFormat = 0;
	if (!chooseEnvironmentBlendMode(instance, systemId, blendMode)
			|| !(displayMode
					? chooseCapturedDisplaySwapchainFormat(session, swapchainFormat)
					: chooseSwapchainFormat(session, swapchainFormat))) {
		return finish(ExitCode::rendering);
	}

	if (displayMode) {
		for (std::size_t index = 0; index < displaySwapchains.size(); ++index) {
			const std::uint32_t width = std::min({
					viewConfigs[index].recommendedImageRectWidth,
					viewConfigs[index].maxImageRectWidth,
					systemProperties.graphicsProperties.maxSwapchainImageWidth,
			});
			const std::uint32_t height = std::min({
					viewConfigs[index].recommendedImageRectHeight,
					viewConfigs[index].maxImageRectHeight,
					systemProperties.graphicsProperties.maxSwapchainImageHeight,
			});
			if (width == 0 || height == 0
					|| !createColorSwapchain(
						session, d3d.device.Get(),
						ColorSwapchainDescription{
								width, height, swapchainFormat, 1, 1,
								XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT},
						displaySwapchains[index])) {
				return finish(ExitCode::rendering);
			}
		}
		if (!renderer.initialize(d3d.device.Get())) {
			return finish(ExitCode::rendering);
		}
	} else {
		controlsSwapchains.resize(viewConfigs.size());
		for (std::size_t index = 0; index < viewConfigs.size(); ++index) {
			if (!createSwapchain(
					session, d3d.device.Get(), viewConfigs[index], swapchainFormat,
					controlsSwapchains[index])) {
				return finish(ExitCode::rendering);
			}
		}
	}

	if (displayMode) {
		std::cout << "Mode: unified immersive display + physical HMD/controller input\n"
				  << "Initial half-SBS capture: " << initialCaptureWidth << 'x'
				  << initialCaptureHeight << '\n'
				  << "Projection targets: left " << displaySwapchains[0].width << 'x'
				  << displaySwapchains[0].height << ", right "
				  << displaySwapchains[1].width << 'x' << displaySwapchains[1].height << '\n'
				  << "Fit: " << (options.fit == HalfSbsFitMode::cover
						  ? "cover (tangent-correct FOV crop)"
						  : "stretch (complete source; distorted)")
				  << "; source vertical FOV: " << options.sourceVerticalFovDegrees
				  << " degrees\n"
				  << "Fixed roll coverage: +/-" << options.rollCoverageDegrees
				  << " degrees\n"
				  << "Frozen projection guard: "
				  << immersiveProjectionCalibrationGuardDegrees << " degrees per edge\n"
				  << "Eye routing: "
				  << (options.eyeOrder == BridgeEyeOrder::leftRight
						  ? "source L->left, R->right"
						  : "source R->left, L->right")
				  << "\nThis is a head-following 3DoF projection; translation provides no "
					 "scene parallax.\n";
	} else {
		std::cout << "Mode: controls-only dark OpenXR session (legacy-compatible)\n";
	}
	std::cout << "This process exclusively owns OpenXR focus; do not run another MCXRInput "
				 "probe or bridge beside it.\n"
			  << "Bridge is ready. Press Ctrl+C to stop.\n";

	bool sessionRunning = false;
	bool shouldExit = false;
	bool runtimeLoss = false;
	bool stopInitiated = false;
	bool exitRequested = false;
	bool shutdownFailed = false;
	bool invalidCaptureReported = false;
	bool calibrationPrinted = false;
	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
	std::optional<ExitCode> terminalFailure;
	RollFreeBasisState rollFreeBasis;
	ImmersiveProjectionCalibration projectionCalibration;
	ImmersiveProjectionFitDiagnostics projectionDiagnostics;
	std::vector<XrView> controlsViews(viewConfigs.size(), XrView{XR_TYPE_VIEW});
	std::vector<XrCompositionLayerProjectionView> controlsProjectionViews(
			controlsViews.size());
	std::uint64_t submittedFrames = 0;
	std::uint64_t staleCaptureFrames = 0;
	std::uint64_t invalidPoseFrames = 0;
	std::uint64_t actionReadFailures = 0;
	const auto loopStarted = std::chrono::steady_clock::now();
	auto lastStatus = loopStarted;
	std::chrono::steady_clock::time_point firstSubmittedAt{};
	std::chrono::steady_clock::time_point firstDisplayOpportunityAt{};
	std::chrono::steady_clock::time_point captureUnavailableSince{};
	std::chrono::steady_clock::time_point stopStartedAt{};

	while (!shouldExit) {
		if (!pollEvents(
				instance, session, sessionRunning, sessionState, shouldExit, runtimeLoss)) {
			terminalFailure = ExitCode::openXrSession;
			break;
		}
		if (shouldExit) {
			if (!stopInitiated && !terminalFailure) {
				terminalFailure = ExitCode::openXrSession;
			}
			break;
		}

		if (displayMode && !stopInitiated && !terminalFailure) {
			const WindowCaptureUpdate update = capture.poll(d3d.context.Get());
			switch (update) {
			case WindowCaptureUpdate::resized:
				std::cout << "Capture resized; input and display are neutral until a fresh "
							 "exact-sized frame arrives.\n";
				break;
			case WindowCaptureUpdate::minimized:
				std::cout << "Minecraft capture minimized; gameplay input is neutral.\n";
				break;
			case WindowCaptureUpdate::restored:
				std::cout << "Minecraft capture restored; waiting for a fresh frame.\n";
				break;
			case WindowCaptureUpdate::windowClosed:
				std::cerr << "The selected Minecraft window closed.\n";
				terminalFailure = ExitCode::windowSelection;
				break;
			case WindowCaptureUpdate::invalidStereoFrame:
				if (!invalidCaptureReported) {
					std::wcerr << capture.lastError()
							   << L" Blanking and neutralizing input until recovery.\n";
					invalidCaptureReported = true;
				}
				break;
			case WindowCaptureUpdate::failure:
				std::wcerr << capture.lastError() << L'\n';
				terminalFailure = invalidCaptureReported
						? ExitCode::invalidStereo : ExitCode::capture;
				break;
			default:
				break;
			}
			if (update == WindowCaptureUpdate::frameReady) {
				invalidCaptureReported = false;
			}
		}

		const auto now = std::chrono::steady_clock::now();
		const bool captureFreshNow = !displayMode
				|| capture.hasFreshFrame(maximumCaptureAge);
		if (displayMode && !stopInitiated
				&& firstSubmittedAt != std::chrono::steady_clock::time_point{}) {
			if (captureFreshNow) {
				captureUnavailableSince = {};
			} else if (captureUnavailableSince == std::chrono::steady_clock::time_point{}) {
				captureUnavailableSince = now;
			} else if (now - captureUnavailableSince >= maximumCaptureStarvation
					&& !terminalFailure) {
				std::cerr << "Live Minecraft capture remained unavailable for five seconds.\n";
				terminalFailure = invalidCaptureReported
						? ExitCode::invalidStereo : ExitCode::capture;
			}
		}
		if ((stopRequested.load() || terminalFailure.has_value()) && !stopInitiated) {
			stopInitiated = true;
			stopStartedAt = now;
			if (!publisher.publishNeutral(true)) {
				terminalFailure = ExitCode::network;
			}
		}
		if (stopInitiated) {
			if (now - stopStartedAt >= shutdownGrace) {
				std::cerr << "OpenXR bridge session did not stop within five seconds; "
							 "forcing fail-closed teardown.\n";
				shutdownFailed = true;
				shouldExit = true;
				continue;
			}
			if (!sessionRunning) {
				if (!exitRequested) {
					// The bridge was cancelled before a session began, or the runtime
					// had already stopped it independently.
					shouldExit = true;
				} else {
					// After STOPPING/xrEndSession, continue polling for the runtime's
					// terminal IDLE/EXITING events instead of destroying the session
					// immediately. The same bounded deadline still prevents a hang.
					publisher.publishNeutral(false);
					std::this_thread::sleep_for(std::chrono::milliseconds{10});
				}
				continue;
			}
			if (!exitRequested) {
				const XrResult exitResult = xrRequestExitSession(session);
				if (XR_SUCCEEDED(exitResult)) {
					exitRequested = true;
					if (exitResult == XR_SESSION_LOSS_PENDING) {
						runtimeLoss = true;
						shouldExit = true;
					}
				} else {
					printFailure("requesting OpenXR bridge session exit", exitResult);
					shutdownFailed = true;
					shouldExit = true;
					continue;
				}
			}
		}
		if (shouldExit) {
			continue;
		}
		if (!sessionRunning) {
			if (!publisher.publishNeutral(false)) {
				terminalFailure = ExitCode::network;
				stopInitiated = true;
				stopStartedAt = now;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds{10});
			continue;
		}

		FrameScope frame;
		if (!frame.begin(session, blendMode)) {
			terminalFailure = ExitCode::openXrSession;
			break;
		}
		const bool frameShouldRender = frame.state().shouldRender == XR_TRUE;
		if (displayMode && frameShouldRender && !stopInitiated
				&& firstSubmittedAt == std::chrono::steady_clock::time_point{}
				&& firstDisplayOpportunityAt == std::chrono::steady_clock::time_point{}) {
			// Do not punish an asleep headset or an IDLE runtime. The bounded
			// validation clock starts only after the compositor requests output.
			firstDisplayOpportunityAt = std::chrono::steady_clock::now();
		}
		bool layerReady = false;
		bool renderFailed = false;
		HeadCenterSample headSample;
		std::array<XrCompositionLayerProjectionView, 2> immersiveViews{
				XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
				XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
		};
		if (displayMode && frameShouldRender && captureFreshNow) {
			const WindowCaptureFrame& captured = capture.latestFrame();
			const float sourceAspect = static_cast<float>(captured.combinedWidth)
					/ static_cast<float>(captured.height);
			ImmersiveProjectionFrame projectionFrame;
			const ImmersiveProjectionBuildResult projectionResult =
					locateAndBuildImmersiveProjection(
						session, viewSpace, localSpace,
						frame.state().predictedDisplayTime,
						options.rollCoverageDegrees, options.fit, sourceAspect,
						options.sourceVerticalFovDegrees, rollFreeBasis,
						projectionCalibration, projectionDiagnostics,
						displaySwapchains, projectionFrame);
			if (projectionResult == ImmersiveProjectionBuildResult::success) {
				headSample.result = XR_SUCCESS;
				headSample.locateSucceeded = true;
				headSample.orientationValid = true;
				headSample.orientationTracked = true;
				headSample.positionValid = true;
				headSample.positionTracked = true;
				headSample.pose = projectionFrame.centerViewPose;
				immersiveViews = projectionFrame.projectionViews;
				if (!calibrationPrinted) {
					printProjectionCalibration(projectionCalibration);
					calibrationPrinted = true;
				}

				SwapchainImageLease physicalLeft;
				SwapchainImageLease physicalRight;
				const bool leftAcquired = physicalLeft.acquire(displaySwapchains[0]);
				const bool rightAcquired = leftAcquired
						&& physicalRight.acquire(displaySwapchains[1]);
				if (leftAcquired && rightAcquired) {
					HalfSbsRenderOptions renderOptions;
					SwapchainImageLease* sourceLeftTarget = &physicalLeft;
					SwapchainImageLease* sourceRightTarget = &physicalRight;
					if (options.eyeOrder == BridgeEyeOrder::leftRight) {
						renderOptions.left = {
								options.fit, projectionFrame.sourceMappings[0],
								options.fit == HalfSbsFitMode::cover};
						renderOptions.right = {
								options.fit, projectionFrame.sourceMappings[1],
								options.fit == HalfSbsFitMode::cover};
					} else {
						sourceLeftTarget = &physicalRight;
						sourceRightTarget = &physicalLeft;
						renderOptions.left = {
								options.fit, projectionFrame.sourceMappings[1],
								options.fit == HalfSbsFitMode::cover};
						renderOptions.right = {
								options.fit, projectionFrame.sourceMappings[0],
								options.fit == HalfSbsFitMode::cover};
					}
					const bool rendered = renderer.render(
							d3d.context.Get(), captured.texture.Get(),
							captured.combinedWidth, captured.height,
							*sourceLeftTarget, *sourceRightTarget, renderOptions);
					const bool rightReleased = physicalRight.release();
					const bool leftReleased = physicalLeft.release();
					const bool swapchainSessionLoss = physicalLeft.sessionLossPending()
							|| physicalRight.sessionLossPending();
					if (swapchainSessionLoss) {
						std::cerr << "OpenXR session loss became pending while presenting captured images.\n";
						terminalFailure = ExitCode::openXrSession;
					}
					layerReady = rendered && rightReleased && leftReleased
							&& physicalLeft.resultsAreExactSuccess()
							&& physicalRight.resultsAreExactSuccess()
							&& !swapchainSessionLoss;
					renderFailed = !layerReady && !swapchainSessionLoss;
				} else {
					if (physicalRight.acquired()) {
						physicalRight.release();
					}
					if (physicalLeft.acquired()) {
						physicalLeft.release();
					}
					const bool swapchainSessionLoss = physicalLeft.sessionLossPending()
							|| physicalRight.sessionLossPending();
					if (swapchainSessionLoss) {
						std::cerr << "OpenXR session loss became pending while acquiring captured-image targets.\n";
						terminalFailure = ExitCode::openXrSession;
					} else {
						renderFailed = true;
					}
				}
			} else if (projectionResult
					== ImmersiveProjectionBuildResult::invalidPoseOrFov) {
				++invalidPoseFrames;
			} else if (projectionResult
					== ImmersiveProjectionBuildResult::sessionLossPending) {
				std::cerr << "OpenXR session loss became pending while locating immersive views.\n";
				terminalFailure = ExitCode::openXrSession;
			} else if (projectionResult
					== ImmersiveProjectionBuildResult::openXrFailure) {
				terminalFailure = ExitCode::openXrSession;
			} else {
				printProjectionFailure(projectionResult, options, projectionDiagnostics);
				renderFailed = true;
			}
		} else if (displayMode && frameShouldRender && !captureFreshNow) {
			++staleCaptureFrames;
		} else if (!displayMode) {
			headSample = locateHeadCenter(
					session, viewSpace, localSpace, frame.state().predictedDisplayTime);
			if (headSample.result != XR_SUCCESS) {
				if (headSample.result == XR_SESSION_LOSS_PENDING) {
					std::cerr << "OpenXR session loss became pending while locating the HMD.\n";
				} else {
					printFailure("locating the central HMD pose", headSample.result);
				}
				terminalFailure = ExitCode::openXrSession;
			}
			XrViewState viewState{XR_TYPE_VIEW_STATE};
			bool viewCountMatches = false;
			const XrResult viewLocateResult = locateViews(
					session, localSpace, frame.state().predictedDisplayTime,
					controlsViews, viewState, viewCountMatches);
			if (viewLocateResult != XR_SUCCESS) {
				if (viewLocateResult == XR_SESSION_LOSS_PENDING) {
					std::cerr << "OpenXR session loss became pending while locating views.\n";
				} else {
					printFailure("locating controls-only HMD views", viewLocateResult);
				}
				terminalFailure = ExitCode::openXrSession;
			} else if (!viewCountMatches) {
				std::cerr << "OpenXR returned an unexpected controls-only view count.\n";
				terminalFailure = ExitCode::openXrSession;
			}
			const XrViewStateFlags validViewFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT
					| XR_VIEW_STATE_POSITION_VALID_BIT;
			const bool viewsValid = viewLocateResult == XR_SUCCESS && viewCountMatches
					&& (viewState.viewStateFlags & validViewFlags) == validViewFlags;
			if (viewLocateResult == XR_SUCCESS && viewCountMatches && !viewsValid) {
				++invalidPoseFrames;
			}
			layerReady = frameShouldRender && viewsValid
					&& controlsViews.size() == controlsSwapchains.size();
			if (layerReady) {
				for (std::size_t index = 0; index < controlsViews.size(); ++index) {
					bool swapchainSessionLoss = false;
					if (controlsSwapchains[index].renderTargets.empty()
							|| !clearSwapchainImage(
								controlsSwapchains[index], d3d.context.Get(),
								&swapchainSessionLoss)) {
						layerReady = false;
						if (swapchainSessionLoss) {
							std::cerr << "OpenXR session loss became pending while clearing controls-only targets.\n";
							terminalFailure = ExitCode::openXrSession;
						} else {
							renderFailed = true;
						}
						break;
					}
					XrCompositionLayerProjectionView projectionView{
							XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
					projectionView.pose = controlsViews[index].pose;
					projectionView.fov = controlsViews[index].fov;
					projectionView.subImage.swapchain = controlsSwapchains[index].swapchain;
					projectionView.subImage.imageRect.extent = {
							static_cast<std::int32_t>(controlsSwapchains[index].width),
							static_cast<std::int32_t>(controlsSwapchains[index].height),
					};
					controlsProjectionViews[index] = projectionView;
				}
			}
		}

		bool actionsSynchronized = false;
		std::array<ControllerReadResult, 2> controllerReads{};
		if (sessionState == XR_SESSION_STATE_FOCUSED && !stopInitiated) {
			XrActiveActionSet activeActionSet{};
			activeActionSet.actionSet = actions.actionSet;
			XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
			syncInfo.countActiveActionSets = 1;
			syncInfo.activeActionSets = &activeActionSet;
			result = xrSyncActions(session, &syncInfo);
			if (result == XR_SUCCESS) {
				actionsSynchronized = true;
				controllerReads = {
						readControllerSnapshot(session, actions, hands[0].userPath),
						readControllerSnapshot(session, actions, hands[1].userPath),
				};
				if (!controllerReads[0].querySucceeded
						|| !controllerReads[1].querySucceeded) {
					++actionReadFailures;
					actionsSynchronized = false;
					const XrResult queryResult = !controllerReads[0].querySucceeded
							? controllerReads[0].result : controllerReads[1].result;
					if (queryResult == XR_SESSION_LOSS_PENDING) {
						std::cerr << "OpenXR session loss became pending while reading controllers.\n";
					} else {
						printFailure("reading current controller actions", queryResult);
					}
					terminalFailure = ExitCode::openXrSession;
				}
			} else if (result == XR_SESSION_NOT_FOCUSED) {
				// Focus can change between event polling and action sync. This is a
				// neutral frame, never a synchronized physical sample.
			} else if (result == XR_SESSION_LOSS_PENDING) {
				std::cerr << "OpenXR session loss became pending while syncing actions.\n";
				terminalFailure = ExitCode::openXrSession;
			} else {
				printFailure("syncing controller actions", result);
				terminalFailure = ExitCode::openXrSession;
			}
		}

		XrCompositionLayerProjection projectionLayer{
				XR_TYPE_COMPOSITION_LAYER_PROJECTION};
		projectionLayer.space = localSpace;
		if (displayMode) {
			projectionLayer.viewCount = static_cast<std::uint32_t>(immersiveViews.size());
			projectionLayer.views = immersiveViews.data();
		} else {
			projectionLayer.viewCount =
					static_cast<std::uint32_t>(controlsProjectionViews.size());
			projectionLayer.views = controlsProjectionViews.data();
		}
		const XrCompositionLayerBaseHeader* layers[]{
				reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projectionLayer)};
		const bool frameEnded = frame.end(layerReady ? layers : nullptr, layerReady ? 1U : 0U);
		if (!frameEnded) {
			publisher.publishNeutral(true);
			terminalFailure = ExitCode::openXrSession;
			break;
		}
		const bool frameResultsExact = frame.resultsAreExactSuccess();
		if (frame.sessionLossPending()) {
			std::cerr << "OpenXR session loss became pending during frame submission.\n";
			terminalFailure = ExitCode::openXrSession;
		} else if (!frameResultsExact && frame.beginResult() != XR_FRAME_DISCARDED) {
			std::cerr << "OpenXR returned an unexpected non-success frame status.\n";
			terminalFailure = ExitCode::openXrSession;
		}
		const bool frameSubmitted = layerReady && frameResultsExact;
		if (frameSubmitted) {
			++submittedFrames;
			if (firstSubmittedAt == std::chrono::steady_clock::time_point{}) {
				firstSubmittedAt = std::chrono::steady_clock::now();
				std::cout << (displayMode
						? "First live immersive frame submitted; physical input can now be enabled.\n"
						: "First controls-only frame submitted; physical input can now be enabled.\n");
			}
		}

		BridgeInputReadiness readiness;
		readiness.stopRequested = stopInitiated || stopRequested.load()
				|| terminalFailure.has_value();
		readiness.sessionRunning = sessionRunning;
		readiness.sessionFocused = sessionState == XR_SESSION_STATE_FOCUSED;
		readiness.frameShouldRender = frameShouldRender;
		readiness.frameSubmitted = frameSubmitted;
		readiness.hmdOrientationValid = headSample.orientationValid;
		readiness.hmdOrientationTracked = headSample.orientationTracked;
		readiness.actionsSynchronized = actionsSynchronized;
		readiness.displayMode = displayMode;
		readiness.hmdPositionValid = headSample.positionValid;
		readiness.hmdPositionTracked = headSample.positionTracked;
		// Re-check after rendering/xrEndFrame so a stalled compositor cannot
		// publish active gameplay input behind a capture that aged out meanwhile.
		readiness.captureFresh = !displayMode
				|| capture.hasFreshFrame(maximumCaptureAge);
		const bool inputActive = bridgeInputIsReady(readiness)
				&& bridgeQuaternionIsPlausible(headSample.pose.orientation);
		const BridgeControllerSnapshot left =
				inputActive && controllerReads[0].querySucceeded
						? controllerReads[0].snapshot : BridgeControllerSnapshot{};
		const BridgeControllerSnapshot right =
				inputActive && controllerReads[1].querySucceeded
						? controllerReads[1].snapshot : BridgeControllerSnapshot{};
		if (!publisher.publish(headSample.pose.orientation, inputActive, left, right)) {
			terminalFailure = ExitCode::network;
			break;
		}
		if (displayMode && !stopInitiated && !terminalFailure
				&& firstSubmittedAt == std::chrono::steady_clock::time_point{}
				&& firstDisplayOpportunityAt != std::chrono::steady_clock::time_point{}
				&& std::chrono::steady_clock::now() - firstDisplayOpportunityAt
						>= firstDisplayTimeout) {
			std::cerr << "No fresh captured stereo frame was projected within 30 seconds "
						 "of the compositor requesting output.\n";
			terminalFailure = ExitCode::rendering;
		}
		if (renderFailed && !stopInitiated) {
			terminalFailure = ExitCode::rendering;
			stopInitiated = true;
			stopStartedAt = std::chrono::steady_clock::now();
		}

		const auto statusNow = std::chrono::steady_clock::now();
		if (statusNow - lastStatus >= statusInterval) {
			std::cout << "XR submitted=" << submittedFrames;
			if (displayMode) {
				const WindowCaptureStats& stats = capture.stats();
				std::cout << " capture=" << stats.usableFrames << '/'
						  << stats.receivedFrames << " stale=" << staleCaptureFrames
						  << " invalid-pose=" << invalidPoseFrames
						  << " resizes=" << stats.resizes;
			}
			std::cout << " action-read-failures=" << actionReadFailures << ' ';
			publisher.printStatus();
			std::cout << '\n';
			lastStatus = statusNow;
		}
	}

	const WindowCaptureStats finalCaptureStats = capture.stats();
	ExitCode finalCode = terminalFailure.value_or(ExitCode::success);
	if (runtimeLoss || shutdownFailed) {
		finalCode = terminalFailure.value_or(ExitCode::openXrSession);
	}
	if (displayMode) {
		std::cout << "Capture summary: received=" << finalCaptureStats.receivedFrames
				  << " usable=" << finalCaptureStats.usableFrames
				  << " discarded=" << finalCaptureStats.discardedFrames
				  << " resizes=" << finalCaptureStats.resizes << '\n';
	}
	const int code = finish(finalCode);
	if (code == static_cast<int>(ExitCode::success)) {
		std::cout << "OpenXR bridge stopped cleanly after " << submittedFrames
				  << " submitted frame(s).\n";
	}
	return code;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
	BridgeOptions options;
	if (!parseBridgeOptions(argc, argv, options, std::wcerr)) {
		printBridgeUsage(std::wcout);
		return static_cast<int>(ExitCode::usage);
	}
	if (options.help) {
		printBridgeUsage(std::wcout);
		return static_cast<int>(ExitCode::success);
	}
	if (options.listWindows) {
		auto windows = enumerateWindows();
		if (options.executable) {
			windows = filterWindowsByExecutable(windows, *options.executable);
		}
		std::wcout << L"Matching visible windows: " << windows.size() << L'\n';
		for (const WindowCandidate& window : windows) {
			printWindow(window, std::wcout);
		}
		return static_cast<int>(ExitCode::success);
	}

	std::optional<WindowCandidate> selected;
	if (options.displayEnabled()) {
		selected = selectWindow(
				WindowSelectionOptions{options.executable, options.window}, std::wcerr);
		if (!selected) {
			return static_cast<int>(ExitCode::windowSelection);
		}
		std::wcout << L"Selected window:\n";
		printWindow(*selected, std::wcout);
	}
	SetConsoleCtrlHandler(handleConsoleControl, TRUE);

	try {
		if (options.displayEnabled()) {
			winrt::init_apartment(winrt::apartment_type::multi_threaded);
		}
		return runBridge(options, selected ? &*selected : nullptr);
	} catch (const winrt::hresult_error& error) {
		std::wcerr << L"Initializing unified capture failed (HRESULT 0x" << std::hex
				   << static_cast<std::uint32_t>(error.code().value) << std::dec
				   << L"): " << error.message().c_str() << L'\n';
		return static_cast<int>(ExitCode::capture);
	} catch (const std::exception& error) {
		std::cerr << "Initializing the OpenXR bridge failed: " << error.what() << '\n';
		return static_cast<int>(ExitCode::capture);
	}
}
