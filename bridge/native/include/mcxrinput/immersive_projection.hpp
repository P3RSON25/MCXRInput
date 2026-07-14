#pragma once

#include <mcxrinput/openxr_d3d11.hpp>
#include <mcxrinput/half_sbs_renderer.hpp>
#include <mcxrinput/projection_math.hpp>

#include <array>

namespace mcxrinput::native {

// Match the fixed guard proven by the bounded immersive-capture hardware probe.
// Callers freeze this margin once; it must never become frame-dependent zoom.
inline constexpr float immersiveProjectionCalibrationGuardDegrees = 0.25F;

enum class ImmersiveProjectionBuildResult {
	success,
	invalidPoseOrFov,
	openXrFailure,
	sessionLossPending,
	eyePlaneCrossing,
	insufficientSourceFov,
	frozenFovExceeded,
	sourceAspectChanged,
};

struct ImmersiveProjectionCalibration {
	bool initialized{false};
	float sourceAspect{0.0F};
	float sourceVerticalFovDegrees{0.0F};
	float worldViewScale{1.0F};
	HalfSbsFitMode fit{HalfSbsFitMode::cover};
	std::array<ProjectionFov, 2> fovs{};
	std::array<SourceUvTransform, 2> sourceMappings{};
	// Frozen source region seen by the aligned physical runtime eye. HUD inset
	// margins conservatively account for ordinary roll; rendering continues to
	// use the larger stabilization-canvas sourceMappings above.
	std::array<SourceUvTransform, 2> physicalViewSourceMappings{};
};

struct ImmersiveProjectionFitDiagnostics {
	bool valid{false};
	std::array<float, 2> requiredSourceVerticalFovDegrees{};
	std::array<MaximumRollCoverage, 2> maximumRollCoverages{};
};

/**
 * A successfully accepted projection frame. centerViewPose is the same tracked
 * VIEW-in-LOCAL pose used to compose projectionViews. Keeping it in this
 * transaction lets a bridge publish HMD input from the exact accepted sample.
 */
struct ImmersiveProjectionFrame {
	XrPosef centerViewPose{};
	std::array<XrCompositionLayerProjectionView, 2> projectionViews{
			XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
			XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
	};
	std::array<SourceUvTransform, 2> sourceMappings{};
	std::array<SourceUvTransform, 2> physicalViewSourceMappings{};
};

/**
 * Pure/testable half of the immersive projection adapter. Inputs must be the
 * tracked center VIEW pose in LOCAL and the runtime stereo views relative to
 * VIEW for one predicted display time. State, calibration, and output are
 * committed together only on success. Fit diagnostics intentionally remain
 * available after insufficientSourceFov so callers can print a safe limit.
 */
ImmersiveProjectionBuildResult buildImmersiveProjectionFromLocatedViews(
		const XrPosef& centerViewPose,
		const std::array<XrView, 2>& relativeViews,
		float rollCoverageDegrees,
		HalfSbsFitMode fit,
		float sourceAspect,
		float sourceVerticalFovDegrees,
		float worldViewScale,
		RollFreeBasisState& basisState,
		ImmersiveProjectionCalibration& calibration,
		ImmersiveProjectionFitDiagnostics& fitDiagnostics,
		const std::array<SwapchainBundle, 2>& swapchains,
		ImmersiveProjectionFrame& output) noexcept;

/**
 * Locates the center and both eyes at one predicted display time, requires
 * VALID+TRACKED orientation and position, then delegates to the transactional
 * projection builder above. External instance/session/D3D lifetime remains
 * entirely caller-owned; this function creates or destroys no OpenXR object.
 */
ImmersiveProjectionBuildResult locateAndBuildImmersiveProjection(
		XrSession session,
		XrSpace viewSpace,
		XrSpace localSpace,
		XrTime displayTime,
		float rollCoverageDegrees,
		HalfSbsFitMode fit,
		float sourceAspect,
		float sourceVerticalFovDegrees,
		float worldViewScale,
		RollFreeBasisState& basisState,
		ImmersiveProjectionCalibration& calibration,
		ImmersiveProjectionFitDiagnostics& fitDiagnostics,
		const std::array<SwapchainBundle, 2>& swapchains,
		ImmersiveProjectionFrame& output);

} // namespace mcxrinput::native
