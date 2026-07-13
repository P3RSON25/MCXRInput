#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>

#include <d3d11.h>
#include <dxgi1_6.h>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mcxrinput::native {

inline constexpr XrViewConfigurationType primaryStereoViewConfiguration =
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

struct D3DState {
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL featureLevel{};
	LUID adapterLuid{};
};

/**
 * Non-owning device access for renderers that share the OpenXR adapter. The
 * immediate context remains confined to the OpenXR frame thread; asynchronous
 * capture callbacks may publish frames but must not issue D3D commands through it.
 */
struct GraphicsContextView {
	ID3D11Device* device{nullptr};
	ID3D11DeviceContext* immediateContext{nullptr};
	D3D_FEATURE_LEVEL featureLevel{};
	LUID adapterLuid{};
};

GraphicsContextView graphicsContextView(const D3DState& state) noexcept;

struct ColorSwapchainDescription {
	std::uint32_t width{0};
	std::uint32_t height{0};
	std::int64_t format{0};
	std::uint32_t sampleCount{1};
	std::uint32_t arraySize{1};
	XrSwapchainUsageFlags usageFlags{XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT};
};

/** Owns one OpenXR swapchain and the D3D11 views created for its images. */
class SwapchainBundle {
public:
	SwapchainBundle() = default;
	~SwapchainBundle();

	SwapchainBundle(const SwapchainBundle&) = delete;
	SwapchainBundle& operator=(const SwapchainBundle&) = delete;
	SwapchainBundle(SwapchainBundle&& other) noexcept;
	SwapchainBundle& operator=(SwapchainBundle&& other) noexcept;

	void reset() noexcept;

	XrSwapchain swapchain{XR_NULL_HANDLE};
	std::uint32_t width{0};
	std::uint32_t height{0};
	std::int64_t format{0};
	std::uint32_t sampleCount{0};
	std::uint32_t arraySize{0};
	XrSwapchainUsageFlags usageFlags{0};
	std::vector<XrSwapchainImageD3D11KHR> images;
	std::vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>> renderTargets;
};

/**
 * Owns one acquired OpenXR swapchain image until it is explicitly released or
 * the lease leaves scope. Future renderers can safely draw into renderTarget()
 * without duplicating acquire/wait/release error paths. Borrowed image pointers
 * are valid only while this lease remains acquired.
 */
class SwapchainImageLease {
public:
	SwapchainImageLease() = default;
	~SwapchainImageLease();

	SwapchainImageLease(const SwapchainImageLease&) = delete;
	SwapchainImageLease& operator=(const SwapchainImageLease&) = delete;

	bool acquire(const SwapchainBundle& bundle);
	bool release();

	[[nodiscard]] bool acquired() const noexcept;
	[[nodiscard]] std::uint32_t imageIndex() const noexcept;
	[[nodiscard]] ID3D11Texture2D* texture() const noexcept;
	[[nodiscard]] ID3D11RenderTargetView* renderTarget() const noexcept;
	[[nodiscard]] std::uint32_t width() const noexcept;
	[[nodiscard]] std::uint32_t height() const noexcept;
	[[nodiscard]] DXGI_FORMAT format() const noexcept;
	[[nodiscard]] std::uint32_t sampleCount() const noexcept;
	[[nodiscard]] XrResult acquireResult() const noexcept;
	[[nodiscard]] XrResult waitResult() const noexcept;
	[[nodiscard]] XrResult releaseResult() const noexcept;
	[[nodiscard]] bool resultsAreExactSuccess() const noexcept;
	[[nodiscard]] bool sessionLossPending() const noexcept;

private:
	XrSwapchain swapchain_{XR_NULL_HANDLE};
	std::uint32_t imageIndex_{0};
	ID3D11Texture2D* texture_{nullptr};
	ID3D11RenderTargetView* renderTarget_{nullptr};
	std::uint32_t width_{0};
	std::uint32_t height_{0};
	DXGI_FORMAT format_{DXGI_FORMAT_UNKNOWN};
	std::uint32_t sampleCount_{0};
	XrResult acquireResult_{XR_SUCCESS};
	XrResult waitResult_{XR_SUCCESS};
	XrResult releaseResult_{XR_SUCCESS};
};

/** Owns the required wait/begin/end sequence for exactly one OpenXR frame. */
class FrameScope {
public:
	FrameScope() = default;
	~FrameScope();

	FrameScope(const FrameScope&) = delete;
	FrameScope& operator=(const FrameScope&) = delete;

	bool begin(XrSession session, XrEnvironmentBlendMode blendMode);
	bool end(
			const XrCompositionLayerBaseHeader* const* layers,
			std::uint32_t layerCount);

	[[nodiscard]] bool active() const noexcept;
	[[nodiscard]] const XrFrameState& state() const noexcept;
	[[nodiscard]] XrResult waitResult() const noexcept;
	[[nodiscard]] XrResult beginResult() const noexcept;
	[[nodiscard]] XrResult endResult() const noexcept;
	[[nodiscard]] bool resultsAreExactSuccess() const noexcept;
	[[nodiscard]] bool sessionLossPending() const noexcept;

private:
	bool endInternal(
			const XrCompositionLayerBaseHeader* const* layers,
			std::uint32_t layerCount,
			std::string_view operation);

	XrSession session_{XR_NULL_HANDLE};
	XrEnvironmentBlendMode blendMode_{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
	XrFrameState state_{XR_TYPE_FRAME_STATE};
	XrResult waitResult_{XR_SUCCESS};
	XrResult beginResult_{XR_SUCCESS};
	XrResult endResult_{XR_SUCCESS};
};

std::string resultToString(XrResult result);
void printFailure(std::string_view operation, XrResult result);
void printHresult(std::string_view operation, HRESULT hr);

bool enumerateInstanceExtensions(std::vector<XrExtensionProperties>& extensions);
bool hasExtension(const std::vector<XrExtensionProperties>& extensions, std::string_view name);
bool loadD3D11RequirementsFunction(
		XrInstance instance, PFN_xrGetD3D11GraphicsRequirementsKHR& getRequirements);
bool createD3D11Device(const XrGraphicsRequirementsD3D11KHR& requirements, D3DState& d3d);
bool enumerateViewConfigurationViews(
		XrInstance instance, XrSystemId systemId, std::vector<XrViewConfigurationView>& views);
bool chooseEnvironmentBlendMode(
		XrInstance instance, XrSystemId systemId, XrEnvironmentBlendMode& blendMode);
bool chooseSwapchainFormat(XrSession session, std::int64_t& format);
/** Chooses the sRGB-first RGBA/BGRA ordering validated by live desktop capture. */
bool chooseCapturedDisplaySwapchainFormat(XrSession session, std::int64_t& format);
bool createColorSwapchain(
		XrSession session, ID3D11Device* device,
		const ColorSwapchainDescription& description, SwapchainBundle& bundle);
bool createSwapchain(
		XrSession session, ID3D11Device* device, const XrViewConfigurationView& config,
		std::int64_t format, SwapchainBundle& bundle);
bool clearSwapchainImage(
		const SwapchainBundle& bundle, ID3D11DeviceContext* context,
		bool* sessionLossPending = nullptr);
/** Flushes and waits for immediate-context work before capture/swapchain teardown. */
bool waitForD3D11GpuIdle(
		ID3D11Device* device, ID3D11DeviceContext* context,
		std::uint32_t timeoutMilliseconds = 2000);

} // namespace mcxrinput::native
