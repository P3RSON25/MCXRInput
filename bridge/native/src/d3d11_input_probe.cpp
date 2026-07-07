#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11

#include <windows.h>
#include <unknwn.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

constexpr auto probeDuration = std::chrono::seconds{20};
constexpr auto statusInterval = std::chrono::milliseconds{500};

enum class ExitCode : int {
	success = 0,
	runtimeUnavailable = 10,
	runtimeNotReady = 11,
	instanceCreationFailed = 20,
	headsetUnavailable = 30,
	d3dSetupFailed = 40,
	sessionCreationFailed = 41,
	spaceOrActionSetupFailed = 42,
	swapchainSetupFailed = 43,
	trackingUnavailable = 50,
};

struct ActionState {
	XrActionSet actionSet{XR_NULL_HANDLE};
	XrAction gripPose{XR_NULL_HANDLE};
	XrAction aimPose{XR_NULL_HANDLE};
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

struct HandState {
	const char* label;
	const char* userPathText;
	XrPath userPath{XR_NULL_PATH};
	XrSpace gripSpace{XR_NULL_HANDLE};
	XrSpace aimSpace{XR_NULL_HANDLE};
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

struct BooleanActionReport {
	bool active{false};
	bool value{false};
};

struct FloatWindow {
	FloatActionReport current;
	bool activeSeen{false};
	float peak{0.0F};
};

struct Vector2Window {
	Vector2ActionReport current;
	bool activeSeen{false};
	float peakMagnitude{0.0F};
	float maxAbsX{0.0F};
	float maxAbsY{0.0F};
};

struct BooleanWindow {
	BooleanActionReport current;
	bool activeSeen{false};
	bool pressed{false};
};

struct HandInputWindow {
	std::string profile{"(none)"};
	PoseActionReport grip;
	PoseActionReport aim;
	FloatWindow trigger;
	FloatWindow squeeze;
	Vector2Window stick;
	BooleanWindow stickClick;
	BooleanWindow a;
	BooleanWindow b;
	BooleanWindow x;
	BooleanWindow y;
	BooleanWindow menu;
};

struct SwapchainBundle {
	XrSwapchain swapchain{XR_NULL_HANDLE};
	std::uint32_t width{0};
	std::uint32_t height{0};
	std::vector<XrSwapchainImageD3D11KHR> images;
	std::vector<ComPtr<ID3D11RenderTargetView>> renderTargets;
};

struct D3DState {
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL featureLevel{};
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
	std::cerr << "MCXRInput OpenXR D3D11 input probe: " << operation << " failed ("
			  << resultName(result) << ", " << result << ").\n";
}

void printHresult(std::string_view operation, HRESULT result) {
	std::cerr << "MCXRInput OpenXR D3D11 input probe: " << operation
			  << " failed (HRESULT 0x" << std::hex << static_cast<unsigned long>(result)
			  << std::dec << ").\n";
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
	addSuggestedBinding(instance, touch, actions.thumbstickClick, "/user/hand/left/input/thumbstick/click");
	addSuggestedBinding(instance, touch, actions.thumbstickClick, "/user/hand/right/input/thumbstick/click");
	addSuggestedBinding(instance, touch, actions.xClick, "/user/hand/left/input/x/click");
	addSuggestedBinding(instance, touch, actions.yClick, "/user/hand/left/input/y/click");
	addSuggestedBinding(instance, touch, actions.menuClick, "/user/hand/left/input/menu/click");
	addSuggestedBinding(instance, touch, actions.aClick, "/user/hand/right/input/a/click");
	addSuggestedBinding(instance, touch, actions.bClick, "/user/hand/right/input/b/click");
	suggestBindings(instance, "/interaction_profiles/oculus/touch_controller", touch);

	std::vector<XrActionSuggestedBinding> index;
	addSuggestedBinding(instance, index, actions.gripPose, "/user/hand/left/input/grip/pose");
	addSuggestedBinding(instance, index, actions.gripPose, "/user/hand/right/input/grip/pose");
	addSuggestedBinding(instance, index, actions.aimPose, "/user/hand/left/input/aim/pose");
	addSuggestedBinding(instance, index, actions.aimPose, "/user/hand/right/input/aim/pose");
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
	copyText("mcxrinput_d3d11_probe", actionSetInfo.actionSetName);
	copyText("MCXRInput D3D11 Probe", actionSetInfo.localizedActionSetName);
	actionSetInfo.priority = 0;

	XrResult result = xrCreateActionSet(instance, &actionSetInfo, &actions.actionSet);
	if (XR_FAILED(result)) {
		printFailure("creating controller action set", result);
		return false;
	}

	struct ActionSpec {
		XrActionType type;
		std::string_view name;
		std::string_view localizedName;
		XrAction& action;
	};

	ActionSpec specs[] = {
			{XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip pose", actions.gripPose},
			{XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim pose", actions.aimPose},
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

	for (const ActionSpec& spec : specs) {
		result = createAction(actions.actionSet, subactionPaths, spec.type,
							  spec.name, spec.localizedName, spec.action);
		if (XR_FAILED(result)) {
			printFailure(std::string{"creating action "} + std::string{spec.name}, result);
			return false;
		}
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

bool loadD3D11RequirementsFunction(
		XrInstance instance, PFN_xrGetD3D11GraphicsRequirementsKHR& getRequirements) {
	PFN_xrVoidFunction function = nullptr;
	const XrResult result = xrGetInstanceProcAddr(instance, "xrGetD3D11GraphicsRequirementsKHR", &function);
	if (XR_FAILED(result) || function == nullptr) {
		if (XR_FAILED(result)) {
			printFailure("loading D3D11 graphics requirements function", result);
		} else {
			std::cerr << "Runtime did not return xrGetD3D11GraphicsRequirementsKHR.\n";
		}
		return false;
	}

	getRequirements = reinterpret_cast<PFN_xrGetD3D11GraphicsRequirementsKHR>(function);
	return true;
}

bool sameLuid(const LUID& left, const LUID& right) {
	return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}

bool findAdapterForLuid(const LUID& requiredLuid, ComPtr<IDXGIAdapter1>& adapter) {
	ComPtr<IDXGIFactory1> factory;
	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
	if (FAILED(hr)) {
		printHresult("creating DXGI factory", hr);
		return false;
	}

	for (UINT index = 0; ; ++index) {
		ComPtr<IDXGIAdapter1> candidate;
		hr = factory->EnumAdapters1(index, candidate.GetAddressOf());
		if (hr == DXGI_ERROR_NOT_FOUND) {
			break;
		}
		if (FAILED(hr)) {
			printHresult("enumerating DXGI adapters", hr);
			return false;
		}

		DXGI_ADAPTER_DESC1 description{};
		hr = candidate->GetDesc1(&description);
		if (FAILED(hr)) {
			printHresult("querying DXGI adapter description", hr);
			return false;
		}

		if (sameLuid(description.AdapterLuid, requiredLuid)) {
			adapter = candidate;
			std::wcout << L"D3D11 adapter: " << description.Description << L'\n';
			return true;
		}
	}

	std::cerr << "Could not find the D3D11 adapter required by the OpenXR runtime.\n";
	return false;
}

bool createD3D11Device(const XrGraphicsRequirementsD3D11KHR& requirements, D3DState& d3d) {
	ComPtr<IDXGIAdapter1> adapter;
	if (!findAdapterForLuid(requirements.adapterLuid, adapter)) {
		return false;
	}

	const D3D_FEATURE_LEVEL featureLevels[] = {
			D3D_FEATURE_LEVEL_12_1,
			D3D_FEATURE_LEVEL_12_0,
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
	};

	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	HRESULT hr = D3D11CreateDevice(
			adapter.Get(),
			D3D_DRIVER_TYPE_UNKNOWN,
			nullptr,
			flags,
			featureLevels,
			static_cast<UINT>(sizeof(featureLevels) / sizeof(featureLevels[0])),
			D3D11_SDK_VERSION,
			d3d.device.GetAddressOf(),
			&d3d.featureLevel,
			d3d.context.GetAddressOf());

	if (hr == E_INVALIDARG) {
		const D3D_FEATURE_LEVEL fallbackLevels[] = {
				D3D_FEATURE_LEVEL_12_0,
				D3D_FEATURE_LEVEL_11_0,
				D3D_FEATURE_LEVEL_10_1,
				D3D_FEATURE_LEVEL_10_0,
		};
		hr = D3D11CreateDevice(
				adapter.Get(),
				D3D_DRIVER_TYPE_UNKNOWN,
				nullptr,
				flags,
				fallbackLevels,
				static_cast<UINT>(sizeof(fallbackLevels) / sizeof(fallbackLevels[0])),
				D3D11_SDK_VERSION,
				d3d.device.GetAddressOf(),
				&d3d.featureLevel,
				d3d.context.GetAddressOf());
	}

	if (FAILED(hr)) {
		printHresult("creating D3D11 device", hr);
		return false;
	}

	if (d3d.featureLevel < requirements.minFeatureLevel) {
		std::cerr << "D3D11 device feature level is below OpenXR runtime requirement.\n";
		return false;
	}

	std::cout << "D3D11 device feature level: 0x" << std::hex << d3d.featureLevel << std::dec << '\n';
	return true;
}

bool enumerateViewConfiguration(
		XrInstance instance, XrSystemId systemId, std::vector<XrViewConfigurationView>& viewConfig,
		std::vector<XrView>& views) {
	std::uint32_t viewCount = 0;
	XrResult result = xrEnumerateViewConfigurationViews(
			instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
	if (XR_FAILED(result) || viewCount == 0) {
		if (XR_FAILED(result)) {
			printFailure("enumerating view configuration", result);
		} else {
			std::cerr << "Runtime reported no stereo views.\n";
		}
		return false;
	}

	viewConfig.assign(viewCount, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
	result = xrEnumerateViewConfigurationViews(
			instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
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
	result = xrEnumerateEnvironmentBlendModes(
			instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count, modes.data());
	if (XR_FAILED(result)) {
		return XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	}

	auto opaque = std::find(modes.begin(), modes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
	return opaque != modes.end() ? XR_ENVIRONMENT_BLEND_MODE_OPAQUE : modes.front();
}

bool chooseSwapchainFormat(XrSession session, DXGI_FORMAT& format) {
	std::uint32_t count = 0;
	XrResult result = xrEnumerateSwapchainFormats(session, 0, &count, nullptr);
	if (XR_FAILED(result) || count == 0) {
		if (XR_FAILED(result)) {
			printFailure("enumerating swapchain formats", result);
		} else {
			std::cerr << "Runtime reported no swapchain formats.\n";
		}
		return false;
	}

	std::vector<std::int64_t> formats(count);
	result = xrEnumerateSwapchainFormats(session, count, &count, formats.data());
	if (XR_FAILED(result)) {
		printFailure("querying swapchain formats", result);
		return false;
	}

	const DXGI_FORMAT preferred[] = {
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_FORMAT_B8G8R8A8_UNORM,
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
			DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
	};

	for (DXGI_FORMAT candidate : preferred) {
		if (std::find(formats.begin(), formats.end(), static_cast<std::int64_t>(candidate)) != formats.end()) {
			format = candidate;
			std::cout << "Swapchain format: " << static_cast<int>(format) << '\n';
			return true;
		}
	}

	std::cerr << "No preferred D3D11 color swapchain format was available.\n";
	return false;
}

bool createSwapchainBundle(
		XrSession session, ID3D11Device* device, const XrViewConfigurationView& config,
		DXGI_FORMAT format, SwapchainBundle& bundle) {
	bundle.width = config.recommendedImageRectWidth;
	bundle.height = config.recommendedImageRectHeight;

	XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
	createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	createInfo.format = static_cast<std::int64_t>(format);
	createInfo.sampleCount = config.recommendedSwapchainSampleCount;
	createInfo.width = bundle.width;
	createInfo.height = bundle.height;
	createInfo.faceCount = 1;
	createInfo.arraySize = 1;
	createInfo.mipCount = 1;

	XrResult result = xrCreateSwapchain(session, &createInfo, &bundle.swapchain);
	if (XR_FAILED(result)) {
		printFailure("creating color swapchain", result);
		return false;
	}

	std::uint32_t imageCount = 0;
	result = xrEnumerateSwapchainImages(bundle.swapchain, 0, &imageCount, nullptr);
	if (XR_FAILED(result) || imageCount == 0) {
		if (XR_FAILED(result)) {
			printFailure("counting swapchain images", result);
		} else {
			std::cerr << "Swapchain has no images.\n";
		}
		return false;
	}

	bundle.images.assign(imageCount, XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
	result = xrEnumerateSwapchainImages(
			bundle.swapchain, imageCount, &imageCount,
			reinterpret_cast<XrSwapchainImageBaseHeader*>(bundle.images.data()));
	if (XR_FAILED(result)) {
		printFailure("enumerating D3D11 swapchain images", result);
		return false;
	}

	bundle.renderTargets.resize(imageCount);
	for (std::uint32_t index = 0; index < imageCount; ++index) {
		D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
		viewDesc.Format = format;
		viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		viewDesc.Texture2D.MipSlice = 0;

		HRESULT hr = device->CreateRenderTargetView(
				bundle.images[index].texture, &viewDesc, bundle.renderTargets[index].GetAddressOf());
		if (FAILED(hr)) {
			printHresult("creating swapchain render target view", hr);
			return false;
		}
	}

	return true;
}

void destroySwapchains(std::vector<SwapchainBundle>& bundles) {
	for (SwapchainBundle& bundle : bundles) {
		bundle.renderTargets.clear();
		bundle.images.clear();
		if (bundle.swapchain != XR_NULL_HANDLE) {
			xrDestroySwapchain(bundle.swapchain);
			bundle.swapchain = XR_NULL_HANDLE;
		}
	}
}

bool clearSwapchainImage(XrSwapchain swapchain, ID3D11DeviceContext* context,
						 ID3D11RenderTargetView* renderTarget) {
	XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
	std::uint32_t imageIndex = 0;
	XrResult result = xrAcquireSwapchainImage(swapchain, &acquireInfo, &imageIndex);
	if (XR_FAILED(result)) {
		printFailure("acquiring swapchain image", result);
		return false;
	}

	XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
	waitInfo.timeout = XR_INFINITE_DURATION;
	result = xrWaitSwapchainImage(swapchain, &waitInfo);
	if (XR_FAILED(result)) {
		printFailure("waiting for swapchain image", result);
		XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
		xrReleaseSwapchainImage(swapchain, &releaseInfo);
		return false;
	}

	const float clearColor[] = {0.0F, 0.0F, 0.0F, 1.0F};
	context->ClearRenderTargetView(renderTarget, clearColor);
	context->Flush();

	XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
	result = xrReleaseSwapchainImage(swapchain, &releaseInfo);
	if (XR_FAILED(result)) {
		printFailure("releasing swapchain image", result);
		return false;
	}

	return true;
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

BooleanActionReport readBooleanAction(XrSession session, XrAction action, XrPath handPath) {
	XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
	getInfo.action = action;
	getInfo.subactionPath = handPath;

	XrActionStateBoolean actionState{XR_TYPE_ACTION_STATE_BOOLEAN};
	const XrResult result = xrGetActionStateBoolean(session, &getInfo, &actionState);
	if (XR_FAILED(result) || actionState.isActive != XR_TRUE) {
		return {};
	}
	return BooleanActionReport{true, actionState.currentState == XR_TRUE};
}

std::string currentInteractionProfile(XrInstance instance, XrSession session, XrPath handPath) {
	XrInteractionProfileState state{XR_TYPE_INTERACTION_PROFILE_STATE};
	const XrResult result = xrGetCurrentInteractionProfile(session, handPath, &state);
	if (XR_FAILED(result)) {
		return "(profile query failed)";
	}
	return pathToString(instance, state.interactionProfile);
}

float magnitude(const XrVector2f& vector) {
	return std::sqrt(vector.x * vector.x + vector.y * vector.y);
}

void updateFloatWindow(FloatWindow& window, const FloatActionReport& report) {
	window.current = report;
	if (!report.active) {
		return;
	}
	window.activeSeen = true;
	window.peak = std::max(window.peak, std::fabs(report.value));
}

void updateVector2Window(Vector2Window& window, const Vector2ActionReport& report) {
	window.current = report;
	if (!report.active) {
		return;
	}
	window.activeSeen = true;
	window.peakMagnitude = std::max(window.peakMagnitude, magnitude(report.value));
	window.maxAbsX = std::max(window.maxAbsX, std::fabs(report.value.x));
	window.maxAbsY = std::max(window.maxAbsY, std::fabs(report.value.y));
}

void updateBooleanWindow(BooleanWindow& window, const BooleanActionReport& report) {
	window.current = report;
	if (!report.active) {
		return;
	}
	window.activeSeen = true;
	window.pressed = window.pressed || report.value;
}

void resetFloatWindow(FloatWindow& window) {
	window.activeSeen = window.current.active;
	window.peak = window.current.active ? std::fabs(window.current.value) : 0.0F;
}

void resetVector2Window(Vector2Window& window) {
	window.activeSeen = window.current.active;
	window.peakMagnitude = window.current.active ? magnitude(window.current.value) : 0.0F;
	window.maxAbsX = window.current.active ? std::fabs(window.current.value.x) : 0.0F;
	window.maxAbsY = window.current.active ? std::fabs(window.current.value.y) : 0.0F;
}

void resetBooleanWindow(BooleanWindow& window) {
	window.activeSeen = window.current.active;
	window.pressed = window.current.active && window.current.value;
}

void resetInputWindow(HandInputWindow& window) {
	resetFloatWindow(window.trigger);
	resetFloatWindow(window.squeeze);
	resetVector2Window(window.stick);
	resetBooleanWindow(window.stickClick);
	resetBooleanWindow(window.a);
	resetBooleanWindow(window.b);
	resetBooleanWindow(window.x);
	resetBooleanWindow(window.y);
	resetBooleanWindow(window.menu);
}

void updateInputWindow(XrInstance instance, XrSession session, const ActionState& actions,
					   const HandState& hand, XrTime displayTime, XrSpace localSpace,
					   HandInputWindow& window) {
	window.profile = currentInteractionProfile(instance, session, hand.userPath);
	window.grip = locatePoseAction(session, hand.gripSpace, localSpace, displayTime,
								   actions.gripPose, hand.userPath);
	window.aim = locatePoseAction(session, hand.aimSpace, localSpace, displayTime,
								  actions.aimPose, hand.userPath);
	updateFloatWindow(window.trigger, readFloatAction(session, actions.triggerValue, hand.userPath));
	updateFloatWindow(window.squeeze, readFloatAction(session, actions.squeezeValue, hand.userPath));
	updateVector2Window(window.stick, readVector2Action(session, actions.thumbstick, hand.userPath));
	updateBooleanWindow(window.stickClick, readBooleanAction(session, actions.thumbstickClick, hand.userPath));
	updateBooleanWindow(window.a, readBooleanAction(session, actions.aClick, hand.userPath));
	updateBooleanWindow(window.b, readBooleanAction(session, actions.bClick, hand.userPath));
	updateBooleanWindow(window.x, readBooleanAction(session, actions.xClick, hand.userPath));
	updateBooleanWindow(window.y, readBooleanAction(session, actions.yClick, hand.userPath));
	updateBooleanWindow(window.menu, readBooleanAction(session, actions.menuClick, hand.userPath));
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

void printFloatWindow(std::string_view label, const FloatWindow& window) {
	std::cout << label << '=';
	if (!window.activeSeen) {
		std::cout << "inactive";
		return;
	}
	if (window.current.active) {
		std::cout << window.current.value;
	} else {
		std::cout << "inactive";
	}
	std::cout << " peak=" << window.peak;
}

void printVector2Window(std::string_view label, const Vector2Window& window) {
	std::cout << label << '=';
	if (!window.activeSeen) {
		std::cout << "inactive";
		return;
	}
	if (window.current.active) {
		std::cout << '(' << window.current.value.x << ' ' << window.current.value.y << ')';
	} else {
		std::cout << "inactive";
	}
	std::cout << " peakMag=" << window.peakMagnitude
			  << " maxAbs=(" << window.maxAbsX << ' ' << window.maxAbsY << ')';
}

void printBooleanWindow(std::string_view label, const BooleanWindow& window) {
	std::cout << label << '=';
	if (!window.activeSeen) {
		std::cout << "inactive";
		return;
	}
	if (window.current.active) {
		std::cout << (window.current.value ? "down" : "up");
	} else {
		std::cout << "inactive";
	}
	if (window.pressed) {
		std::cout << '*';
	}
}

void printStatus(const std::array<HandState, 2>& hands,
				 const std::array<HandInputWindow, 2>& inputWindows, XrViewState viewState,
				 const std::vector<XrView>& views, XrTime displayTime, XrSpace localSpace,
				 double elapsedSeconds) {
	(void) displayTime;
	(void) localSpace;
	std::cout << "\n[" << elapsedSeconds << "s] HMD tracking="
			  << ((viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0 ? "orientation" : "no-orientation")
			  << '/'
			  << ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0 ? "position" : "no-position");
	if (!views.empty()) {
		std::cout << " position=(";
		printVector(views[0].pose.position);
		std::cout << ") rotation=(";
		printQuaternion(views[0].pose.orientation);
		std::cout << ')';
	}
	std::cout << '\n';

	for (std::size_t index = 0; index < hands.size(); ++index) {
		const HandState& hand = hands[index];
		const HandInputWindow& input = inputWindows[index];
		std::cout << "  " << hand.label
				  << " profile=" << input.profile << " ";
		printPoseReport("grip", input.grip);
		std::cout << ' ';
		printPoseReport("aim", input.aim);
		std::cout << ' ';
		printFloatWindow("trigger", input.trigger);
		std::cout << ' ';
		printFloatWindow("squeeze", input.squeeze);
		std::cout << ' ';
		printVector2Window("stick", input.stick);
		std::cout << ' ';
		printBooleanWindow("stickClick", input.stickClick);
		std::cout << ' ';
		printBooleanWindow("A", input.a);
		std::cout << ' ';
		printBooleanWindow("B", input.b);
		std::cout << ' ';
		printBooleanWindow("X", input.x);
		std::cout << ' ';
		printBooleanWindow("Y", input.y);
		std::cout << ' ';
		printBooleanWindow("menu", input.menu);
		std::cout << '\n';
	}
}

bool pollEvents(XrInstance instance, XrSession session, bool& sessionRunning,
				XrSessionState& sessionState, bool& shouldExit) {
	XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
	XrResult result = xrPollEvent(instance, &event);
	while (result == XR_SUCCESS) {
		switch (event.type) {
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
			const auto& stateEvent = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
			sessionState = stateEvent.state;
			std::cout << "Session state: " << sessionStateName(sessionState) << '\n';

			if (sessionState == XR_SESSION_STATE_READY && !sessionRunning) {
				XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
				beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				const XrResult beginResult = xrBeginSession(session, &beginInfo);
				if (XR_FAILED(beginResult)) {
					printFailure("beginning OpenXR session", beginResult);
					return false;
				}
				sessionRunning = true;
				std::cout << "OpenXR D3D11 session running. The headset may show a blank/dark app.\n";
			} else if (sessionState == XR_SESSION_STATE_STOPPING) {
				if (sessionRunning) {
					const XrResult endResult = xrEndSession(session);
					if (XR_FAILED(endResult)) {
						printFailure("ending OpenXR session", endResult);
						return false;
					}
					sessionRunning = false;
				}
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

bool locateViews(XrSession session, XrSpace localSpace, XrTime displayTime,
				 std::vector<XrView>& views, XrViewState& viewState) {
	for (XrView& view : views) {
		view = XrView{XR_TYPE_VIEW};
	}

	XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
	locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locateInfo.displayTime = displayTime;
	locateInfo.space = localSpace;

	std::uint32_t outputCount = 0;
	const XrResult result = xrLocateViews(session, &locateInfo, &viewState,
										  static_cast<std::uint32_t>(views.size()), &outputCount, views.data());
	if (XR_FAILED(result)) {
		printFailure("locating HMD views", result);
		return false;
	}

	if (outputCount != views.size()) {
		std::cerr << "Runtime returned an unexpected HMD view count.\n";
		return false;
	}

	return true;
}

} // namespace

int main() {
	std::cout << "MCXRInput OpenXR D3D11 focused input probe\n";
	std::cout << std::fixed << std::setprecision(3);

	std::vector<XrExtensionProperties> extensions;
	XrResult result = enumerateExtensions(extensions);
	if (XR_FAILED(result)) {
		printFailure("loading the active OpenXR runtime", result);
		std::cerr << "Check that SteamVR is installed, running, and selected as the OpenXR runtime.\n";
		return static_cast<int>(ExitCode::runtimeUnavailable);
	}

	if (!hasExtension(extensions, XR_KHR_D3D11_ENABLE_EXTENSION_NAME)) {
		std::cerr << "SteamVR did not advertise " << XR_KHR_D3D11_ENABLE_EXTENSION_NAME << ".\n";
		return static_cast<int>(ExitCode::d3dSetupFailed);
	}

	XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	copyText("MCXRInput", createInfo.applicationInfo.applicationName);
	createInfo.applicationInfo.applicationVersion = 1;
	copyText("MCXRInput native bridge", createInfo.applicationInfo.engineName);
	createInfo.applicationInfo.engineVersion = 1;
	createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);

	const char* enabledExtensions[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
	createInfo.enabledExtensionCount = 1;
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
	ActionState actions{};
	std::array<HandState, 2> hands{{
			HandState{"left", "/user/hand/left"},
			HandState{"right", "/user/hand/right"},
	}};
	std::array<HandInputWindow, 2> inputWindows;
	std::vector<SwapchainBundle> swapchains;

	auto cleanup = [&]() {
		destroySwapchains(swapchains);
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

	PFN_xrGetD3D11GraphicsRequirementsKHR getD3D11Requirements = nullptr;
	if (!loadD3D11RequirementsFunction(instance, getD3D11Requirements)) {
		cleanup();
		return static_cast<int>(ExitCode::d3dSetupFailed);
	}

	XrGraphicsRequirementsD3D11KHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
	result = getD3D11Requirements(instance, systemId, &graphicsRequirements);
	if (XR_FAILED(result)) {
		printFailure("querying D3D11 graphics requirements", result);
		cleanup();
		return static_cast<int>(ExitCode::d3dSetupFailed);
	}

	D3DState d3d;
	if (!createD3D11Device(graphicsRequirements, d3d)) {
		cleanup();
		return static_cast<int>(ExitCode::d3dSetupFailed);
	}

	XrGraphicsBindingD3D11KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
	graphicsBinding.device = d3d.device.Get();

	XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionInfo.next = &graphicsBinding;
	sessionInfo.systemId = systemId;
	result = xrCreateSession(instance, &sessionInfo, &session);
	if (XR_FAILED(result)) {
		printFailure("creating D3D11 OpenXR session", result);
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

	if (!attachActionsAndSpaces(session, actions, hands)) {
		cleanup();
		return static_cast<int>(ExitCode::spaceOrActionSetupFailed);
	}

	std::vector<XrViewConfigurationView> viewConfig;
	std::vector<XrView> views;
	if (!enumerateViewConfiguration(instance, systemId, viewConfig, views)) {
		cleanup();
		return static_cast<int>(ExitCode::swapchainSetupFailed);
	}

	const XrEnvironmentBlendMode environmentBlendMode = chooseEnvironmentBlendMode(instance, systemId);

	DXGI_FORMAT swapchainFormat = DXGI_FORMAT_UNKNOWN;
	if (!chooseSwapchainFormat(session, swapchainFormat)) {
		cleanup();
		return static_cast<int>(ExitCode::swapchainSetupFailed);
	}

	swapchains.resize(viewConfig.size());
	for (std::size_t index = 0; index < viewConfig.size(); ++index) {
		if (!createSwapchainBundle(session, d3d.device.Get(), viewConfig[index], swapchainFormat, swapchains[index])) {
			cleanup();
			return static_cast<int>(ExitCode::swapchainSetupFailed);
		}
	}

	std::cout << "Running for " << probeDuration.count()
			  << " seconds. The headset may show a blank dark MCXRInput app.\n";
	std::cout << "Move the headset and press controller trigger/grip/buttons/sticks.\n";
	std::cout << "Buttons marked with * were pressed at least once since the previous print.\n";
	std::cout << "Trigger/squeeze peak and stick peakMag/maxAbs summarize movement since the previous print.\n";

	bool sessionRunning = false;
	bool shouldExit = false;
	bool sawValidHmdPose = false;
	bool requestedExit = false;
	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
	const Clock::time_point start = Clock::now();
	Clock::time_point lastStatus = start - statusInterval;

	while (!shouldExit && Clock::now() - start < probeDuration) {
		if (!pollEvents(instance, session, sessionRunning, sessionState, shouldExit)) {
			cleanup();
			return static_cast<int>(ExitCode::runtimeNotReady);
		}

		if (!sessionRunning) {
			std::this_thread::sleep_for(std::chrono::milliseconds{25});
			continue;
		}

		XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
		XrFrameState frameState{XR_TYPE_FRAME_STATE};
		result = xrWaitFrame(session, &waitInfo, &frameState);
		if (XR_FAILED(result)) {
			printFailure("waiting for OpenXR frame", result);
			cleanup();
			return static_cast<int>(ExitCode::runtimeNotReady);
		}

		XrFrameBeginInfo beginFrameInfo{XR_TYPE_FRAME_BEGIN_INFO};
		result = xrBeginFrame(session, &beginFrameInfo);
		if (XR_FAILED(result)) {
			printFailure("beginning OpenXR frame", result);
			cleanup();
			return static_cast<int>(ExitCode::runtimeNotReady);
		}

		XrViewState viewState{XR_TYPE_VIEW_STATE};
		const bool viewsLocated = locateViews(session, localSpace, frameState.predictedDisplayTime, views, viewState);
		sawValidHmdPose = sawValidHmdPose ||
				((viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0);

		XrActiveActionSet activeActionSet{};
		activeActionSet.actionSet = actions.actionSet;
		activeActionSet.subactionPath = XR_NULL_PATH;
		XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
		syncInfo.countActiveActionSets = 1;
		syncInfo.activeActionSets = &activeActionSet;
		result = xrSyncActions(session, &syncInfo);
		if (XR_FAILED(result)) {
			printFailure("syncing controller actions", result);
		} else {
			for (std::size_t index = 0; index < hands.size(); ++index) {
				updateInputWindow(instance, session, actions, hands[index],
								  frameState.predictedDisplayTime, localSpace, inputWindows[index]);
			}
		}

		std::vector<XrCompositionLayerProjectionView> projectionViews(views.size());
		bool layerReady = frameState.shouldRender == XR_TRUE && viewsLocated && views.size() == swapchains.size();
		if (layerReady) {
			for (std::size_t index = 0; index < views.size(); ++index) {
				SwapchainBundle& swapchain = swapchains[index];
				XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
				std::uint32_t imageIndex = 0;
				result = xrAcquireSwapchainImage(swapchain.swapchain, &acquireInfo, &imageIndex);
				if (XR_FAILED(result)) {
					printFailure("acquiring swapchain image", result);
					layerReady = false;
					break;
				}

				XrSwapchainImageWaitInfo waitSwapchainInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
				waitSwapchainInfo.timeout = XR_INFINITE_DURATION;
				result = xrWaitSwapchainImage(swapchain.swapchain, &waitSwapchainInfo);
				if (XR_FAILED(result)) {
					printFailure("waiting for swapchain image", result);
					XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
					xrReleaseSwapchainImage(swapchain.swapchain, &releaseInfo);
					layerReady = false;
					break;
				}

				const float clearColor[] = {0.0F, 0.0F, 0.0F, 1.0F};
				d3d.context->ClearRenderTargetView(swapchain.renderTargets[imageIndex].Get(), clearColor);

				XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
				result = xrReleaseSwapchainImage(swapchain.swapchain, &releaseInfo);
				if (XR_FAILED(result)) {
					printFailure("releasing swapchain image", result);
					layerReady = false;
					break;
				}

				XrCompositionLayerProjectionView projectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
				projectionView.pose = views[index].pose;
				projectionView.fov = views[index].fov;
				projectionView.subImage.swapchain = swapchain.swapchain;
				projectionView.subImage.imageRect.offset = {0, 0};
				projectionView.subImage.imageRect.extent = {
						static_cast<std::int32_t>(swapchain.width),
						static_cast<std::int32_t>(swapchain.height),
				};
				projectionView.subImage.imageArrayIndex = 0;
				projectionViews[index] = projectionView;
			}
			d3d.context->Flush();
		}

		XrCompositionLayerProjection projectionLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
		projectionLayer.space = localSpace;
		projectionLayer.viewCount = static_cast<std::uint32_t>(projectionViews.size());
		projectionLayer.views = projectionViews.data();
		const XrCompositionLayerBaseHeader* layers[] = {
				reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projectionLayer),
		};

		XrFrameEndInfo endFrameInfo{XR_TYPE_FRAME_END_INFO};
		endFrameInfo.displayTime = frameState.predictedDisplayTime;
		endFrameInfo.environmentBlendMode = environmentBlendMode;
		endFrameInfo.layerCount = layerReady ? 1U : 0U;
		endFrameInfo.layers = layerReady ? layers : nullptr;
		result = xrEndFrame(session, &endFrameInfo);
		if (XR_FAILED(result)) {
			printFailure("ending OpenXR frame", result);
			cleanup();
			return static_cast<int>(ExitCode::runtimeNotReady);
		}

		const Clock::time_point now = Clock::now();
		if (now - lastStatus >= statusInterval) {
			const double elapsedSeconds = std::chrono::duration<double>(now - start).count();
			printStatus(hands, inputWindows, viewState, views,
						frameState.predictedDisplayTime, localSpace, elapsedSeconds);
			for (HandInputWindow& inputWindow : inputWindows) {
				resetInputWindow(inputWindow);
			}
			lastStatus = now;
		}
	}

	if (sessionRunning && !requestedExit) {
		const XrResult exitResult = xrRequestExitSession(session);
		if (XR_FAILED(exitResult)) {
			printFailure("requesting OpenXR session exit", exitResult);
		}
		requestedExit = true;
	}

	const Clock::time_point shutdownStart = Clock::now();
	while (sessionRunning && Clock::now() - shutdownStart < std::chrono::seconds{2}) {
		if (!pollEvents(instance, session, sessionRunning, sessionState, shouldExit)) {
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{25});
	}
	if (sessionRunning) {
		std::cout << "Session did not reach STOPPING before cleanup; destroying session handles.\n";
	}

	cleanup();

	if (!sawValidHmdPose) {
		std::cerr << "No valid HMD orientation pose was observed during the D3D11 probe.\n"
				  << "Confirm SteamVR is focused/running and the headset is awake.\n";
		return static_cast<int>(ExitCode::trackingUnavailable);
	}

	std::cout << "\nD3D11 OpenXR HMD/controller input probe completed successfully.\n";
	return static_cast<int>(ExitCode::success);
}
