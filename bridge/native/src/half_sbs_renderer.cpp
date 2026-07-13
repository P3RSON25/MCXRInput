#include "mcxrinput/half_sbs_renderer.hpp"

#include "mcxrinput/openxr_d3d11.hpp"
#include "mcxrinput/projection_math.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <string_view>
#include <utility>

#include <d3dcompiler.h>

namespace mcxrinput::native {

using Microsoft::WRL::ComPtr;

namespace {

constexpr std::string_view vertexShaderSource = R"hlsl(
struct VertexOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VertexOutput main(uint vertexId : SV_VertexID) {
    VertexOutput output;
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
    output.uv = uv;
    return output;
}
)hlsl";

constexpr std::string_view pixelShaderSource = R"hlsl(
Texture2D<float4> captureTexture : register(t0);
SamplerState captureSampler : register(s0);

cbuffer EyeConstants : register(b0) {
    float4 uvTransform;
    float4 uvBounds;
};

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float2 sourceUv = uv * uvTransform.xy + uvTransform.zw;
    sourceUv = clamp(sourceUv, uvBounds.xy, uvBounds.zw);
    float4 color = captureTexture.Sample(captureSampler, sourceUv);
    return float4(color.rgb, 1.0f);
}
)hlsl";

struct EyeConstants {
	std::array<float, 4> uvTransform{};
	std::array<float, 4> uvBounds{};
};

static_assert(sizeof(EyeConstants) % 16 == 0);

bool compileShader(
		std::string_view source, const char* target, ComPtr<ID3DBlob>& bytecode) {
	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
	flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
	ComPtr<ID3DBlob> errors;
	const HRESULT result = D3DCompile(
			source.data(),
			source.size(),
			"MCXRInputHalfSbsRenderer",
			nullptr,
			nullptr,
			"main",
			target,
			flags,
			0,
			&bytecode,
			&errors);
	if (errors) {
		const auto* message = static_cast<const char*>(errors->GetBufferPointer());
		const std::size_t length = errors->GetBufferSize();
		std::cerr << "Half-SBS shader compiler: " << std::string_view{message, length};
		if (length == 0 || message[length - 1] != '\n') {
			std::cerr << '\n';
		}
	}
	if (FAILED(result)) {
		printHresult("compiling half-SBS D3D11 shader", result);
		return false;
	}
	return true;
}

bool sameDevice(ID3D11Device* expected, ID3D11Device* actual) {
	if (expected == nullptr || actual == nullptr) {
		return false;
	}
	ComPtr<IUnknown> expectedIdentity;
	ComPtr<IUnknown> actualIdentity;
	return SUCCEEDED(expected->QueryInterface(IID_PPV_ARGS(&expectedIdentity)))
			&& SUCCEEDED(actual->QueryInterface(IID_PPV_ARGS(&actualIdentity)))
			&& expectedIdentity.Get() == actualIdentity.Get();
}

bool isSupportedOutputFormat(DXGI_FORMAT format) {
	return format == DXGI_FORMAT_R8G8B8A8_UNORM
			|| format == DXGI_FORMAT_B8G8R8A8_UNORM
			|| format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
			|| format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
}

D3D11_VIEWPORT fittedViewport(
		std::uint32_t targetWidth, std::uint32_t targetHeight, float decodedAspect) {
	const float width = static_cast<float>(targetWidth);
	const float height = static_cast<float>(targetHeight);
	const float targetAspect = width / height;
	D3D11_VIEWPORT viewport{};
	if (targetAspect > decodedAspect) {
		viewport.Height = height;
		viewport.Width = height * decodedAspect;
		viewport.TopLeftX = (width - viewport.Width) * 0.5F;
	} else {
		viewport.Width = width;
		viewport.Height = width / decodedAspect;
		viewport.TopLeftY = (height - viewport.Height) * 0.5F;
	}
	viewport.MinDepth = 0.0F;
	viewport.MaxDepth = 1.0F;
	return viewport;
}

bool makeEyeConstants(
		bool leftEye, std::uint32_t sourceWidth, std::uint32_t sourceHeight,
		const HalfSbsEyePresentation& presentation, EyeConstants& output) {
	const float inverseWidth = 1.0F / static_cast<float>(sourceWidth);
	const float inverseHeight = 1.0F / static_cast<float>(sourceHeight);
	const float eyeOffset = leftEye ? 0.0F : 0.5F;
	SourceUvTransform sourceTransform;
	if (presentation.fit == HalfSbsFitMode::cover) {
		sourceTransform = presentation.sourceUv;
		const bool valid = presentation.hasSourceUv
				&& std::isfinite(sourceTransform.scaleX)
				&& std::isfinite(sourceTransform.scaleY)
				&& std::isfinite(sourceTransform.offsetX)
				&& std::isfinite(sourceTransform.offsetY)
				&& sourceTransform.scaleX > 0.0F && sourceTransform.scaleY > 0.0F
				&& sourceTransform.offsetX >= 0.0F && sourceTransform.offsetY >= 0.0F
				&& sourceTransform.offsetX + sourceTransform.scaleX <= 1.0F + 1.0e-5F
				&& sourceTransform.offsetY + sourceTransform.scaleY <= 1.0F + 1.0e-5F;
		if (!valid) {
			std::cerr << "Half-SBS cover mode received an invalid source-FOV mapping.\n";
			return false;
		}
	}
	EyeConstants constants;
	constants.uvTransform = {
			0.5F * sourceTransform.scaleX,
			sourceTransform.scaleY,
			eyeOffset + 0.5F * sourceTransform.offsetX,
			sourceTransform.offsetY,
	};
	// Clamp to texel centers so linear filtering cannot bleed across the seam
	// between eyes when the anamorphic source is enlarged.
	constants.uvBounds = {
			eyeOffset + inverseWidth * 0.5F,
			inverseHeight * 0.5F,
			eyeOffset + 0.5F - inverseWidth * 0.5F,
			1.0F - inverseHeight * 0.5F,
	};
	output = constants;
	return true;
}

void unbindCaptureAndTarget(ID3D11DeviceContext* context) {
	ID3D11ShaderResourceView* nullView = nullptr;
	context->PSSetShaderResources(0, 1, &nullView);
	context->OMSetRenderTargets(0, nullptr, nullptr);
}

} // namespace

bool HalfSbsRenderer::initialize(ID3D11Device* device) {
	reset();
	if (device == nullptr) {
		std::cerr << "Cannot initialize the half-SBS renderer without a D3D11 device.\n";
		return false;
	}

	ComPtr<ID3DBlob> vertexBytecode;
	ComPtr<ID3DBlob> pixelBytecode;
	if (!compileShader(vertexShaderSource, "vs_4_0", vertexBytecode)
			|| !compileShader(pixelShaderSource, "ps_4_0", pixelBytecode)) {
		return false;
	}

	HRESULT result = device->CreateVertexShader(
			vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
			nullptr, &vertexShader_);
	if (FAILED(result)) {
		printHresult("creating half-SBS vertex shader", result);
		reset();
		return false;
	}
	result = device->CreatePixelShader(
			pixelBytecode->GetBufferPointer(), pixelBytecode->GetBufferSize(),
			nullptr, &pixelShader_);
	if (FAILED(result)) {
		printHresult("creating half-SBS pixel shader", result);
		reset();
		return false;
	}

	D3D11_BUFFER_DESC bufferDescription{};
	bufferDescription.ByteWidth = static_cast<UINT>(sizeof(EyeConstants));
	bufferDescription.Usage = D3D11_USAGE_DEFAULT;
	bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	result = device->CreateBuffer(&bufferDescription, nullptr, &eyeConstants_);
	if (FAILED(result)) {
		printHresult("creating half-SBS eye constant buffer", result);
		reset();
		return false;
	}

	D3D11_SAMPLER_DESC samplerDescription{};
	samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
	result = device->CreateSamplerState(&samplerDescription, &sampler_);
	if (FAILED(result)) {
		printHresult("creating half-SBS sampler", result);
		reset();
		return false;
	}

	D3D11_RASTERIZER_DESC rasterizerDescription{};
	rasterizerDescription.FillMode = D3D11_FILL_SOLID;
	rasterizerDescription.CullMode = D3D11_CULL_NONE;
	rasterizerDescription.DepthClipEnable = TRUE;
	result = device->CreateRasterizerState(&rasterizerDescription, &rasterizer_);
	if (FAILED(result)) {
		printHresult("creating half-SBS rasterizer", result);
		reset();
		return false;
	}

	D3D11_BLEND_DESC blendDescription{};
	blendDescription.RenderTarget[0].BlendEnable = FALSE;
	blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	result = device->CreateBlendState(&blendDescription, &blendState_);
	if (FAILED(result)) {
		printHresult("creating half-SBS blend state", result);
		reset();
		return false;
	}

	D3D11_DEPTH_STENCIL_DESC depthStencilDescription{};
	depthStencilDescription.DepthEnable = FALSE;
	depthStencilDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depthStencilDescription.DepthFunc = D3D11_COMPARISON_ALWAYS;
	depthStencilDescription.StencilEnable = FALSE;
	result = device->CreateDepthStencilState(
			&depthStencilDescription, &depthStencilState_);
	if (FAILED(result)) {
		printHresult("creating half-SBS depth-stencil state", result);
		reset();
		return false;
	}

	device_ = device;
	return true;
}

void HalfSbsRenderer::reset() noexcept {
	cachedSourceView_.Reset();
	cachedSource_.Reset();
	cachedSourceWidth_ = 0;
	cachedSourceHeight_ = 0;
	depthStencilState_.Reset();
	blendState_.Reset();
	rasterizer_.Reset();
	sampler_.Reset();
	eyeConstants_.Reset();
	pixelShader_.Reset();
	vertexShader_.Reset();
	device_.Reset();
}

bool HalfSbsRenderer::initialized() const noexcept {
	return device_ && vertexShader_ && pixelShader_ && eyeConstants_ && sampler_ && rasterizer_
			&& blendState_ && depthStencilState_;
}

bool HalfSbsRenderer::render(
		ID3D11DeviceContext* context,
		ID3D11Texture2D* bgraSource,
		std::uint32_t sourceWidth,
		std::uint32_t sourceHeight,
		SwapchainImageLease& leftImage,
		SwapchainImageLease& rightImage,
		const HalfSbsRenderOptions& options) {
	if (!initialized() || context == nullptr || bgraSource == nullptr) {
		std::cerr << "Half-SBS renderer is missing its device, context, or capture texture.\n";
		return false;
	}
	if (!leftImage.acquired() || !rightImage.acquired()) {
		std::cerr << "Half-SBS renderer requires two acquired OpenXR swapchain images.\n";
		return false;
	}
	if (sourceWidth < 2 || sourceHeight == 0 || (sourceWidth & 1U) != 0) {
		std::cerr << "Half-SBS capture dimensions must be nonzero with an even width.\n";
		return false;
	}
	if (leftImage.width() == 0 || leftImage.height() == 0
			|| rightImage.width() == 0 || rightImage.height() == 0
			|| leftImage.texture() == nullptr || rightImage.texture() == nullptr
			|| leftImage.renderTarget() == nullptr || rightImage.renderTarget() == nullptr
			|| leftImage.sampleCount() != 1 || rightImage.sampleCount() != 1) {
		std::cerr << "Half-SBS targets must be nonempty, single-sample textures.\n";
		return false;
	}
	if (!isSupportedOutputFormat(leftImage.format())
			|| !isSupportedOutputFormat(rightImage.format())) {
		std::cerr << "Half-SBS renderer supports only RGBA8/BGRA8 UNORM or sRGB "
				  << "OpenXR targets.\n";
		return false;
	}

	D3D11_TEXTURE2D_DESC sourceDescription{};
	bgraSource->GetDesc(&sourceDescription);
	if (sourceDescription.Width != sourceWidth || sourceDescription.Height != sourceHeight
			|| sourceDescription.Format != DXGI_FORMAT_B8G8R8A8_TYPELESS
			|| sourceDescription.MipLevels != 1 || sourceDescription.ArraySize != 1
			|| sourceDescription.SampleDesc.Count != 1
			|| (sourceDescription.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0) {
		std::cerr << "Half-SBS source must exactly match the supplied dimensions and be a "
				  << "single-sample BGRA8_TYPELESS shader-resource texture.\n";
		return false;
	}

	ComPtr<ID3D11Device> contextDevice;
	ComPtr<ID3D11Device> sourceDevice;
	ComPtr<ID3D11Device> leftDevice;
	ComPtr<ID3D11Device> rightDevice;
	context->GetDevice(&contextDevice);
	bgraSource->GetDevice(&sourceDevice);
	leftImage.texture()->GetDevice(&leftDevice);
	rightImage.texture()->GetDevice(&rightDevice);
	if (!sameDevice(device_.Get(), contextDevice.Get())
			|| !sameDevice(device_.Get(), sourceDevice.Get())
			|| !sameDevice(device_.Get(), leftDevice.Get())
			|| !sameDevice(device_.Get(), rightDevice.Get())) {
		std::cerr << "Half-SBS capture and OpenXR targets must share the renderer's D3D11 device.\n";
		return false;
	}

	if (cachedSource_.Get() != bgraSource
			|| cachedSourceWidth_ != sourceWidth || cachedSourceHeight_ != sourceHeight) {
		D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
		// SDR desktop capture stores nonlinear display values. An sRGB view
		// converts them to linear light before filtering; sRGB output RTVs encode
		// again, while UNORM output RTVs retain the linear values expected by XR.
		viewDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		viewDescription.Texture2D.MostDetailedMip = 0;
		viewDescription.Texture2D.MipLevels = 1;
		ComPtr<ID3D11ShaderResourceView> replacementView;
		const HRESULT viewResult = device_->CreateShaderResourceView(
				bgraSource, &viewDescription, &replacementView);
		if (FAILED(viewResult)) {
			printHresult("creating half-SBS capture shader-resource view", viewResult);
			return false;
		}
		cachedSource_ = bgraSource;
		cachedSourceView_ = std::move(replacementView);
		cachedSourceWidth_ = sourceWidth;
		cachedSourceHeight_ = sourceHeight;
	}

	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->HSSetShader(nullptr, nullptr, 0);
	context->DSSetShader(nullptr, nullptr, 0);
	context->GSSetShader(nullptr, nullptr, 0);
	context->VSSetShader(vertexShader_.Get(), nullptr, 0);
	context->PSSetShader(pixelShader_.Get(), nullptr, 0);
	ID3D11Buffer* constants = eyeConstants_.Get();
	context->PSSetConstantBuffers(0, 1, &constants);
	ID3D11SamplerState* sampler = sampler_.Get();
	context->PSSetSamplers(0, 1, &sampler);
	ID3D11ShaderResourceView* captureView = cachedSourceView_.Get();
	context->PSSetShaderResources(0, 1, &captureView);
	context->RSSetState(rasterizer_.Get());
	const std::array<float, 4> blendFactor{0.0F, 0.0F, 0.0F, 0.0F};
	context->OMSetBlendState(blendState_.Get(), blendFactor.data(), 0xFFFFFFFFU);
	context->OMSetDepthStencilState(depthStencilState_.Get(), 0);
	context->SetPredication(nullptr, FALSE);

	const float decodedAspect = static_cast<float>(sourceWidth)
			/ static_cast<float>(sourceHeight);
	const std::array<float, 4> black{0.0F, 0.0F, 0.0F, 1.0F};
	bool constantsValid = true;
	const auto drawEye = [&](bool leftEye, SwapchainImageLease& image,
			const HalfSbsEyePresentation& presentation) {
		ID3D11RenderTargetView* target = image.renderTarget();
		context->OMSetRenderTargets(1, &target, nullptr);
		context->ClearRenderTargetView(target, black.data());
		D3D11_VIEWPORT viewport{};
		if (presentation.fit == HalfSbsFitMode::contain) {
			viewport = fittedViewport(image.width(), image.height(), decodedAspect);
		} else {
			viewport.Width = static_cast<float>(image.width());
			viewport.Height = static_cast<float>(image.height());
			viewport.MinDepth = 0.0F;
			viewport.MaxDepth = 1.0F;
		}
		context->RSSetViewports(1, &viewport);
		EyeConstants eye;
		if (!makeEyeConstants(
				leftEye, sourceWidth, sourceHeight, presentation, eye)) {
			constantsValid = false;
			return;
		}
		context->UpdateSubresource(eyeConstants_.Get(), 0, nullptr, &eye, 0, 0);
		context->Draw(3, 0);
	};
	drawEye(true, leftImage, options.left);
	if (constantsValid) {
		drawEye(false, rightImage, options.right);
	}

	unbindCaptureAndTarget(context);
	if (!constantsValid) {
		return false;
	}
	// Flush only after both eye draws so the caller can immediately release both
	// OpenXR image leases without exposing unsubmitted work to the runtime.
	context->Flush();
	return true;
}

} // namespace mcxrinput::native
