#pragma once

#include <cstdint>

#include <mcxrinput/projection_math.hpp>

#include <d3d11.h>
#include <wrl/client.h>

namespace mcxrinput::native {

class SwapchainImageLease;

enum class HalfSbsFitMode {
	// Preserve the complete decoded eye with bars where target aspect differs.
	contain,
	// Fill without distortion by cropping the decoded eye around its center.
	cover,
	// Fill with the complete decoded eye, accepting aspect distortion.
	stretch,
};

struct HalfSbsEyePresentation {
	HalfSbsFitMode fit{HalfSbsFitMode::contain};
	// Cover mode uses a caller-validated target-frustum-to-source mapping.
	// It is ignored by contain/stretch.
	SourceUvTransform sourceUv{};
	bool hasSourceUv{false};
};

struct HalfSbsRenderOptions {
	HalfSbsEyePresentation left{};
	HalfSbsEyePresentation right{};
};

/**
 * Decodes a horizontally squeezed half-SBS capture into two OpenXR render
 * targets. All methods must be called on the thread that owns the immediate
 * D3D11 context; this class does not create worker threads or retain captures.
 */
class HalfSbsRenderer {
public:
	HalfSbsRenderer() = default;
	~HalfSbsRenderer() = default;

	HalfSbsRenderer(const HalfSbsRenderer&) = delete;
	HalfSbsRenderer& operator=(const HalfSbsRenderer&) = delete;

	bool initialize(ID3D11Device* device);
	void reset() noexcept;
	[[nodiscard]] bool initialized() const noexcept;

	/**
	 * Renders into two already-acquired, single-sample swapchain images. The
	 * source must be an even-width BGRA8_TYPELESS texture owned by the same D3D11
	 * device and created with D3D11_BIND_SHADER_RESOURCE. It is viewed as sRGB so
	 * desktop pixels are decoded to linear light before filtering and output.
	 *
	 * This method deliberately does not release either lease. On success it
	 * unbinds the capture and render targets and flushes the immediate context
	 * once, after which the caller should release both OpenXR images promptly.
	 */
	bool render(
			ID3D11DeviceContext* context,
			ID3D11Texture2D* bgraSource,
			std::uint32_t sourceWidth,
			std::uint32_t sourceHeight,
			SwapchainImageLease& leftImage,
			SwapchainImageLease& rightImage,
			const HalfSbsRenderOptions& options = {});

private:
	Microsoft::WRL::ComPtr<ID3D11Device> device_;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
	Microsoft::WRL::ComPtr<ID3D11Buffer> eyeConstants_;
	Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
	Microsoft::WRL::ComPtr<ID3D11BlendState> blendState_;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthStencilState_;
	// The capture producer normally reuses one owned texture. Cache its view so
	// steady-state rendering does not allocate a COM object every XR frame.
	Microsoft::WRL::ComPtr<ID3D11Texture2D> cachedSource_;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cachedSourceView_;
	std::uint32_t cachedSourceWidth_{0};
	std::uint32_t cachedSourceHeight_{0};
};

} // namespace mcxrinput::native
