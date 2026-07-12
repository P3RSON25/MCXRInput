#include <mcxrinput/openxr_d3d11.hpp>

#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace mcxrinput::native {

SwapchainBundle::~SwapchainBundle() {
	reset();
}

SwapchainBundle::SwapchainBundle(SwapchainBundle&& other) noexcept {
	*this = std::move(other);
}

SwapchainBundle& SwapchainBundle::operator=(SwapchainBundle&& other) noexcept {
	if (this == &other) {
		return *this;
	}
	reset();
	swapchain = std::exchange(other.swapchain, XR_NULL_HANDLE);
	width = std::exchange(other.width, 0);
	height = std::exchange(other.height, 0);
	format = std::exchange(other.format, 0);
	sampleCount = std::exchange(other.sampleCount, 0);
	arraySize = std::exchange(other.arraySize, 0);
	usageFlags = std::exchange(other.usageFlags, 0);
	images = std::move(other.images);
	renderTargets = std::move(other.renderTargets);
	return *this;
}

void SwapchainBundle::reset() noexcept {
	// D3D views reference runtime-owned textures and must be released before the
	// OpenXR swapchain that owns those textures.
	renderTargets.clear();
	images.clear();
	if (swapchain != XR_NULL_HANDLE) {
		xrDestroySwapchain(swapchain);
		swapchain = XR_NULL_HANDLE;
	}
	width = 0;
	height = 0;
	format = 0;
	sampleCount = 0;
	arraySize = 0;
	usageFlags = 0;
}

SwapchainImageLease::~SwapchainImageLease() {
	if (acquired()) {
		XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
		const XrResult result = xrReleaseSwapchainImage(swapchain_, &releaseInfo);
		if (XR_FAILED(result)) {
			printFailure("releasing scoped swapchain image", result);
		}
	}
}

bool SwapchainImageLease::acquire(const SwapchainBundle& bundle) {
	if (acquired()) {
		std::cerr << "Cannot acquire an OpenXR swapchain image while another image is leased.\n";
		return false;
	}

	XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
	XrResult result = xrAcquireSwapchainImage(bundle.swapchain, &acquireInfo, &imageIndex_);
	if (XR_FAILED(result)) {
		printFailure("acquiring swapchain image", result);
		return false;
	}
	swapchain_ = bundle.swapchain;

	XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
	waitInfo.timeout = XR_INFINITE_DURATION;
	result = xrWaitSwapchainImage(swapchain_, &waitInfo);
	if (XR_FAILED(result)) {
		printFailure("waiting for swapchain image", result);
		// OpenXR only permits release after a successful wait. The caller treats
		// this as fatal and destroys the session, which abandons the acquisition.
		swapchain_ = XR_NULL_HANDLE;
		return false;
	}

	if (imageIndex_ >= bundle.images.size() || imageIndex_ >= bundle.renderTargets.size()) {
		std::cerr << "OpenXR returned swapchain image index " << imageIndex_
				  << " outside the D3D11 image list.\n";
		XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
		xrReleaseSwapchainImage(swapchain_, &releaseInfo);
		swapchain_ = XR_NULL_HANDLE;
		return false;
	}

	texture_ = bundle.images[imageIndex_].texture;
	renderTarget_ = bundle.renderTargets[imageIndex_].Get();
	width_ = bundle.width;
	height_ = bundle.height;
	format_ = static_cast<DXGI_FORMAT>(bundle.format);
	sampleCount_ = bundle.sampleCount;
	return true;
}

bool SwapchainImageLease::release() {
	if (!acquired()) {
		return true;
	}

	XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
	const XrResult result = xrReleaseSwapchainImage(swapchain_, &releaseInfo);
	swapchain_ = XR_NULL_HANDLE;
	imageIndex_ = 0;
	texture_ = nullptr;
	renderTarget_ = nullptr;
	width_ = 0;
	height_ = 0;
	format_ = DXGI_FORMAT_UNKNOWN;
	sampleCount_ = 0;
	if (XR_FAILED(result)) {
		printFailure("releasing swapchain image", result);
		return false;
	}
	return true;
}

bool SwapchainImageLease::acquired() const noexcept {
	return swapchain_ != XR_NULL_HANDLE;
}

std::uint32_t SwapchainImageLease::imageIndex() const noexcept {
	return imageIndex_;
}

ID3D11Texture2D* SwapchainImageLease::texture() const noexcept {
	return texture_;
}

ID3D11RenderTargetView* SwapchainImageLease::renderTarget() const noexcept {
	return renderTarget_;
}

std::uint32_t SwapchainImageLease::width() const noexcept {
	return width_;
}

std::uint32_t SwapchainImageLease::height() const noexcept {
	return height_;
}

DXGI_FORMAT SwapchainImageLease::format() const noexcept {
	return format_;
}

std::uint32_t SwapchainImageLease::sampleCount() const noexcept {
	return sampleCount_;
}

GraphicsContextView graphicsContextView(const D3DState& state) noexcept {
	return GraphicsContextView{
			state.device.Get(),
			state.context.Get(),
			state.featureLevel,
			state.adapterLuid,
	};
}

FrameScope::~FrameScope() {
	if (active()) {
		endInternal(nullptr, 0, "ending abandoned OpenXR frame");
	}
}

bool FrameScope::begin(XrSession session, XrEnvironmentBlendMode blendMode) {
	if (active() || session == XR_NULL_HANDLE) {
		std::cerr << "Cannot begin an OpenXR frame from an invalid frame scope.\n";
		return false;
	}

	XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
	state_ = XrFrameState{XR_TYPE_FRAME_STATE};
	XrResult result = xrWaitFrame(session, &waitInfo, &state_);
	if (XR_FAILED(result)) {
		printFailure("waiting for OpenXR frame", result);
		return false;
	}

	XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
	result = xrBeginFrame(session, &beginInfo);
	if (XR_FAILED(result)) {
		printFailure("beginning OpenXR frame", result);
		return false;
	}

	session_ = session;
	blendMode_ = blendMode;
	return true;
}

bool FrameScope::end(
		const XrCompositionLayerBaseHeader* const* layers,
		std::uint32_t layerCount) {
	return endInternal(layers, layerCount, "ending OpenXR frame");
}

bool FrameScope::active() const noexcept {
	return session_ != XR_NULL_HANDLE;
}

const XrFrameState& FrameScope::state() const noexcept {
	return state_;
}

bool FrameScope::endInternal(
		const XrCompositionLayerBaseHeader* const* layers,
		std::uint32_t layerCount,
		std::string_view operation) {
	if (!active()) {
		std::cerr << "Cannot end an OpenXR frame that was not begun.\n";
		return false;
	}
	bool validLayers = true;
	if (layerCount != 0 && layers == nullptr) {
		std::cerr << "Cannot end an OpenXR frame with a null layer list.\n";
		layers = nullptr;
		layerCount = 0;
		validLayers = false;
	}

	XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
	endInfo.displayTime = state_.predictedDisplayTime;
	endInfo.environmentBlendMode = blendMode_;
	endInfo.layerCount = layerCount;
	endInfo.layers = layerCount == 0 ? nullptr : layers;
	const XrSession session = std::exchange(session_, XR_NULL_HANDLE);
	const XrResult result = xrEndFrame(session, &endInfo);
	if (XR_FAILED(result)) {
		printFailure(operation, result);
		return false;
	}
	return validLayers;
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

void printFailure(std::string_view operation, XrResult result) {
	std::cerr << "MCXRInput OpenXR bridge: " << operation << " failed ("
			  << resultToString(result) << ", " << static_cast<int>(result) << ").\n";
}

void printHresult(std::string_view operation, HRESULT hr) {
	std::cerr << "MCXRInput OpenXR bridge: " << operation << " failed (HRESULT 0x"
			  << std::hex << static_cast<std::uint32_t>(hr) << std::dec << ").\n";
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

namespace {

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

} // namespace

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
	const HRESULT hr = D3D11CreateDevice(
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
	d3d.adapterLuid = requirements.adapterLuid;

	std::cout << "D3D11 device feature level: 0x" << std::hex << d3d.featureLevel << std::dec << '\n';
	return true;
}

bool enumerateViewConfigurationViews(
		XrInstance instance, XrSystemId systemId, std::vector<XrViewConfigurationView>& views) {
	std::uint32_t count = 0;
	XrResult result = xrEnumerateViewConfigurationViews(
			instance, systemId, primaryStereoViewConfiguration, 0, &count, nullptr);
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
			instance, systemId, primaryStereoViewConfiguration, count, &count, views.data());
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
			instance, systemId, primaryStereoViewConfiguration, 0, &count, nullptr);
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
			instance, systemId, primaryStereoViewConfiguration, count, &count, modes.data());
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

bool createColorSwapchain(
		XrSession session, ID3D11Device* device,
		const ColorSwapchainDescription& description, SwapchainBundle& bundle) {
	bundle.reset();
	if (session == XR_NULL_HANDLE || device == nullptr
			|| description.width == 0 || description.height == 0
			|| description.sampleCount == 0 || description.arraySize == 0
			|| description.usageFlags == 0) {
		std::cerr << "Cannot create an OpenXR color swapchain from an invalid description.\n";
		return false;
	}
	bundle.width = description.width;
	bundle.height = description.height;
	bundle.format = description.format;
	bundle.sampleCount = description.sampleCount;
	bundle.arraySize = description.arraySize;
	bundle.usageFlags = description.usageFlags;

	XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
	createInfo.usageFlags = description.usageFlags;
	createInfo.format = description.format;
	createInfo.sampleCount = description.sampleCount;
	createInfo.width = bundle.width;
	createInfo.height = bundle.height;
	createInfo.faceCount = 1;
	createInfo.arraySize = description.arraySize;
	createInfo.mipCount = 1;

	XrResult result = xrCreateSwapchain(session, &createInfo, &bundle.swapchain);
	if (XR_FAILED(result)) {
		printFailure("creating D3D11 swapchain", result);
		bundle.reset();
		return false;
	}

	std::uint32_t imageCount = 0;
	result = xrEnumerateSwapchainImages(bundle.swapchain, 0, &imageCount, nullptr);
	if (XR_FAILED(result) || imageCount == 0) {
		if (XR_FAILED(result)) {
			printFailure("enumerating swapchain image count", result);
		} else {
			std::cerr << "OpenXR returned no D3D11 swapchain images.\n";
		}
		bundle.reset();
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
		bundle.reset();
		return false;
	}
	bundle.images.resize(imageCount);

	bundle.renderTargets.reserve(imageCount);
	for (const XrSwapchainImageD3D11KHR& image : bundle.images) {
		D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
		viewDesc.Format = static_cast<DXGI_FORMAT>(description.format);
		if (description.arraySize == 1 && description.sampleCount == 1) {
			viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			viewDesc.Texture2D.MipSlice = 0;
		} else if (description.arraySize == 1) {
			viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
		} else if (description.sampleCount == 1) {
			viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			viewDesc.Texture2DArray.MipSlice = 0;
			viewDesc.Texture2DArray.FirstArraySlice = 0;
			viewDesc.Texture2DArray.ArraySize = description.arraySize;
		} else {
			viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
			viewDesc.Texture2DMSArray.FirstArraySlice = 0;
			viewDesc.Texture2DMSArray.ArraySize = description.arraySize;
		}

		ComPtr<ID3D11RenderTargetView> renderTarget;
		const HRESULT hr = device->CreateRenderTargetView(image.texture, &viewDesc, &renderTarget);
		if (FAILED(hr)) {
			printHresult("creating D3D11 render target view", hr);
			bundle.reset();
			return false;
		}
		bundle.renderTargets.push_back(renderTarget);
	}

	return true;
}

bool createSwapchain(
		XrSession session, ID3D11Device* device, const XrViewConfigurationView& config,
		std::int64_t format, SwapchainBundle& bundle) {
	return createColorSwapchain(
			session,
			device,
			ColorSwapchainDescription{
					config.recommendedImageRectWidth,
					config.recommendedImageRectHeight,
					format,
					config.recommendedSwapchainSampleCount,
					1,
					XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
			},
			bundle);
}

bool clearSwapchainImage(const SwapchainBundle& bundle, ID3D11DeviceContext* context) {
	SwapchainImageLease image;
	if (!image.acquire(bundle)) {
		return false;
	}

	const FLOAT clearColor[4]{0.0F, 0.0F, 0.0F, 1.0F};
	context->ClearRenderTargetView(image.renderTarget(), clearColor);
	context->Flush();
	return image.release();
}

} // namespace mcxrinput::native
