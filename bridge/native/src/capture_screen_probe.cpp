#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <mcxrinput/half_sbs_renderer.hpp>
#include <mcxrinput/openxr_d3d11.hpp>
#include <mcxrinput/screen_pose_math.hpp>
#include <mcxrinput/window_capture.hpp>

#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace mcxrinput::native;

namespace {

constexpr XrFormFactor formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
constexpr std::uint32_t targetEyeWidth = 1280;
constexpr auto initialCaptureTimeout = std::chrono::seconds{10};
constexpr auto firstDisplayTimeout = std::chrono::seconds{30};
constexpr auto maximumCaptureAge = std::chrono::milliseconds{500};
constexpr auto maximumLiveStarvation = std::chrono::seconds{5};
constexpr auto shutdownGrace = std::chrono::seconds{5};
constexpr auto statusInterval = std::chrono::seconds{1};

std::atomic_bool stopRequested{false};

enum class ExitCode : int {
	success = 0,
	usage = 1,
	windowSelection = 2,
	openXrRuntime = 3,
	noHeadset = 4,
	openXrSession = 5,
	capture = 6,
	invalidStereo = 7,
	rendering = 8,
	cancelledBeforeValidation = 9,
};

enum class EyeOrder {
	leftRight,
	rightLeft,
};

struct Options {
	bool help{false};
	bool listWindows{false};
	std::optional<std::filesystem::path> executable;
	std::optional<HWND> window;
	int seconds{30};
	float distanceMeters{1.5F};
	float widthMeters{1.6F};
	EyeOrder eyeOrder{EyeOrder::leftRight};
	bool secondsSeen{false};
	bool distanceSeen{false};
	bool widthSeen{false};
	bool eyeOrderSeen{false};
};

struct EyeExtent {
	std::uint32_t width{0};
	std::uint32_t height{0};
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

void printUsage() {
	std::wcout
			<< L"Usage:\n"
			<< L"  MCXRInputOpenXRCaptureScreenProbe.exe --list-windows [--executable <absolute-path>]\n"
			<< L"  MCXRInputOpenXRCaptureScreenProbe.exe (--executable <absolute-path> | --window <0xHWND>)\n"
			<< L"      [--seconds <1..300>] [--distance-m <0.25..5>] [--width-m <0.25..4>]\n"
			<< L"      [--eye-order lr|rl]\n\n"
			<< L"Displays a captured half-SBS window on roll-stabilized OpenXR quads.\n"
			<< L"This bounded diagnostic does not send UDP or generate Minecraft input.\n";
}

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

bool parseInteger(std::wstring_view text, int minimum, int maximum, int& output) {
	std::string narrow;
	if (!narrowAscii(text, narrow)) {
		return false;
	}
	int parsed = 0;
	const auto result = std::from_chars(narrow.data(), narrow.data() + narrow.size(), parsed);
	if (result.ec != std::errc{} || result.ptr != narrow.data() + narrow.size()
			|| parsed < minimum || parsed > maximum) {
		return false;
	}
	output = parsed;
	return true;
}

bool parseFloat(std::wstring_view text, float minimum, float maximum, float& output) {
	std::string narrow;
	if (!narrowAscii(text, narrow)) {
		return false;
	}
	float parsed = 0.0F;
	const auto result = std::from_chars(narrow.data(), narrow.data() + narrow.size(), parsed);
	if (result.ec != std::errc{} || result.ptr != narrow.data() + narrow.size()
			|| !std::isfinite(parsed) || parsed < minimum || parsed > maximum) {
		return false;
	}
	output = parsed;
	return true;
}

bool parseOptions(int argc, wchar_t** argv, Options& options) {
	bool executableSeen = false;
	bool windowSeen = false;
	for (int index = 1; index < argc; ++index) {
		const std::wstring_view argument{argv[index]};
		if (argument == L"--help" || argument == L"-h") {
			if (options.help) {
				std::wcerr << L"Duplicate --help option.\n";
				return false;
			}
			options.help = true;
			continue;
		}
		if (argument == L"--list-windows") {
			if (options.listWindows) {
				std::wcerr << L"Duplicate --list-windows option.\n";
				return false;
			}
			options.listWindows = true;
			continue;
		}
		if (argument == L"--executable") {
			if (executableSeen || index + 1 >= argc) {
				std::wcerr << L"Expected one --executable followed by an absolute path.\n";
				return false;
			}
			executableSeen = true;
			options.executable = std::filesystem::path{argv[++index]};
			continue;
		}
		if (argument == L"--window") {
			if (windowSeen || index + 1 >= argc) {
				std::wcerr << L"Expected one --window followed by a hexadecimal handle.\n";
				return false;
			}
			HWND parsed = nullptr;
			if (!parseWindowHandle(argv[++index], parsed)) {
				std::wcerr << L"Expected --window in hexadecimal form, for example 0x123ABC.\n";
				return false;
			}
			windowSeen = true;
			options.window = parsed;
			continue;
		}
		if (argument == L"--seconds") {
			if (options.secondsSeen || index + 1 >= argc
					|| !parseInteger(argv[++index], 1, 300, options.seconds)) {
				std::wcerr << L"Expected one --seconds value from 1 to 300.\n";
				return false;
			}
			options.secondsSeen = true;
			continue;
		}
		if (argument == L"--distance-m") {
			if (options.distanceSeen || index + 1 >= argc
					|| !parseFloat(argv[++index], 0.25F, 5.0F, options.distanceMeters)) {
				std::wcerr << L"Expected one --distance-m value from 0.25 to 5.0.\n";
				return false;
			}
			options.distanceSeen = true;
			continue;
		}
		if (argument == L"--width-m") {
			if (options.widthSeen || index + 1 >= argc
					|| !parseFloat(argv[++index], 0.25F, 4.0F, options.widthMeters)) {
				std::wcerr << L"Expected one --width-m value from 0.25 to 4.0.\n";
				return false;
			}
			options.widthSeen = true;
			continue;
		}
		if (argument == L"--eye-order") {
			if (options.eyeOrderSeen || index + 1 >= argc) {
				std::wcerr << L"Expected one --eye-order value: lr or rl.\n";
				return false;
			}
			const std::wstring_view order{argv[++index]};
			if (order == L"lr") {
				options.eyeOrder = EyeOrder::leftRight;
			} else if (order == L"rl") {
				options.eyeOrder = EyeOrder::rightLeft;
			} else {
				std::wcerr << L"Expected --eye-order to be lr or rl.\n";
				return false;
			}
			options.eyeOrderSeen = true;
			continue;
		}
		std::wcerr << L"Unknown argument: " << argument << L'\n';
		return false;
	}

	if (options.help) {
		if (argc != 2) {
			std::wcerr << L"--help cannot be combined with other options.\n";
			return false;
		}
		return true;
	}
	if (options.listWindows) {
		if (options.window || options.secondsSeen || options.distanceSeen
				|| options.widthSeen || options.eyeOrderSeen) {
			std::wcerr << L"--list-windows accepts only the optional --executable filter.\n";
			return false;
		}
	} else if (options.executable.has_value() == options.window.has_value()) {
		std::wcerr << L"Display capture requires exactly one of --executable or --window.\n";
		return false;
	}
	if (options.executable) {
		if (options.executable->empty() || !options.executable->is_absolute()) {
			std::wcerr << L"--executable must be an absolute path.\n";
			return false;
		}
		*options.executable = options.executable->lexically_normal();
	}
	return true;
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

bool pollEvents(
		XrInstance instance, XrSession session, bool& sessionRunning,
		XrSessionState& sessionState, bool& shouldExit, bool& runtimeLoss) {
	XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
	XrResult result = xrPollEvent(instance, &event);
	while (result == XR_SUCCESS) {
		if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
			const auto& changed = reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
			sessionState = changed.state;
			std::cout << "Session state: " << sessionStateName(sessionState) << '\n';
			if (sessionState == XR_SESSION_STATE_READY) {
				XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
				beginInfo.primaryViewConfigurationType = primaryStereoViewConfiguration;
				const XrResult beginResult = xrBeginSession(session, &beginInfo);
				if (XR_FAILED(beginResult)) {
					printFailure("beginning capture-screen session", beginResult);
					return false;
				}
				sessionRunning = true;
			} else if (sessionState == XR_SESSION_STATE_STOPPING) {
				if (sessionRunning) {
					const XrResult endResult = xrEndSession(session);
					sessionRunning = false;
					if (XR_FAILED(endResult)) {
						printFailure("ending capture-screen session", endResult);
						return false;
					}
				}
				shouldExit = true;
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
		printFailure("polling capture-screen events", result);
		return false;
	}
	return true;
}

bool createReferenceSpace(XrSession session, XrReferenceSpaceType type, XrSpace& space) {
	XrReferenceSpaceCreateInfo info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	info.referenceSpaceType = type;
	info.poseInReferenceSpace.orientation.w = 1.0F;
	const XrResult result = xrCreateReferenceSpace(session, &info, &space);
	if (XR_FAILED(result)) {
		printFailure(type == XR_REFERENCE_SPACE_TYPE_LOCAL
				? "creating LOCAL reference space" : "creating VIEW reference space", result);
		return false;
	}
	return true;
}

bool chooseDisplayFormat(XrSession session, std::int64_t& format) {
	std::uint32_t count = 0;
	XrResult result = xrEnumerateSwapchainFormats(session, 0, &count, nullptr);
	if (XR_FAILED(result) || count == 0) {
		if (XR_FAILED(result)) {
			printFailure("enumerating capture-screen swapchain formats", result);
		} else {
			std::cerr << "OpenXR runtime reported no swapchain formats.\n";
		}
		return false;
	}
	std::vector<std::int64_t> formats(count);
	result = xrEnumerateSwapchainFormats(session, count, &count, formats.data());
	if (XR_FAILED(result)) {
		printFailure("enumerating capture-screen swapchain formats", result);
		return false;
	}
	formats.resize(count);
	// Desktop capture is sRGB-encoded. Prefer an sRGB render target so the
	// shader samples into linear space and D3D encodes correctly on output.
	const std::array<DXGI_FORMAT, 4> preferred{
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
			DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_FORMAT_B8G8R8A8_UNORM,
	};
	for (DXGI_FORMAT candidate : preferred) {
		const std::int64_t encoded = static_cast<std::int64_t>(candidate);
		if (std::find(formats.begin(), formats.end(), encoded) != formats.end()) {
			format = encoded;
			std::cout << "Capture-screen swapchain format: " << format
					  << ((candidate == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
							  || candidate == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) ? " (sRGB)\n" : " (linear fallback)\n");
			return true;
		}
	}
	std::cerr << "No supported RGBA8/BGRA8 capture-screen swapchain format is available.\n";
	return false;
}

EyeExtent chooseEyeExtent(
		std::uint32_t sourceWidth, std::uint32_t sourceHeight,
		const XrSystemGraphicsProperties& limits) {
	const double aspect = static_cast<double>(sourceWidth) / sourceHeight;
	std::uint32_t width = std::min(targetEyeWidth, limits.maxSwapchainImageWidth);
	std::uint32_t height = static_cast<std::uint32_t>(std::max(1.0, std::round(width / aspect)));
	if (height > limits.maxSwapchainImageHeight) {
		height = limits.maxSwapchainImageHeight;
		width = static_cast<std::uint32_t>(std::max(1.0, std::round(height * aspect)));
		width = std::min(width, limits.maxSwapchainImageWidth);
	}
	return EyeExtent{width, height};
}

bool waitForGpuIdle(ID3D11Device* device, ID3D11DeviceContext* context) {
	if (device == nullptr || context == nullptr) {
		return true;
	}
	D3D11_QUERY_DESC description{};
	description.Query = D3D11_QUERY_EVENT;
	ComPtr<ID3D11Query> query;
	const HRESULT createResult = device->CreateQuery(&description, &query);
	if (FAILED(createResult)) {
		printHresult("creating capture-screen completion query", createResult);
		return false;
	}
	context->End(query.Get());
	context->Flush();
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
	while (std::chrono::steady_clock::now() < deadline) {
		BOOL complete = FALSE;
		const HRESULT result = context->GetData(query.Get(), &complete, sizeof(complete), 0);
		if (result == S_OK && complete) {
			return true;
		}
		if (FAILED(result)) {
			printHresult("waiting for capture-screen D3D11 work", result);
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{1});
	}
	std::cerr << "Timed out waiting for capture-screen D3D11 work.\n";
	return false;
}

XrPosef toXrPose(const Pose& pose) {
	XrPosef result{};
	result.orientation = XrQuaternionf{
			pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w};
	result.position = XrVector3f{pose.position.x, pose.position.y, pose.position.z};
	return result;
}

void configureQuad(
		XrCompositionLayerQuad& layer, XrEyeVisibility eye,
		XrSpace localSpace, XrSwapchain swapchain, const XrPosef& pose,
		float widthMeters, float heightMeters, const EyeExtent& extent) {
	layer = XrCompositionLayerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
	layer.space = localSpace;
	layer.eyeVisibility = eye;
	layer.subImage.swapchain = swapchain;
	layer.subImage.imageRect.extent = XrExtent2Di{
			static_cast<std::int32_t>(extent.width), static_cast<std::int32_t>(extent.height)};
	layer.pose = pose;
	layer.size = XrExtent2Df{widthMeters, heightMeters};
}

std::optional<ExitCode> waitForInitialCapture(
		WindowCaptureSource& capture, ID3D11DeviceContext* context) {
	const auto deadline = std::chrono::steady_clock::now() + initialCaptureTimeout;
	bool invalidFrameObserved = false;
	while (!stopRequested.load() && std::chrono::steady_clock::now() < deadline) {
		const WindowCaptureUpdate update = capture.poll(context);
		if (update == WindowCaptureUpdate::frameReady && capture.hasFreshFrame(maximumCaptureAge)) {
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
		return ExitCode::cancelledBeforeValidation;
	}
	std::wcerr << L"No fresh half-SBS capture frame arrived within ten seconds.\n";
	return invalidFrameObserved ? ExitCode::invalidStereo : ExitCode::capture;
}

ExitCode runCaptureScreen(const Options& options, const WindowCandidate& selected) {
	XrInstance instance = XR_NULL_HANDLE;
	XrSession session = XR_NULL_HANDLE;
	XrSpace localSpace = XR_NULL_HANDLE;
	XrSpace viewSpace = XR_NULL_HANDLE;
	D3DState d3d;
	WindowCaptureSource capture;
	HalfSbsRenderer renderer;
	std::array<SwapchainBundle, 2> eyeSwapchains;

	auto cleanup = [&]() -> bool {
		bool gpuDrained = true;
		capture.stop();
		if (d3d.context != nullptr) {
			gpuDrained = waitForGpuIdle(d3d.device.Get(), d3d.context.Get());
			d3d.context->ClearState();
			d3d.context->Flush();
		}
		renderer.reset();
		for (SwapchainBundle& swapchain : eyeSwapchains) {
			swapchain.reset();
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
		if (instance != XR_NULL_HANDLE) {
			xrDestroyInstance(instance);
			instance = XR_NULL_HANDLE;
		}
		return gpuDrained;
	};

	std::vector<XrExtensionProperties> extensions;
	if (!enumerateInstanceExtensions(extensions)
			|| !hasExtension(extensions, XR_KHR_D3D11_ENABLE_EXTENSION_NAME)) {
		std::cerr << "The active OpenXR runtime does not provide D3D11 support.\n";
		cleanup();
		return ExitCode::openXrRuntime;
	}

	XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	copyText("MCXRInput", instanceInfo.applicationInfo.applicationName);
	instanceInfo.applicationInfo.applicationVersion = 1;
	copyText("MCXRInput capture screen probe", instanceInfo.applicationInfo.engineName);
	instanceInfo.applicationInfo.engineVersion = 1;
	instanceInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	const char* enabledExtensions[]{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
	instanceInfo.enabledExtensionCount = 1;
	instanceInfo.enabledExtensionNames = enabledExtensions;
	XrResult result = xrCreateInstance(&instanceInfo, &instance);
	if (XR_FAILED(result)) {
		printFailure("creating capture-screen OpenXR instance", result);
		cleanup();
		return ExitCode::openXrRuntime;
	}

	XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
	systemInfo.formFactor = formFactor;
	XrSystemId systemId = XR_NULL_SYSTEM_ID;
	result = xrGetSystem(instance, &systemInfo, &systemId);
	if (XR_FAILED(result)) {
		printFailure("requesting an HMD for capture display", result);
		cleanup();
		return ExitCode::noHeadset;
	}
	XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
	result = xrGetSystemProperties(instance, systemId, &systemProperties);
	if (XR_FAILED(result) || systemProperties.graphicsProperties.maxLayerCount < 2) {
		if (XR_FAILED(result)) {
			printFailure("querying capture-display layer limits", result);
		} else {
			std::cerr << "Capture display requires two OpenXR composition layers.\n";
		}
		cleanup();
		return ExitCode::openXrSession;
	}
	std::vector<XrViewConfigurationView> viewConfigs;
	if (!enumerateViewConfigurationViews(instance, systemId, viewConfigs)
			|| viewConfigs.size() != 2) {
		std::cerr << "Capture display requires exactly two PRIMARY_STEREO views.\n";
		cleanup();
		return ExitCode::openXrSession;
	}

	PFN_xrGetD3D11GraphicsRequirementsKHR getRequirements = nullptr;
	if (!loadD3D11RequirementsFunction(instance, getRequirements)) {
		cleanup();
		return ExitCode::openXrSession;
	}
	XrGraphicsRequirementsD3D11KHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
	result = getRequirements(instance, systemId, &graphicsRequirements);
	if (XR_FAILED(result) || !createD3D11Device(graphicsRequirements, d3d)) {
		if (XR_FAILED(result)) {
			printFailure("querying capture-display D3D11 requirements", result);
		}
		cleanup();
		return ExitCode::openXrSession;
	}

	if (!capture.start(selected.window, d3d.device.Get())) {
		std::wcerr << capture.lastError() << L'\n';
		cleanup();
		return ExitCode::capture;
	}
	if (const auto initialFailure = waitForInitialCapture(capture, d3d.context.Get())) {
		cleanup();
		return *initialFailure;
	}
	const WindowCaptureFrame& initialFrame = capture.latestFrame();
	if (!initialFrame.texture || initialFrame.combinedWidth < 2 || initialFrame.height == 0) {
		cleanup();
		return ExitCode::invalidStereo;
	}
	const std::uint32_t initialCaptureWidth = initialFrame.combinedWidth;
	const std::uint32_t initialCaptureHeight = initialFrame.height;

	XrGraphicsBindingD3D11KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
	graphicsBinding.device = d3d.device.Get();
	XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionInfo.next = &graphicsBinding;
	sessionInfo.systemId = systemId;
	result = xrCreateSession(instance, &sessionInfo, &session);
	if (XR_FAILED(result)) {
		printFailure("creating capture-display OpenXR session", result);
		cleanup();
		return ExitCode::openXrSession;
	}
	if (!createReferenceSpace(session, XR_REFERENCE_SPACE_TYPE_LOCAL, localSpace)
			|| !createReferenceSpace(session, XR_REFERENCE_SPACE_TYPE_VIEW, viewSpace)) {
		cleanup();
		return ExitCode::openXrSession;
	}
	XrEnvironmentBlendMode blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	std::int64_t swapchainFormat = 0;
	if (!chooseEnvironmentBlendMode(instance, systemId, blendMode)
			|| !chooseDisplayFormat(session, swapchainFormat)) {
		cleanup();
		return ExitCode::rendering;
	}

	const EyeExtent eyeExtent = chooseEyeExtent(
			initialCaptureWidth, initialCaptureHeight,
			systemProperties.graphicsProperties);
	if (eyeExtent.width == 0 || eyeExtent.height == 0) {
		std::cerr << "OpenXR runtime cannot provide a usable capture-display swapchain size.\n";
		cleanup();
		return ExitCode::rendering;
	}
	for (SwapchainBundle& swapchain : eyeSwapchains) {
		if (!createColorSwapchain(
				session, d3d.device.Get(),
				ColorSwapchainDescription{
						eyeExtent.width, eyeExtent.height, swapchainFormat, 1, 1,
						XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT},
				swapchain)) {
			cleanup();
			return ExitCode::rendering;
		}
	}
	if (!renderer.initialize(d3d.device.Get())) {
		cleanup();
		return ExitCode::rendering;
	}

	const float initialAspect = static_cast<float>(initialCaptureWidth)
			/ static_cast<float>(initialCaptureHeight);
	const float heightMeters = options.widthMeters / initialAspect;
	std::cout << std::fixed << std::setprecision(3)
			  << "Initial half-SBS capture: " << initialCaptureWidth << 'x'
			  << initialCaptureHeight << "; decoded eyes -> " << eyeExtent.width << 'x'
			  << eyeExtent.height << '\n'
			  << "Virtual screen: " << options.widthMeters << 'x' << heightMeters
			  << " m at " << options.distanceMeters << " m\n"
			  << "Eye routing: "
			  << (options.eyeOrder == EyeOrder::leftRight ? "source L->left, R->right" : "source R->left, L->right")
			  << "\nThis probe owns OpenXR focus; do not run MCXRInputOpenXRBridge.exe beside it.\n"
			  << "Press Ctrl+C to stop early.\n";

	bool sessionRunning = false;
	bool shouldExit = false;
	bool runtimeLoss = false;
	bool exitRequested = false;
	bool stopInitiated = false;
	bool userCancelled = false;
	bool cancelledBeforeValidation = false;
	bool shutdownFailed = false;
	bool invalidCaptureReported = false;
	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
	std::optional<ExitCode> terminalFailure;
	RollFreeBasisState basisState;
	std::uint64_t submittedFrames = 0;
	std::uint64_t staleFrames = 0;
	std::uint64_t invalidPoseFrames = 0;
	const auto loopStarted = std::chrono::steady_clock::now();
	auto lastStatus = loopStarted;
	std::chrono::steady_clock::time_point firstSubmittedAt{};
	std::chrono::steady_clock::time_point lastSubmittedAt{};
	std::chrono::steady_clock::time_point captureUnavailableSince{};
	std::chrono::steady_clock::time_point stopStartedAt{};
	std::chrono::steady_clock::time_point nextExitAttempt{};

	while (!shouldExit) {
		if (!pollEvents(instance, session, sessionRunning, sessionState, shouldExit, runtimeLoss)) {
			terminalFailure = ExitCode::openXrSession;
			break;
		}
		if (shouldExit) {
			break;
		}

		if (!terminalFailure) {
			const WindowCaptureUpdate update = capture.poll(d3d.context.Get());
			switch (update) {
			case WindowCaptureUpdate::resized:
				std::cout << "Capture resized; waiting for a fresh exact-sized frame.\n";
				break;
			case WindowCaptureUpdate::minimized:
				std::cout << "Capture blanked while the Minecraft window is minimized.\n";
				break;
			case WindowCaptureUpdate::restored:
				std::cout << "Minecraft window restored; waiting for a fresh frame.\n";
				break;
			case WindowCaptureUpdate::windowClosed:
				std::cerr << "The selected Minecraft window closed.\n";
				terminalFailure = ExitCode::windowSelection;
				break;
			case WindowCaptureUpdate::invalidStereoFrame:
				if (!invalidCaptureReported) {
					std::wcerr << capture.lastError() << L" Blanking until a valid frame arrives.\n";
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
		const bool captureFreshNow = capture.hasFreshFrame(maximumCaptureAge);
		if (!stopInitiated
				&& firstSubmittedAt != std::chrono::steady_clock::time_point{}) {
			if (captureFreshNow) {
				captureUnavailableSince = {};
			} else if (captureUnavailableSince == std::chrono::steady_clock::time_point{}) {
				captureUnavailableSince = now;
			} else if (now - captureUnavailableSince >= maximumLiveStarvation
					&& !terminalFailure) {
				std::cerr << "Live Minecraft capture remained unavailable for five seconds.\n";
				terminalFailure = invalidCaptureReported
						? ExitCode::invalidStereo : ExitCode::capture;
			}
		}
		const bool durationReached = firstSubmittedAt != std::chrono::steady_clock::time_point{}
				&& now - firstSubmittedAt >= std::chrono::seconds{options.seconds};
		const bool recentlySubmitted = lastSubmittedAt != std::chrono::steady_clock::time_point{}
				&& now - lastSubmittedAt <= maximumCaptureAge;
		const bool durationComplete = durationReached && captureFreshNow && recentlySubmitted;
		if (!stopInitiated && durationReached && !durationComplete
				&& now - firstSubmittedAt >= std::chrono::seconds{options.seconds}
						+ maximumLiveStarvation && !terminalFailure) {
			std::cerr << "Capture-screen output did not recover near the end of its bounded run.\n";
			terminalFailure = captureFreshNow ? ExitCode::rendering : ExitCode::capture;
		}
		if ((stopRequested.load() || durationComplete || terminalFailure.has_value()) && !stopInitiated) {
			stopInitiated = true;
			userCancelled = stopRequested.load();
			cancelledBeforeValidation = userCancelled
					&& firstSubmittedAt == std::chrono::steady_clock::time_point{};
			stopStartedAt = now;
			nextExitAttempt = now;
		}
		if (stopInitiated && !sessionRunning) {
			shouldExit = true;
		} else if (stopInitiated && now - stopStartedAt >= shutdownGrace) {
			std::cerr << "OpenXR capture-screen session did not stop within five seconds.\n";
			shutdownFailed = true;
			shouldExit = true;
		} else if (stopInitiated && !exitRequested) {
			if (sessionState == XR_SESSION_STATE_FOCUSED && now >= nextExitAttempt) {
				const XrResult exitResult = xrRequestExitSession(session);
				if (exitResult == XR_SUCCESS) {
					exitRequested = true;
				} else if (exitResult == XR_SESSION_NOT_FOCUSED) {
					std::cout << "OpenXR focus changed during shutdown; destroying the session directly.\n";
					shouldExit = true;
				} else {
					printFailure("requesting capture-screen session exit", exitResult);
					shutdownFailed = true;
					shouldExit = true;
				}
			} else if (sessionState != XR_SESSION_STATE_FOCUSED) {
				// xrRequestExitSession is focused-only. xrDestroySession is valid for
				// a running session, so this bounded diagnostic exits through cleanup
				// instead of waiting indefinitely for focus to return.
				std::cout << "OpenXR session is not focused; destroying it directly after the final frame.\n";
				shouldExit = true;
			}
		}
		if (shouldExit) {
			continue;
		}
		if (firstSubmittedAt == std::chrono::steady_clock::time_point{}
				&& now - loopStarted >= firstDisplayTimeout) {
			std::cerr << "No fresh captured stereo frame was displayed within 30 seconds.\n";
			terminalFailure = ExitCode::rendering;
			stopInitiated = true;
			stopStartedAt = now;
			nextExitAttempt = now;
			continue;
		}
		if (!sessionRunning) {
			std::this_thread::sleep_for(std::chrono::milliseconds{10});
			continue;
		}

		FrameScope frame;
		if (!frame.begin(session, blendMode)) {
			terminalFailure = ExitCode::openXrSession;
			break;
		}
		bool layerReady = false;
		bool renderFailed = false;
		XrCompositionLayerQuad leftLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
		XrCompositionLayerQuad rightLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
		const bool captureFresh = capture.hasFreshFrame(maximumCaptureAge);
		if (!stopInitiated && frame.state().shouldRender == XR_TRUE && captureFresh) {
			XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
			result = xrLocateSpace(viewSpace, localSpace, frame.state().predictedDisplayTime, &location);
			const XrSpaceLocationFlags required = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT
					| XR_SPACE_LOCATION_POSITION_VALID_BIT;
			if (XR_SUCCEEDED(result) && (location.locationFlags & required) == required) {
				const Pose headPose{
						Quaternion{location.pose.orientation.x, location.pose.orientation.y,
								location.pose.orientation.z, location.pose.orientation.w},
						Vec3{location.pose.position.x, location.pose.position.y, location.pose.position.z}};
				Pose screenPose;
				if (computeGazeCenteredRollFreePose(
						headPose, options.distanceMeters, basisState, screenPose)) {
					SwapchainImageLease leftImage;
					SwapchainImageLease rightImage;
					if (leftImage.acquire(eyeSwapchains[0]) && rightImage.acquire(eyeSwapchains[1])) {
						const WindowCaptureFrame& captured = capture.latestFrame();
						const bool rendered = options.eyeOrder == EyeOrder::leftRight
								? renderer.render(d3d.context.Get(), captured.texture.Get(),
										captured.combinedWidth, captured.height, leftImage, rightImage)
								: renderer.render(d3d.context.Get(), captured.texture.Get(),
										captured.combinedWidth, captured.height, rightImage, leftImage);
						const bool rightReleased = rightImage.release();
						const bool leftReleased = leftImage.release();
						if (rendered && rightReleased && leftReleased) {
							const XrPosef pose = toXrPose(screenPose);
							configureQuad(leftLayer, XR_EYE_VISIBILITY_LEFT, localSpace,
									eyeSwapchains[0].swapchain, pose, options.widthMeters,
									heightMeters, eyeExtent);
							configureQuad(rightLayer, XR_EYE_VISIBILITY_RIGHT, localSpace,
									eyeSwapchains[1].swapchain, pose, options.widthMeters,
									heightMeters, eyeExtent);
							layerReady = true;
						} else {
							renderFailed = true;
						}
					} else {
						renderFailed = true;
					}
				} else {
					++invalidPoseFrames;
				}
			} else {
				if (XR_FAILED(result)) {
					printFailure("locating VIEW space for captured screen", result);
				}
				++invalidPoseFrames;
			}
		} else if (!stopInitiated && frame.state().shouldRender == XR_TRUE && !captureFresh) {
			++staleFrames;
		}

		const XrCompositionLayerBaseHeader* layers[]{
				reinterpret_cast<const XrCompositionLayerBaseHeader*>(&leftLayer),
				reinterpret_cast<const XrCompositionLayerBaseHeader*>(&rightLayer)};
		if (!frame.end(layerReady ? layers : nullptr, layerReady ? 2U : 0U)) {
			terminalFailure = ExitCode::openXrSession;
			break;
		}
		if (renderFailed) {
			terminalFailure = ExitCode::rendering;
			continue;
		}
		if (layerReady) {
			++submittedFrames;
			lastSubmittedAt = std::chrono::steady_clock::now();
			if (firstSubmittedAt == std::chrono::steady_clock::time_point{}) {
				firstSubmittedAt = lastSubmittedAt;
				std::cout << "First live captured stereo frame submitted. Display timer started.\n";
			}
		}
		if (now - lastStatus >= statusInterval) {
			const WindowCaptureStats& stats = capture.stats();
			std::cout << "XR frames=" << submittedFrames
					  << " capture=" << stats.usableFrames << '/' << stats.receivedFrames
					  << " stale=" << staleFrames << " invalid-pose=" << invalidPoseFrames
					  << " resizes=" << stats.resizes << '\n';
			lastStatus = now;
		}
	}

	const WindowCaptureStats finalStats = capture.stats();
	const bool cleanupSucceeded = cleanup();
	std::cout << "Capture summary: received=" << finalStats.receivedFrames
			  << " usable=" << finalStats.usableFrames
			  << " discarded=" << finalStats.discardedFrames
			  << " resizes=" << finalStats.resizes << '\n';
	if (terminalFailure) {
		return *terminalFailure;
	}
	if (!cleanupSucceeded) {
		std::cerr << "Capture-screen GPU teardown did not complete safely.\n";
		return ExitCode::rendering;
	}
	if (runtimeLoss || shutdownFailed || !stopInitiated) {
		std::cerr << "Capture-screen probe ended before a normal bounded shutdown.\n";
		return ExitCode::openXrSession;
	}
	if (cancelledBeforeValidation) {
		std::cout << "Capture-screen probe was cancelled before display validation.\n";
		return ExitCode::cancelledBeforeValidation;
	}
	if (submittedFrames == 0 && !userCancelled) {
		std::cerr << "No live captured stereo frame was successfully displayed.\n";
		return ExitCode::rendering;
	}
	if (submittedFrames == 0) {
		std::cerr << "Capture-screen probe stopped without a validated display frame.\n";
		return ExitCode::rendering;
	} else {
		std::cout << "Capture-screen probe completed after " << submittedFrames
				  << " submitted frame(s).\n";
	}
	return ExitCode::success;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
	std::wcout << L"MCXRInput live OpenXR capture-screen probe\n";
	Options options;
	if (!parseOptions(argc, argv, options)) {
		printUsage();
		return static_cast<int>(ExitCode::usage);
	}
	if (options.help) {
		printUsage();
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

	const WindowSelectionOptions selection{options.executable, options.window};
	const auto selected = selectWindow(selection, std::wcerr);
	if (!selected) {
		return static_cast<int>(ExitCode::windowSelection);
	}
	std::wcout << L"Selected window:\n";
	printWindow(*selected, std::wcout);
	SetConsoleCtrlHandler(handleConsoleControl, TRUE);

	try {
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
		return static_cast<int>(runCaptureScreen(options, *selected));
	} catch (const winrt::hresult_error& error) {
		std::wcerr << L"Initializing live capture failed (HRESULT 0x" << std::hex
				   << static_cast<std::uint32_t>(error.code().value) << std::dec
				   << L"): " << error.message().c_str() << L'\n';
		return static_cast<int>(ExitCode::capture);
	} catch (const std::exception& error) {
		std::cerr << "Live capture initialization failed: " << error.what() << '\n';
		return static_cast<int>(ExitCode::capture);
	}
}
