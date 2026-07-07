#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wrl/client.h>

#include <d3d11.h>
#include <dxgi1_6.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

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
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr std::uint16_t defaultUdpPort = 28771;
constexpr std::string_view loopbackAddress = "127.0.0.1";
constexpr XrFormFactor formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
constexpr XrViewConfigurationType viewConfiguration = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
constexpr auto statusInterval = std::chrono::seconds{1};

std::atomic_bool stopRequested{false};

enum class ExitCode : int {
	success = 0,
	usage = 1,
	openXrRuntime = 2,
	noHeadset = 3,
	openXrSession = 4,
	network = 5,
};

struct Options {
	std::uint16_t port{defaultUdpPort};
	bool help{false};
};

struct D3DState {
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL featureLevel{};
};

struct SwapchainBundle {
	XrSwapchain swapchain{XR_NULL_HANDLE};
	std::uint32_t width{0};
	std::uint32_t height{0};
	std::vector<XrSwapchainImageD3D11KHR> images;
	std::vector<ComPtr<ID3D11RenderTargetView>> renderTargets;
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

void printUsage() {
	std::cout
			<< "Usage: MCXRInputOpenXRBridge.exe [--port 28771]\n\n"
			<< "Runs a focused SteamVR/OpenXR D3D11 session and sends live HMD\n"
			<< "orientation to the MCXRInput Fabric mod over localhost UDP.\n";
}

bool parsePort(std::string_view text, std::uint16_t& port) {
	int parsed = 0;
	const char* begin = text.data();
	const char* end = text.data() + text.size();
	const std::from_chars_result result = std::from_chars(begin, end, parsed);
	if (result.ec != std::errc{} || result.ptr != end || parsed < 1024 || parsed > 65535) {
		return false;
	}
	port = static_cast<std::uint16_t>(parsed);
	return true;
}

bool parseOptions(int argc, char** argv, Options& options) {
	for (int index = 1; index < argc; ++index) {
		const std::string_view argument{argv[index]};
		if (argument == "--help" || argument == "-h") {
			options.help = true;
			return true;
		}
		if (argument == "--port") {
			if (index + 1 >= argc || !parsePort(argv[index + 1], options.port)) {
				std::cerr << "Expected --port to be followed by a UDP port from 1024 to 65535.\n";
				return false;
			}
			++index;
			continue;
		}

		std::cerr << "Unknown argument: " << argument << '\n';
		return false;
	}
	return true;
}

std::string resultToString(XrResult result) {
	switch (result) {
	case XR_SUCCESS:
		return "XR_SUCCESS";
	case XR_TIMEOUT_EXPIRED:
		return "XR_TIMEOUT_EXPIRED";
	case XR_SESSION_LOSS_PENDING:
		return "XR_SESSION_LOSS_PENDING";
	case XR_ERROR_RUNTIME_FAILURE:
		return "XR_ERROR_RUNTIME_FAILURE";
	case XR_ERROR_API_VERSION_UNSUPPORTED:
		return "XR_ERROR_API_VERSION_UNSUPPORTED";
	case XR_ERROR_INSTANCE_LOST:
		return "XR_ERROR_INSTANCE_LOST";
	case XR_ERROR_SESSION_LOST:
		return "XR_ERROR_SESSION_LOST";
	case XR_ERROR_INITIALIZATION_FAILED:
		return "XR_ERROR_INITIALIZATION_FAILED";
	case XR_ERROR_HANDLE_INVALID:
		return "XR_ERROR_HANDLE_INVALID";
	case XR_ERROR_FORM_FACTOR_UNAVAILABLE:
		return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
	case XR_ERROR_FORM_FACTOR_UNSUPPORTED:
		return "XR_ERROR_FORM_FACTOR_UNSUPPORTED";
	case XR_ERROR_EXTENSION_NOT_PRESENT:
		return "XR_ERROR_EXTENSION_NOT_PRESENT";
	case XR_ERROR_RUNTIME_UNAVAILABLE:
		return "XR_ERROR_RUNTIME_UNAVAILABLE";
	case XR_ERROR_GRAPHICS_DEVICE_INVALID:
		return "XR_ERROR_GRAPHICS_DEVICE_INVALID";
	default:
		return "unknown OpenXR result";
	}
}

std::string sessionStateToString(XrSessionState state) {
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
		return "unrecognized";
	}
}

void printFailure(std::string_view operation, XrResult result) {
	std::cerr << "MCXRInput OpenXR bridge: " << operation << " failed ("
			  << resultToString(result) << ", " << static_cast<int>(result) << ").\n";
}

void printHresult(std::string_view operation, HRESULT hr) {
	std::cerr << "MCXRInput OpenXR bridge: " << operation << " failed (HRESULT 0x"
			  << std::hex << static_cast<std::uint32_t>(hr) << std::dec << ").\n";
}

template <std::size_t Size>
void copyText(std::string_view text, char (&target)[Size]) {
	static_assert(Size > 0);
	const std::size_t count = std::min(text.size(), Size - 1);
	std::memcpy(target, text.data(), count);
	target[count] = '\0';
}

bool enumerateInstanceExtensions(std::vector<XrExtensionProperties>& extensions) {
	std::uint32_t count = 0;
	XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
	if (XR_FAILED(result)) {
		printFailure("enumerating OpenXR extension count", result);
		return false;
	}

	extensions.assign(count, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
	result = xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data());
	if (XR_FAILED(result)) {
		printFailure("enumerating OpenXR extensions", result);
		return false;
	}
	extensions.resize(count);
	return true;
}

bool hasExtension(const std::vector<XrExtensionProperties>& extensions, std::string_view name) {
	return std::any_of(extensions.begin(), extensions.end(), [&](const XrExtensionProperties& extension) {
		return name == extension.extensionName;
	});
}

bool loadD3D11RequirementsFunction(
		XrInstance instance, PFN_xrGetD3D11GraphicsRequirementsKHR& getRequirements) {
	PFN_xrVoidFunction function = nullptr;
	const XrResult result = xrGetInstanceProcAddr(instance, "xrGetD3D11GraphicsRequirementsKHR", &function);
	if (XR_FAILED(result) || function == nullptr) {
		if (XR_FAILED(result)) {
			printFailure("loading D3D11 graphics requirements function", result);
		} else {
			std::cerr << "OpenXR runtime did not return xrGetD3D11GraphicsRequirementsKHR.\n";
		}
		return false;
	}

	getRequirements = reinterpret_cast<PFN_xrGetD3D11GraphicsRequirementsKHR>(function);
	return true;
}

bool sameLuid(const LUID& left, const LUID& right) {
	return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}

bool findAdapterForLuid(const LUID& adapterLuid, ComPtr<IDXGIAdapter1>& adapter) {
	ComPtr<IDXGIFactory1> factory;
	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
	if (FAILED(hr)) {
		printHresult("creating DXGI factory", hr);
		return false;
	}

	for (UINT index = 0;; ++index) {
		ComPtr<IDXGIAdapter1> candidate;
		hr = factory->EnumAdapters1(index, &candidate);
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
			printHresult("describing DXGI adapter", hr);
			return false;
		}

		if (sameLuid(description.AdapterLuid, adapterLuid)) {
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

	const std::array<D3D_FEATURE_LEVEL, 7> featureLevels{
			D3D_FEATURE_LEVEL_12_1,
			D3D_FEATURE_LEVEL_12_0,
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
			D3D_FEATURE_LEVEL_9_3,
	};

	const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	HRESULT hr = D3D11CreateDevice(
			adapter.Get(),
			D3D_DRIVER_TYPE_UNKNOWN,
			nullptr,
			flags,
			featureLevels.data(),
			static_cast<UINT>(featureLevels.size()),
			D3D11_SDK_VERSION,
			&d3d.device,
			&d3d.featureLevel,
			&d3d.context);
	if (FAILED(hr)) {
		printHresult("creating D3D11 device", hr);
		return false;
	}

	if (d3d.featureLevel < requirements.minFeatureLevel) {
		std::cerr << "D3D11 feature level is below the OpenXR runtime requirement.\n";
		return false;
	}

	std::cout << "D3D11 device feature level: 0x" << std::hex << d3d.featureLevel << std::dec << '\n';
	return true;
}

bool enumerateViewConfigurationViews(
		XrInstance instance, XrSystemId systemId, std::vector<XrViewConfigurationView>& views) {
	std::uint32_t count = 0;
	XrResult result = xrEnumerateViewConfigurationViews(
			instance, systemId, viewConfiguration, 0, &count, nullptr);
	if (XR_FAILED(result)) {
		printFailure("enumerating view configuration count", result);
		return false;
	}
	if (count == 0) {
		std::cerr << "OpenXR runtime reported no stereo views.\n";
		return false;
	}

	views.assign(count, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
	result = xrEnumerateViewConfigurationViews(
			instance, systemId, viewConfiguration, count, &count, views.data());
	if (XR_FAILED(result)) {
		printFailure("enumerating view configuration views", result);
		return false;
	}
	views.resize(count);
	return true;
}

bool chooseEnvironmentBlendMode(
		XrInstance instance, XrSystemId systemId, XrEnvironmentBlendMode& blendMode) {
	std::uint32_t count = 0;
	XrResult result = xrEnumerateEnvironmentBlendModes(
			instance, systemId, viewConfiguration, 0, &count, nullptr);
	if (XR_FAILED(result)) {
		printFailure("enumerating environment blend mode count", result);
		return false;
	}
	if (count == 0) {
		std::cerr << "OpenXR runtime reported no environment blend modes.\n";
		return false;
	}

	std::vector<XrEnvironmentBlendMode> modes(count);
	result = xrEnumerateEnvironmentBlendModes(
			instance, systemId, viewConfiguration, count, &count, modes.data());
	if (XR_FAILED(result)) {
		printFailure("enumerating environment blend modes", result);
		return false;
	}

	const auto opaque = std::find(modes.begin(), modes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
	blendMode = opaque != modes.end() ? XR_ENVIRONMENT_BLEND_MODE_OPAQUE : modes.front();
	return true;
}

bool chooseSwapchainFormat(XrSession session, std::int64_t& format) {
	std::uint32_t count = 0;
	XrResult result = xrEnumerateSwapchainFormats(session, 0, &count, nullptr);
	if (XR_FAILED(result)) {
		printFailure("enumerating swapchain format count", result);
		return false;
	}
	if (count == 0) {
		std::cerr << "OpenXR runtime reported no swapchain formats.\n";
		return false;
	}

	std::vector<std::int64_t> formats(count);
	result = xrEnumerateSwapchainFormats(session, count, &count, formats.data());
	if (XR_FAILED(result)) {
		printFailure("enumerating swapchain formats", result);
		return false;
	}

	const std::array<DXGI_FORMAT, 4> preferred{
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_FORMAT_B8G8R8A8_UNORM,
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
			DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
	};
	for (DXGI_FORMAT candidate : preferred) {
		const auto encoded = static_cast<std::int64_t>(candidate);
		if (std::find(formats.begin(), formats.end(), encoded) != formats.end()) {
			format = encoded;
			std::cout << "Swapchain format: " << format << '\n';
			return true;
		}
	}

	std::cerr << "No preferred D3D11 color swapchain format was available.\n";
	return false;
}

bool createSwapchain(
		XrSession session, ID3D11Device* device, const XrViewConfigurationView& config,
		std::int64_t format, SwapchainBundle& bundle) {
	bundle.width = config.recommendedImageRectWidth;
	bundle.height = config.recommendedImageRectHeight;
	if (bundle.width == 0 || bundle.height == 0 || config.recommendedSwapchainSampleCount == 0) {
		std::cerr << "OpenXR runtime reported an invalid recommended swapchain size.\n";
		return false;
	}

	XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
	createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	createInfo.format = format;
	createInfo.sampleCount = config.recommendedSwapchainSampleCount;
	createInfo.width = bundle.width;
	createInfo.height = bundle.height;
	createInfo.faceCount = 1;
	createInfo.arraySize = 1;
	createInfo.mipCount = 1;

	XrResult result = xrCreateSwapchain(session, &createInfo, &bundle.swapchain);
	if (XR_FAILED(result)) {
		printFailure("creating D3D11 swapchain", result);
		return false;
	}

	std::uint32_t imageCount = 0;
	result = xrEnumerateSwapchainImages(bundle.swapchain, 0, &imageCount, nullptr);
	if (XR_FAILED(result)) {
		printFailure("enumerating swapchain image count", result);
		return false;
	}

	bundle.images.assign(imageCount, XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
	result = xrEnumerateSwapchainImages(
			bundle.swapchain,
			imageCount,
			&imageCount,
			reinterpret_cast<XrSwapchainImageBaseHeader*>(bundle.images.data()));
	if (XR_FAILED(result)) {
		printFailure("enumerating D3D11 swapchain images", result);
		return false;
	}
	bundle.images.resize(imageCount);

	bundle.renderTargets.clear();
	bundle.renderTargets.reserve(imageCount);
	for (const XrSwapchainImageD3D11KHR& image : bundle.images) {
		D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
		viewDesc.Format = static_cast<DXGI_FORMAT>(format);
		viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		viewDesc.Texture2D.MipSlice = 0;

		ComPtr<ID3D11RenderTargetView> renderTarget;
		const HRESULT hr = device->CreateRenderTargetView(image.texture, &viewDesc, &renderTarget);
		if (FAILED(hr)) {
			printHresult("creating D3D11 render target view", hr);
			return false;
		}
		bundle.renderTargets.push_back(renderTarget);
	}

	return true;
}

void destroySwapchains(std::vector<SwapchainBundle>& swapchains) {
	for (SwapchainBundle& bundle : swapchains) {
		if (bundle.swapchain != XR_NULL_HANDLE) {
			xrDestroySwapchain(bundle.swapchain);
			bundle.swapchain = XR_NULL_HANDLE;
		}
		bundle.renderTargets.clear();
		bundle.images.clear();
	}
	swapchains.clear();
}

bool clearSwapchainImage(const SwapchainBundle& bundle, ID3D11DeviceContext* context) {
	XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
	std::uint32_t imageIndex = 0;
	XrResult result = xrAcquireSwapchainImage(bundle.swapchain, &acquireInfo, &imageIndex);
	if (XR_FAILED(result)) {
		printFailure("acquiring swapchain image", result);
		return false;
	}

	XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
	waitInfo.timeout = XR_INFINITE_DURATION;
	result = xrWaitSwapchainImage(bundle.swapchain, &waitInfo);
	if (XR_FAILED(result)) {
		printFailure("waiting for swapchain image", result);
		XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
		xrReleaseSwapchainImage(bundle.swapchain, &releaseInfo);
		return false;
	}
	if (imageIndex >= bundle.renderTargets.size()) {
		std::cerr << "OpenXR returned swapchain image index " << imageIndex
				  << " outside the render target list.\n";
		XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
		xrReleaseSwapchainImage(bundle.swapchain, &releaseInfo);
		return false;
	}

	const FLOAT clearColor[4]{0.0F, 0.0F, 0.0F, 1.0F};
	context->ClearRenderTargetView(bundle.renderTargets[imageIndex].Get(), clearColor);
	context->Flush();

	XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
	result = xrReleaseSwapchainImage(bundle.swapchain, &releaseInfo);
	if (XR_FAILED(result)) {
		printFailure("releasing swapchain image", result);
		return false;
	}

	return true;
}

bool pollEvents(
		XrInstance instance, XrSession session, bool& sessionRunning,
		XrSessionState& sessionState, bool& shouldExit) {
	XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
	XrResult result = xrPollEvent(instance, &event);
	while (result == XR_SUCCESS) {
		switch (event.type) {
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
			const auto& stateChanged = reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
			sessionState = stateChanged.state;
			std::cout << "Session state: " << sessionStateToString(sessionState) << '\n';

			if (sessionState == XR_SESSION_STATE_READY) {
				XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
				beginInfo.primaryViewConfigurationType = viewConfiguration;
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
				}
				sessionRunning = false;
			} else if (sessionState == XR_SESSION_STATE_EXITING
					|| sessionState == XR_SESSION_STATE_LOSS_PENDING) {
				shouldExit = true;
			}
			break;
		}
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			std::cerr << "OpenXR runtime reported instance loss pending.\n";
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
		printFailure("polling OpenXR events", result);
		return false;
	}

	return true;
}

bool locateViews(
		XrSession session, XrSpace localSpace, XrTime displayTime,
		std::vector<XrView>& views, XrViewState& viewState) {
	for (XrView& view : views) {
		view = XrView{XR_TYPE_VIEW};
	}
	viewState = XrViewState{XR_TYPE_VIEW_STATE};

	XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
	locateInfo.viewConfigurationType = viewConfiguration;
	locateInfo.displayTime = displayTime;
	locateInfo.space = localSpace;

	std::uint32_t outputCount = 0;
	const XrResult result = xrLocateViews(
			session,
			&locateInfo,
			&viewState,
			static_cast<std::uint32_t>(views.size()),
			&outputCount,
			views.data());
	if (XR_FAILED(result)) {
		printFailure("locating HMD views", result);
		return false;
	}
	if (outputCount != views.size()) {
		std::cerr << "OpenXR returned " << outputCount << " view(s), expected " << views.size() << ".\n";
		return false;
	}

	return true;
}

bool finiteQuaternion(const XrQuaternionf& orientation) {
	return std::isfinite(orientation.x)
			&& std::isfinite(orientation.y)
			&& std::isfinite(orientation.z)
			&& std::isfinite(orientation.w);
}

std::int64_t timestampNanos() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string makeHmdDatagram(XrQuaternionf orientation, bool active) {
	if (!active || !finiteQuaternion(orientation)) {
		active = false;
		orientation = XrQuaternionf{0.0F, 0.0F, 0.0F, 1.0F};
	}

	std::ostringstream json;
	json.imbue(std::locale::classic());
	json << std::setprecision(9)
		 << "{\"version\":1,\"timestamp\":" << timestampNanos()
		 << ",\"hmd\":{\"rotation\":["
		 << orientation.x << ','
		 << orientation.y << ','
		 << orientation.z << ','
		 << orientation.w << "],\"active\":"
		 << (active ? "true" : "false")
		 << "}}";
	return json.str();
}

class UdpSender {
public:
	UdpSender() = default;

	UdpSender(const UdpSender&) = delete;
	UdpSender& operator=(const UdpSender&) = delete;

	~UdpSender() {
		close();
	}

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

		target_ = sockaddr_in{};
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
				socket_,
				payload.data(),
				static_cast<int>(payload.size()),
				0,
				reinterpret_cast<const sockaddr*>(&target_),
				static_cast<int>(sizeof(target_)));
		return sent == static_cast<int>(payload.size());
	}

	std::uint16_t port() const {
		return port_;
	}

	static void printSocketError(std::string_view operation) {
		std::cerr << "MCXRInput OpenXR bridge: " << operation
				  << " failed (Winsock error " << WSAGetLastError() << ").\n";
	}

private:
	void close() {
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
	std::uint16_t port_{defaultUdpPort};
};

void printBridgeStatus(
		std::uint64_t sentFrames, std::uint64_t activeFrames, std::uint64_t inactiveFrames,
		const XrQuaternionf& lastOrientation, bool lastActive) {
	std::cout << "UDP frames sent=" << sentFrames
			  << " active=" << activeFrames
			  << " inactive=" << inactiveFrames
			  << " last=" << (lastActive ? "active" : "inactive")
			  << " rotation=(" << lastOrientation.x << ' '
			  << lastOrientation.y << ' '
			  << lastOrientation.z << ' '
			  << lastOrientation.w << ")\n";
}

} // namespace

int main(int argc, char** argv) {
	std::cout << "MCXRInput OpenXR HMD UDP bridge\n";
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

	UdpSender sender;
	if (!sender.open(options.port)) {
		return static_cast<int>(ExitCode::network);
	}
	std::cout << "Sending protocol v1 HMD poses to "
			  << loopbackAddress << ':' << sender.port() << '\n';

	XrInstance instance = XR_NULL_HANDLE;
	XrSession session = XR_NULL_HANDLE;
	XrSpace localSpace = XR_NULL_HANDLE;
	std::vector<SwapchainBundle> swapchains;

	auto cleanup = [&]() {
		destroySwapchains(swapchains);
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
		std::cerr << "SteamVR did not advertise " << XR_KHR_D3D11_ENABLE_EXTENSION_NAME << ".\n";
		cleanup();
		return static_cast<int>(ExitCode::openXrRuntime);
	}

	XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	copyText("MCXRInput", createInfo.applicationInfo.applicationName);
	createInfo.applicationInfo.applicationVersion = 1;
	copyText("MCXRInput native bridge", createInfo.applicationInfo.engineName);
	createInfo.applicationInfo.engineVersion = 1;
	// OpenXR 1.0 is enough for this bridge and matches the proven probes. Asking
	// for the newest header API version can make SteamVR reject instance creation.
	createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);

	const char* enabledExtensions[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
	createInfo.enabledExtensionCount = static_cast<std::uint32_t>(std::size(enabledExtensions));
	createInfo.enabledExtensionNames = enabledExtensions;

	XrResult result = xrCreateInstance(&createInfo, &instance);
	if (XR_FAILED(result)) {
		printFailure("creating an OpenXR instance", result);
		std::cerr << "Check that SteamVR is installed, running, and selected as the OpenXR runtime.\n";
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
		printFailure("requesting a head-mounted display system", result);
		std::cerr << "No OpenXR HMD is available. Confirm SteamVR sees the headset.\n";
		cleanup();
		return static_cast<int>(ExitCode::noHeadset);
	}

	PFN_xrGetD3D11GraphicsRequirementsKHR getD3D11Requirements = nullptr;
	if (!loadD3D11RequirementsFunction(instance, getD3D11Requirements)) {
		cleanup();
		return static_cast<int>(ExitCode::openXrSession);
	}

	XrGraphicsRequirementsD3D11KHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
	result = getD3D11Requirements(instance, systemId, &graphicsRequirements);
	if (XR_FAILED(result)) {
		printFailure("querying D3D11 graphics requirements", result);
		cleanup();
		return static_cast<int>(ExitCode::openXrSession);
	}

	D3DState d3d;
	if (!createD3D11Device(graphicsRequirements, d3d)) {
		cleanup();
		return static_cast<int>(ExitCode::openXrSession);
	}

	XrGraphicsBindingD3D11KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
	graphicsBinding.device = d3d.device.Get();

	XrSessionCreateInfo sessionCreateInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionCreateInfo.next = &graphicsBinding;
	sessionCreateInfo.systemId = systemId;
	result = xrCreateSession(instance, &sessionCreateInfo, &session);
	if (XR_FAILED(result)) {
		printFailure("creating D3D11 OpenXR session", result);
		cleanup();
		return static_cast<int>(ExitCode::openXrSession);
	}

	XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	spaceInfo.poseInReferenceSpace.orientation.w = 1.0F;
	result = xrCreateReferenceSpace(session, &spaceInfo, &localSpace);
	if (XR_FAILED(result)) {
		printFailure("creating local reference space", result);
		cleanup();
		return static_cast<int>(ExitCode::openXrSession);
	}

	std::vector<XrViewConfigurationView> viewConfigs;
	if (!enumerateViewConfigurationViews(instance, systemId, viewConfigs)) {
		cleanup();
		return static_cast<int>(ExitCode::openXrSession);
	}

	XrEnvironmentBlendMode blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	if (!chooseEnvironmentBlendMode(instance, systemId, blendMode)) {
		cleanup();
		return static_cast<int>(ExitCode::openXrSession);
	}

	std::int64_t swapchainFormat = 0;
	if (!chooseSwapchainFormat(session, swapchainFormat)) {
		cleanup();
		return static_cast<int>(ExitCode::openXrSession);
	}

	swapchains.resize(viewConfigs.size());
	for (std::size_t index = 0; index < viewConfigs.size(); ++index) {
		if (!createSwapchain(session, d3d.device.Get(), viewConfigs[index], swapchainFormat, swapchains[index])) {
			cleanup();
			return static_cast<int>(ExitCode::openXrSession);
		}
	}

	std::vector<XrView> views(viewConfigs.size(), XrView{XR_TYPE_VIEW});

	std::cout << "Bridge is ready. Press Ctrl+C to stop.\n"
			  << "The headset may show a blank dark MCXRInput app while SteamVR focuses this session.\n";

	bool sessionRunning = false;
	bool shouldExit = false;
	bool exitRequested = false;
	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
	std::uint64_t sentFrames = 0;
	std::uint64_t activeFrames = 0;
	std::uint64_t inactiveFrames = 0;
	XrQuaternionf lastOrientation{0.0F, 0.0F, 0.0F, 1.0F};
	bool lastActive = false;
	auto lastStatus = std::chrono::steady_clock::now();

	while (!shouldExit) {
		if (!pollEvents(instance, session, sessionRunning, sessionState, shouldExit)) {
			cleanup();
			return static_cast<int>(ExitCode::openXrSession);
		}
		if (shouldExit) {
			break;
		}
		if (stopRequested.load() && !exitRequested) {
			if (sessionRunning) {
				const XrResult exitResult = xrRequestExitSession(session);
				if (XR_FAILED(exitResult)) {
					printFailure("requesting OpenXR session exit", exitResult);
				}
			}
			exitRequested = true;
			shouldExit = !sessionRunning;
		}
		if (!sessionRunning) {
			std::this_thread::sleep_for(std::chrono::milliseconds{10});
			continue;
		}

		XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
		XrFrameState frameState{XR_TYPE_FRAME_STATE};
		result = xrWaitFrame(session, &waitInfo, &frameState);
		if (XR_FAILED(result)) {
			printFailure("waiting for OpenXR frame", result);
			cleanup();
			return static_cast<int>(ExitCode::openXrSession);
		}

		XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
		result = xrBeginFrame(session, &beginInfo);
		if (XR_FAILED(result)) {
			printFailure("beginning OpenXR frame", result);
			cleanup();
			return static_cast<int>(ExitCode::openXrSession);
		}

		XrViewState viewState{XR_TYPE_VIEW_STATE};
		const bool viewsLocated = locateViews(session, localSpace, frameState.predictedDisplayTime, views, viewState);
		bool orientationActive = viewsLocated
				&& !views.empty()
				&& (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
		XrQuaternionf hmdOrientation = orientationActive ? views.front().pose.orientation
														 : XrQuaternionf{0.0F, 0.0F, 0.0F, 1.0F};
		if (!finiteQuaternion(hmdOrientation)) {
			orientationActive = false;
			hmdOrientation = XrQuaternionf{0.0F, 0.0F, 0.0F, 1.0F};
		}

		const std::string datagram = makeHmdDatagram(hmdOrientation, orientationActive);
		if (!sender.send(datagram)) {
			UdpSender::printSocketError("sending UDP datagram");
			cleanup();
			return static_cast<int>(ExitCode::network);
		}
		++sentFrames;
		if (orientationActive) {
			++activeFrames;
		} else {
			++inactiveFrames;
		}
		lastOrientation = hmdOrientation;
		lastActive = orientationActive;

		std::vector<XrCompositionLayerProjectionView> projectionViews(views.size());
		bool layerReady = frameState.shouldRender == XR_TRUE
				&& viewsLocated
				&& views.size() == swapchains.size();
		if (layerReady) {
			for (std::size_t index = 0; index < views.size(); ++index) {
				if (swapchains[index].renderTargets.empty()) {
					layerReady = false;
					break;
				}
				if (!clearSwapchainImage(swapchains[index], d3d.context.Get())) {
					layerReady = false;
					break;
				}

				XrCompositionLayerProjectionView projectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
				projectionView.pose = views[index].pose;
				projectionView.fov = views[index].fov;
				projectionView.subImage.swapchain = swapchains[index].swapchain;
				projectionView.subImage.imageRect.offset = {0, 0};
				projectionView.subImage.imageRect.extent = {
						static_cast<std::int32_t>(swapchains[index].width),
						static_cast<std::int32_t>(swapchains[index].height),
				};
				projectionViews[index] = projectionView;
			}
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
		endFrameInfo.environmentBlendMode = blendMode;
		endFrameInfo.layerCount = layerReady ? 1U : 0U;
		endFrameInfo.layers = layerReady ? layers : nullptr;
		result = xrEndFrame(session, &endFrameInfo);
		if (XR_FAILED(result)) {
			printFailure("ending OpenXR frame", result);
			cleanup();
			return static_cast<int>(ExitCode::openXrSession);
		}

		const auto now = std::chrono::steady_clock::now();
		if (now - lastStatus >= statusInterval) {
			printBridgeStatus(sentFrames, activeFrames, inactiveFrames, lastOrientation, lastActive);
			lastStatus = now;
		}
	}

	cleanup();
	std::cout << "\nOpenXR HMD UDP bridge stopped cleanly.\n";
	return static_cast<int>(ExitCode::success);
}
