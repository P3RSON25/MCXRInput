#include <mcxrinput/openxr_d3d11.hpp>
#include <mcxrinput/screen_pose_math.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace mcxrinput::native;

namespace {

constexpr XrFormFactor formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
constexpr std::uint32_t targetCardWidth = 1280;
constexpr std::uint32_t targetCardHeight = 720;
constexpr auto startupTimeout = std::chrono::seconds{30};
constexpr auto shutdownGrace = std::chrono::seconds{5};
constexpr auto statusInterval = std::chrono::seconds{1};

std::atomic_bool stopRequested{false};

enum class ExitCode : int {
	success = 0,
	usage = 1,
	openXrRuntime = 2,
	noHeadset = 3,
	openXrSession = 4,
	rendering = 5,
};

enum class EyeOrder {
	leftRight,
	rightLeft,
};

struct Options {
	int seconds{30};
	float distanceMeters{1.5F};
	float widthMeters{1.6F};
	EyeOrder eyeOrder{EyeOrder::leftRight};
	bool help{false};
};

struct Rgba {
	std::uint8_t red;
	std::uint8_t green;
	std::uint8_t blue;
	std::uint8_t alpha;
};

struct CardResources {
	SwapchainBundle swapchain;
	ComPtr<ID3D11Texture2D> sourceTexture;
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
	std::cout
			<< "Usage: MCXRInputOpenXRStereoScreenProbe.exe [options]\n\n"
			<< "Shows eye-specific synthetic cards on a roll-stabilized OpenXR quad.\n"
			<< "This diagnostic does not capture Minecraft, send UDP, or read controllers.\n\n"
			<< "Options:\n"
			<< "  --seconds N          Duration after first displayed frame (1-300; default 30)\n"
			<< "  --distance-m N       Screen distance in meters (0.25-5.0; default 1.5)\n"
			<< "  --width-m N          Screen width in meters (0.25-4.0; default 1.6)\n"
			<< "  --eye-order lr|rl    Texture routing diagnostic (default lr)\n"
			<< "  --help               Show this help\n";
}

bool parseInteger(std::string_view text, int minimum, int maximum, int& output) {
	int parsed = 0;
	const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
	if (result.ec != std::errc{} || result.ptr != text.data() + text.size()
			|| parsed < minimum || parsed > maximum) {
		return false;
	}
	output = parsed;
	return true;
}

bool parseFloat(std::string_view text, float minimum, float maximum, float& output) {
	float parsed = 0.0F;
	const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
	if (result.ec != std::errc{} || result.ptr != text.data() + text.size()
			|| !std::isfinite(parsed) || parsed < minimum || parsed > maximum) {
		return false;
	}
	output = parsed;
	return true;
}

bool parseOptions(int argc, char** argv, Options& options) {
	for (int index = 1; index < argc; ++index) {
		const std::string_view argument{argv[index]};
		if (argument == "--help" || argument == "-h") {
			options.help = true;
			return true;
		}
		if (argument == "--seconds") {
			if (index + 1 >= argc || !parseInteger(argv[index + 1], 1, 300, options.seconds)) {
				std::cerr << "Expected --seconds to be followed by an integer from 1 to 300.\n";
				return false;
			}
			++index;
			continue;
		}
		if (argument == "--distance-m") {
			if (index + 1 >= argc || !parseFloat(argv[index + 1], 0.25F, 5.0F, options.distanceMeters)) {
				std::cerr << "Expected --distance-m to be followed by a number from 0.25 to 5.0.\n";
				return false;
			}
			++index;
			continue;
		}
		if (argument == "--width-m") {
			if (index + 1 >= argc || !parseFloat(argv[index + 1], 0.25F, 4.0F, options.widthMeters)) {
				std::cerr << "Expected --width-m to be followed by a number from 0.25 to 4.0.\n";
				return false;
			}
			++index;
			continue;
		}
		if (argument == "--eye-order") {
			if (index + 1 >= argc) {
				std::cerr << "Expected --eye-order to be followed by lr or rl.\n";
				return false;
			}
			const std::string_view order{argv[++index]};
			if (order == "lr") {
				options.eyeOrder = EyeOrder::leftRight;
			} else if (order == "rl") {
				options.eyeOrder = EyeOrder::rightLeft;
			} else {
				std::cerr << "Expected --eye-order to be followed by lr or rl.\n";
				return false;
			}
			continue;
		}

		std::cerr << "Unknown argument: " << argument << '\n';
		return false;
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
		switch (event.type) {
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
			const auto& changed = reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
			sessionState = changed.state;
			std::cout << "Session state: " << sessionStateName(sessionState) << '\n';
			if (sessionState == XR_SESSION_STATE_READY) {
				XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
				beginInfo.primaryViewConfigurationType = primaryStereoViewConfiguration;
				const XrResult beginResult = xrBeginSession(session, &beginInfo);
				if (XR_FAILED(beginResult)) {
					printFailure("beginning synthetic screen session", beginResult);
					return false;
				}
				sessionRunning = true;
			} else if (sessionState == XR_SESSION_STATE_STOPPING) {
				if (sessionRunning) {
					const XrResult endResult = xrEndSession(session);
					// The session is non-running after xrEndSession regardless of its result.
					sessionRunning = false;
					if (XR_FAILED(endResult)) {
						printFailure("ending synthetic screen session", endResult);
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
			break;
		}
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			std::cerr << "OpenXR runtime reported instance loss pending.\n";
			runtimeLoss = true;
			shouldExit = true;
			break;
		case XR_TYPE_EVENT_DATA_EVENTS_LOST: {
			const auto& lost = reinterpret_cast<const XrEventDataEventsLost&>(event);
			std::cerr << "OpenXR runtime dropped " << lost.lostEventCount << " event(s).\n";
			break;
		}
		default:
			break;
		}

		event = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
		result = xrPollEvent(instance, &event);
	}

	if (result != XR_EVENT_UNAVAILABLE) {
		printFailure("polling synthetic screen events", result);
		return false;
	}
	return true;
}

bool createReferenceSpace(XrSession session, XrReferenceSpaceType type, XrSpace& space) {
	XrReferenceSpaceCreateInfo createInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	createInfo.referenceSpaceType = type;
	createInfo.poseInReferenceSpace.orientation.w = 1.0F;
	const XrResult result = xrCreateReferenceSpace(session, &createInfo, &space);
	if (XR_FAILED(result)) {
		printFailure(type == XR_REFERENCE_SPACE_TYPE_LOCAL
				? "creating LOCAL reference space" : "creating VIEW reference space", result);
		return false;
	}
	return true;
}

void writePixel(
		std::vector<std::uint8_t>& pixels, std::uint32_t width,
		std::uint32_t x, std::uint32_t y, Rgba color, bool bgra) {
	const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
	pixels[index + 0] = bgra ? color.blue : color.red;
	pixels[index + 1] = color.green;
	pixels[index + 2] = bgra ? color.red : color.blue;
	pixels[index + 3] = color.alpha;
}

void fillRectangle(
		std::vector<std::uint8_t>& pixels, std::uint32_t width, std::uint32_t height,
		int left, int top, int right, int bottom, Rgba color, bool bgra) {
	left = std::clamp(left, 0, static_cast<int>(width));
	right = std::clamp(right, 0, static_cast<int>(width));
	top = std::clamp(top, 0, static_cast<int>(height));
	bottom = std::clamp(bottom, 0, static_cast<int>(height));
	for (int y = top; y < bottom; ++y) {
		for (int x = left; x < right; ++x) {
			writePixel(pixels, width, static_cast<std::uint32_t>(x),
					static_cast<std::uint32_t>(y), color, bgra);
		}
	}
}

std::vector<std::uint8_t> makeCardPixels(
		std::uint32_t width, std::uint32_t height, bool leftEye, DXGI_FORMAT format) {
	const bool bgra = format == DXGI_FORMAT_B8G8R8A8_UNORM
			|| format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	const Rgba background = leftEye ? Rgba{8, 34, 112, 255} : Rgba{118, 38, 4, 255};
	const Rgba grid = leftEye ? Rgba{28, 68, 160, 255} : Rgba{170, 70, 22, 255};
	const Rgba white{245, 245, 245, 255};
	const Rgba marker = leftEye ? Rgba{0, 255, 230, 255} : Rgba{255, 235, 0, 255};
	std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
	fillRectangle(pixels, width, height, 0, 0, static_cast<int>(width),
			static_cast<int>(height), background, bgra);

	const int gridStep = std::max(32, static_cast<int>(width / 16));
	const int line = std::max(2, static_cast<int>(width / 640));
	for (int x = 0; x < static_cast<int>(width); x += gridStep) {
		fillRectangle(pixels, width, height, x, 0, x + line, static_cast<int>(height), grid, bgra);
	}
	for (int y = 0; y < static_cast<int>(height); y += gridStep) {
		fillRectangle(pixels, width, height, 0, y, static_cast<int>(width), y + line, grid, bgra);
	}

	const int border = std::max(8, static_cast<int>(width / 100));
	fillRectangle(pixels, width, height, 0, 0, static_cast<int>(width), border, white, bgra);
	fillRectangle(pixels, width, height, 0, static_cast<int>(height) - border,
			static_cast<int>(width), static_cast<int>(height), white, bgra);
	fillRectangle(pixels, width, height, 0, 0, border, static_cast<int>(height), white, bgra);
	fillRectangle(pixels, width, height, static_cast<int>(width) - border, 0,
			static_cast<int>(width), static_cast<int>(height), white, bgra);

	const int x0 = static_cast<int>(width * 0.23F);
	const int x1 = static_cast<int>(width * 0.65F);
	const int y0 = static_cast<int>(height * 0.18F);
	const int y1 = static_cast<int>(height * 0.78F);
	const int stroke = std::max(18, static_cast<int>(width * 0.055F));
	fillRectangle(pixels, width, height, x0, y0, x0 + stroke, y1, white, bgra);
	if (leftEye) {
		fillRectangle(pixels, width, height, x0, y1 - stroke, x1, y1, white, bgra);
	} else {
		fillRectangle(pixels, width, height, x0, y0, x1, y0 + stroke, white, bgra);
		fillRectangle(pixels, width, height, x0, (y0 + y1) / 2 - stroke / 2,
				x1, (y0 + y1) / 2 + stroke / 2, white, bgra);
		fillRectangle(pixels, width, height, x1 - stroke, y0, x1,
				(y0 + y1) / 2, white, bgra);
		for (int y = (y0 + y1) / 2; y < y1; ++y) {
			const float progress = static_cast<float>(y - (y0 + y1) / 2)
					/ static_cast<float>((y1 - y0) / 2);
			const int x = static_cast<int>(x0 + stroke * 0.5F
					+ progress * static_cast<float>(x1 - x0 - stroke));
			fillRectangle(pixels, width, height, x - stroke / 2, y,
					x + stroke / 2, y + 2, white, bgra);
		}
	}

	// Opposite marker offsets create an obvious binocular-disparity check.
	const int markerSize = std::max(28, static_cast<int>(width * 0.04F));
	const int markerX = static_cast<int>(width / 2) + (leftEye ? markerSize : -markerSize);
	const int markerY = static_cast<int>(height * 0.88F);
	fillRectangle(pixels, width, height, markerX - markerSize, markerY - markerSize,
			markerX + markerSize, markerY + markerSize, marker, bgra);
	fillRectangle(pixels, width, height, markerX - markerSize / 4, markerY - markerSize * 2,
			markerX + markerSize / 4, markerY + markerSize * 2, white, bgra);
	return pixels;
}

bool createCardTexture(
		ID3D11Device* device, std::uint32_t width, std::uint32_t height,
		DXGI_FORMAT format, bool leftEye, ComPtr<ID3D11Texture2D>& texture) {
	const std::vector<std::uint8_t> pixels = makeCardPixels(width, height, leftEye, format);
	D3D11_TEXTURE2D_DESC description{};
	description.Width = width;
	description.Height = height;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = format;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_DEFAULT;

	D3D11_SUBRESOURCE_DATA initialData{};
	initialData.pSysMem = pixels.data();
	initialData.SysMemPitch = width * 4;
	const HRESULT hr = device->CreateTexture2D(&description, &initialData, &texture);
	if (FAILED(hr)) {
		printHresult(leftEye ? "creating left-eye test card" : "creating right-eye test card", hr);
		return false;
	}
	return true;
}

bool uploadCards(
		ID3D11DeviceContext* context, const CardResources& leftCard,
		const CardResources& rightCard) {
	SwapchainImageLease leftImage;
	if (!leftImage.acquire(leftCard.swapchain)) {
		return false;
	}
	SwapchainImageLease rightImage;
	if (!rightImage.acquire(rightCard.swapchain)) {
		return false;
	}
	context->CopyResource(leftImage.texture(), leftCard.sourceTexture.Get());
	context->CopyResource(rightImage.texture(), rightCard.sourceTexture.Get());
	context->Flush();
	const bool rightReleased = rightImage.release();
	const bool leftReleased = leftImage.release();
	// Copy on every acquisition rather than assuming the runtime preserves an
	// untouched image. This is intentionally conservative for this diagnostic.
	return rightReleased && leftReleased;
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
		printHresult("creating D3D11 completion query", createResult);
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
			printHresult("waiting for D3D11 completion", result);
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{1});
	}
	std::cerr << "Timed out waiting for the final D3D11 card upload to complete.\n";
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
		float widthMeters, float heightMeters, std::uint32_t width, std::uint32_t height) {
	layer = XrCompositionLayerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
	layer.layerFlags = 0;
	layer.space = localSpace;
	layer.eyeVisibility = eye;
	layer.subImage.swapchain = swapchain;
	layer.subImage.imageRect.offset = XrOffset2Di{0, 0};
	layer.subImage.imageRect.extent = XrExtent2Di{
			static_cast<std::int32_t>(width), static_cast<std::int32_t>(height)};
	layer.subImage.imageArrayIndex = 0;
	layer.pose = pose;
	layer.size = XrExtent2Df{widthMeters, heightMeters};
}

} // namespace

int main(int argc, char** argv) {
	std::cout << "MCXRInput OpenXR synthetic stereo-screen probe\n";
	std::cout << std::fixed << std::setprecision(3);

	Options options;
	if (!parseOptions(argc, argv, options)) {
		printUsage();
		return static_cast<int>(ExitCode::usage);
	}
	if (options.help) {
		printUsage();
		return static_cast<int>(ExitCode::success);
	}

	SetConsoleCtrlHandler(handleConsoleControl, TRUE);
	XrInstance instance = XR_NULL_HANDLE;
	XrSession session = XR_NULL_HANDLE;
	XrSpace localSpace = XR_NULL_HANDLE;
	XrSpace viewSpace = XR_NULL_HANDLE;
	D3DState d3d;
	std::array<CardResources, 2> cards;

	auto cleanup = [&]() {
		if (d3d.context != nullptr) {
			waitForGpuIdle(d3d.device.Get(), d3d.context.Get());
			d3d.context->ClearState();
			d3d.context->Flush();
		}
		for (CardResources& card : cards) {
			card.sourceTexture.Reset();
			card.swapchain.reset();
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
	};

	std::vector<XrExtensionProperties> extensions;
	if (!enumerateInstanceExtensions(extensions)) {
		cleanup();
		return static_cast<int>(ExitCode::openXrRuntime);
	}
	if (!hasExtension(extensions, XR_KHR_D3D11_ENABLE_EXTENSION_NAME)) {
		std::cerr << "The active OpenXR runtime does not advertise "
				  << XR_KHR_D3D11_ENABLE_EXTENSION_NAME << ".\n";
		cleanup();
		return static_cast<int>(ExitCode::openXrRuntime);
	}

	XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	copyText("MCXRInput", instanceInfo.applicationInfo.applicationName);
	instanceInfo.applicationInfo.applicationVersion = 1;
	copyText("MCXRInput synthetic screen probe", instanceInfo.applicationInfo.engineName);
	instanceInfo.applicationInfo.engineVersion = 1;
	instanceInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	const char* enabledExtensions[]{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
	instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(std::size(enabledExtensions));
	instanceInfo.enabledExtensionNames = enabledExtensions;
	XrResult result = xrCreateInstance(&instanceInfo, &instance);
	if (XR_FAILED(result)) {
		printFailure("creating synthetic screen OpenXR instance", result);
		std::cerr << "Check that SteamVR is running and selected as the OpenXR runtime.\n";
		cleanup();
		return static_cast<int>(ExitCode::openXrRuntime);
	}

	XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
	result = xrGetInstanceProperties(instance, &instanceProperties);
	if (XR_SUCCEEDED(result)) {
		std::cout << "Runtime: " << instanceProperties.runtimeName << ' '
				  << XR_VERSION_MAJOR(instanceProperties.runtimeVersion) << '.'
				  << XR_VERSION_MINOR(instanceProperties.runtimeVersion) << '.'
				  << XR_VERSION_PATCH(instanceProperties.runtimeVersion) << '\n';
	}

	XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
	systemInfo.formFactor = formFactor;
	XrSystemId systemId = XR_NULL_SYSTEM_ID;
	result = xrGetSystem(instance, &systemInfo, &systemId);
	if (XR_FAILED(result)) {
		printFailure("requesting an HMD for the synthetic screen", result);
		std::cerr << "No OpenXR HMD is available. Confirm SteamVR sees the headset.\n";
		cleanup();
		return static_cast<int>(ExitCode::noHeadset);
	}

	XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
	result = xrGetSystemProperties(instance, systemId, &systemProperties);
	if (XR_FAILED(result)) {
		printFailure("querying HMD layer limits", result);
		cleanup();
		return static_cast<int>(ExitCode::openXrSession);
	}
	if (systemProperties.graphicsProperties.maxLayerCount < 2) {
		std::cerr << "The OpenXR runtime supports only "
				  << systemProperties.graphicsProperties.maxLayerCount
				  << " composition layer(s); this eye-routing probe requires two.\n";
		cleanup();
		return static_cast<int>(ExitCode::rendering);
	}

	std::vector<XrViewConfigurationView> viewConfigs;
	if (!enumerateViewConfigurationViews(instance, systemId, viewConfigs)
			|| viewConfigs.size() != 2) {
		std::cerr << "This diagnostic requires exactly two PRIMARY_STEREO views.\n";
		cleanup();
		return static_cast<int>(ExitCode::openXrSession);
	}

	PFN_xrGetD3D11GraphicsRequirementsKHR getRequirements = nullptr;
	if (!loadD3D11RequirementsFunction(instance, getRequirements)) {
		cleanup();
		return static_cast<int>(ExitCode::openXrSession);
	}
	XrGraphicsRequirementsD3D11KHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
	result = getRequirements(instance, systemId, &graphicsRequirements);
	if (XR_FAILED(result) || !createD3D11Device(graphicsRequirements, d3d)) {
		if (XR_FAILED(result)) {
			printFailure("querying D3D11 graphics requirements", result);
		}
		cleanup();
		return static_cast<int>(ExitCode::openXrSession);
	}

	XrGraphicsBindingD3D11KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
	graphicsBinding.device = d3d.device.Get();
	XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionInfo.next = &graphicsBinding;
	sessionInfo.systemId = systemId;
	result = xrCreateSession(instance, &sessionInfo, &session);
	if (XR_FAILED(result)) {
		printFailure("creating synthetic screen D3D11 session", result);
		cleanup();
		return static_cast<int>(ExitCode::openXrSession);
	}

	if (!createReferenceSpace(session, XR_REFERENCE_SPACE_TYPE_LOCAL, localSpace)
			|| !createReferenceSpace(session, XR_REFERENCE_SPACE_TYPE_VIEW, viewSpace)) {
		cleanup();
		return static_cast<int>(ExitCode::openXrSession);
	}

	XrEnvironmentBlendMode blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	std::int64_t swapchainFormat = 0;
	if (!chooseEnvironmentBlendMode(instance, systemId, blendMode)
			|| !chooseSwapchainFormat(session, swapchainFormat)) {
		cleanup();
		return static_cast<int>(ExitCode::openXrSession);
	}

	const std::uint32_t cardWidth = std::max(1U, std::min(
			targetCardWidth, systemProperties.graphicsProperties.maxSwapchainImageWidth));
	const std::uint32_t heightForAspect = std::max(1U, cardWidth * 9U / 16U);
	std::uint32_t cardHeight = std::min(
			targetCardHeight, systemProperties.graphicsProperties.maxSwapchainImageHeight);
	cardHeight = std::min(cardHeight, heightForAspect);
	const std::uint32_t widthForAspect = std::max(1U, cardHeight * 16U / 9U);
	const std::uint32_t finalCardWidth = std::min(cardWidth, widthForAspect);
	const DXGI_FORMAT d3dFormat = static_cast<DXGI_FORMAT>(swapchainFormat);
	for (std::size_t index = 0; index < cards.size(); ++index) {
		const ColorSwapchainDescription description{
				finalCardWidth,
				cardHeight,
				swapchainFormat,
				1,
				1,
				XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT,
		};
		if (!createColorSwapchain(session, d3d.device.Get(), description, cards[index].swapchain)
				|| !createCardTexture(d3d.device.Get(), finalCardWidth, cardHeight, d3dFormat,
						index == 0, cards[index].sourceTexture)) {
			cleanup();
			return static_cast<int>(ExitCode::rendering);
		}
	}

	const float heightMeters = options.widthMeters
			* static_cast<float>(cardHeight) / static_cast<float>(finalCardWidth);
	const bool normalEyeOrder = options.eyeOrder == EyeOrder::leftRight;
	std::cout << "Synthetic card textures: " << finalCardWidth << 'x' << cardHeight << '\n'
			  << "Virtual screen: " << options.widthMeters << 'x' << heightMeters
			  << " m at " << options.distanceMeters << " m\n"
			  << "Texture routing: "
			  << (normalEyeOrder ? "L->left, R->right" : "R->left, L->right")
			  << "\n\nAcceptance check in the headset:\n"
			  << (normalEyeOrder
					  ? "  1. Close each eye: physical left sees blue L; physical right sees orange R.\n"
					  : "  1. Reversed diagnostic: physical left sees orange R; physical right sees blue L.\n")
			  << "  2. Yaw, pitch, and translate: the card stays centered in physical gaze.\n"
			  << "  3. Tilt toward either shoulder: the card stays gravity-level (head roll removed).\n"
			  << "The probe stops after " << options.seconds << " seconds. Press Ctrl+C to stop early.\n";

	bool sessionRunning = false;
	bool shouldExit = false;
	bool exitRequested = false;
	bool normalStopInitiated = false;
	bool userCancelled = false;
	bool runtimeLoss = false;
	bool shutdownFailed = false;
	bool reportedWaitingForFocus = false;
	bool sessionAnnounced = false;
	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
	RollFreeBasisState basisState;
	std::uint64_t renderedFrames = 0;
	std::uint64_t invalidPoseFrames = 0;
	const auto processStarted = std::chrono::steady_clock::now();
	auto lastStatus = processStarted;
	std::chrono::steady_clock::time_point firstRenderedAt{};
	std::chrono::steady_clock::time_point stopInitiatedAt{};
	std::chrono::steady_clock::time_point nextExitAttempt{};

	while (!shouldExit) {
		if (!pollEvents(
				instance, session, sessionRunning, sessionState, shouldExit, runtimeLoss)) {
			cleanup();
			return static_cast<int>(ExitCode::openXrSession);
		}
		if (shouldExit) {
			break;
		}
		const auto now = std::chrono::steady_clock::now();
		if (sessionRunning && !sessionAnnounced) {
			sessionAnnounced = true;
			std::cout << "Synthetic stereo screen session is running.\n";
		}
		const bool durationElapsed = firstRenderedAt != std::chrono::steady_clock::time_point{}
				&& now - firstRenderedAt >= std::chrono::seconds{options.seconds};
		if ((stopRequested.load() || durationElapsed) && !normalStopInitiated) {
			normalStopInitiated = true;
			userCancelled = stopRequested.load();
			stopInitiatedAt = now;
			nextExitAttempt = now;
		}
		if (normalStopInitiated && !sessionRunning) {
			shouldExit = true;
		} else if (normalStopInitiated && now - stopInitiatedAt >= shutdownGrace) {
			std::cerr << "OpenXR session did not accept/complete a focused exit within five seconds.\n";
			shutdownFailed = true;
			shouldExit = true;
		} else if (normalStopInitiated && !exitRequested) {
			if (sessionState == XR_SESSION_STATE_FOCUSED && now >= nextExitAttempt) {
				const XrResult exitResult = xrRequestExitSession(session);
				if (exitResult == XR_SUCCESS) {
					exitRequested = true;
				} else if (exitResult == XR_SESSION_NOT_FOCUSED) {
					if (!reportedWaitingForFocus) {
						std::cout << "OpenXR focus changed during shutdown; waiting to retry safely.\n";
						reportedWaitingForFocus = true;
					}
					nextExitAttempt = now + std::chrono::milliseconds{100};
				} else {
					printFailure("requesting synthetic screen session exit", exitResult);
					shutdownFailed = true;
					shouldExit = true;
				}
			} else if (!reportedWaitingForFocus) {
				std::cout << "Waiting for OpenXR focus before requesting a legal session exit.\n";
				reportedWaitingForFocus = true;
			}
		}
		if (shouldExit) {
			continue;
		}
		if (firstRenderedAt == std::chrono::steady_clock::time_point{}
				&& now - processStarted >= startupTimeout) {
			std::cerr << "OpenXR did not submit a visible synthetic frame within 30 seconds.\n"
					  << "Confirm SteamVR is focused, no other VR app owns focus, and the headset is awake.\n";
			cleanup();
			return static_cast<int>(ExitCode::openXrSession);
		}
		if (!sessionRunning) {
			std::this_thread::sleep_for(std::chrono::milliseconds{10});
			continue;
		}

		FrameScope frame;
		if (!frame.begin(session, blendMode)) {
			cleanup();
			return static_cast<int>(ExitCode::openXrSession);
		}
		bool layerReady = false;
		bool cardFailure = false;
		XrCompositionLayerQuad leftLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
		XrCompositionLayerQuad rightLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
		if (frame.state().shouldRender == XR_TRUE) {
			XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
			result = xrLocateSpace(viewSpace, localSpace, frame.state().predictedDisplayTime, &location);
			const XrSpaceLocationFlags required = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT
					| XR_SPACE_LOCATION_POSITION_VALID_BIT;
			if (XR_SUCCEEDED(result) && (location.locationFlags & required) == required) {
				const Pose headPose{
						Quaternion{location.pose.orientation.x, location.pose.orientation.y,
								location.pose.orientation.z, location.pose.orientation.w},
						Vec3{location.pose.position.x, location.pose.position.y, location.pose.position.z},
				};
				Pose screenPose;
				if (computeGazeCenteredRollFreePose(
						headPose, options.distanceMeters, basisState, screenPose)) {
					cardFailure = !uploadCards(d3d.context.Get(), cards[0], cards[1]);
					if (!cardFailure) {
						const XrPosef pose = toXrPose(screenPose);
						const std::size_t leftSource = options.eyeOrder == EyeOrder::leftRight ? 0 : 1;
						const std::size_t rightSource = options.eyeOrder == EyeOrder::leftRight ? 1 : 0;
						configureQuad(leftLayer, XR_EYE_VISIBILITY_LEFT, localSpace,
								cards[leftSource].swapchain.swapchain, pose, options.widthMeters,
								heightMeters, finalCardWidth, cardHeight);
						configureQuad(rightLayer, XR_EYE_VISIBILITY_RIGHT, localSpace,
								cards[rightSource].swapchain.swapchain, pose, options.widthMeters,
								heightMeters, finalCardWidth, cardHeight);
						layerReady = true;
					}
				} else {
					++invalidPoseFrames;
				}
			} else {
				if (XR_FAILED(result)) {
					printFailure("locating VIEW space for synthetic screen", result);
				}
				++invalidPoseFrames;
			}
		}

		const XrCompositionLayerBaseHeader* layers[]{
				reinterpret_cast<const XrCompositionLayerBaseHeader*>(&leftLayer),
				reinterpret_cast<const XrCompositionLayerBaseHeader*>(&rightLayer),
		};
		if (!frame.end(layerReady ? layers : nullptr, layerReady ? 2U : 0U)) {
			cleanup();
			return static_cast<int>(ExitCode::openXrSession);
		}
		if (cardFailure) {
			cleanup();
			return static_cast<int>(ExitCode::rendering);
		}
		if (layerReady) {
			++renderedFrames;
			if (firstRenderedAt == std::chrono::steady_clock::time_point{}) {
				firstRenderedAt = std::chrono::steady_clock::now();
				std::cout << "First synthetic stereo frame submitted. Display timer started.\n";
			}
		}

		if (now - lastStatus >= statusInterval) {
			std::cout << "Frames rendered=" << renderedFrames
					  << " invalid-pose=" << invalidPoseFrames << '\n';
			lastStatus = now;
		}
	}

	cleanup();
	if (runtimeLoss || shutdownFailed || !normalStopInitiated) {
		std::cerr << "Synthetic stereo-screen probe ended before a normal bounded shutdown.\n";
		return static_cast<int>(ExitCode::openXrSession);
	}
	if (renderedFrames == 0) {
		if (userCancelled) {
			std::cout << "Synthetic stereo-screen probe was cancelled before display validation.\n";
			return static_cast<int>(ExitCode::success);
		}
		std::cerr << "No synthetic stereo frame was successfully submitted.\n";
		return static_cast<int>(ExitCode::rendering);
	}
	std::cout << "Synthetic stereo-screen probe completed after " << renderedFrames
			  << " submitted frame(s).\n";
	return static_cast<int>(ExitCode::success);
}
