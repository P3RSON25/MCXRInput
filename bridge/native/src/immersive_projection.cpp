#include <mcxrinput/immersive_projection.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace mcxrinput::native {

namespace {

Pose toPose(const XrPosef& pose) {
	return Pose{
			Quaternion{pose.orientation.x, pose.orientation.y,
					pose.orientation.z, pose.orientation.w},
			Vec3{pose.position.x, pose.position.y, pose.position.z},
	};
}

XrPosef toXrPose(const Pose& pose) {
	XrPosef result{};
	result.orientation = XrQuaternionf{
			pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w};
	result.position = XrVector3f{pose.position.x, pose.position.y, pose.position.z};
	return result;
}

ProjectionFov toProjectionFov(const XrFovf& fov) {
	return ProjectionFov{fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown};
}

XrFovf toXrFov(ProjectionFov fov) {
	return XrFovf{fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown};
}

ImmersiveProjectionBuildResult locateRelativeViews(
		XrSession session, XrSpace viewSpace, XrTime displayTime,
		std::array<XrView, 2>& views, XrViewState& state) {
	views = {XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW}};
	state = XrViewState{XR_TYPE_VIEW_STATE};
	XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};
	info.viewConfigurationType = primaryStereoViewConfiguration;
	info.displayTime = displayTime;
	info.space = viewSpace;
	std::uint32_t outputCount = 0;
	const XrResult result = xrLocateViews(
			session, &info, &state, static_cast<std::uint32_t>(views.size()),
			&outputCount, views.data());
	if (result == XR_SESSION_LOSS_PENDING) {
		return ImmersiveProjectionBuildResult::sessionLossPending;
	}
	if (result != XR_SUCCESS) {
		printFailure("locating immersive stereo views", result);
		return ImmersiveProjectionBuildResult::openXrFailure;
	}
	if (outputCount != views.size()) {
		std::cerr << "OpenXR returned " << outputCount
				  << " immersive view(s), expected two.\n";
		return ImmersiveProjectionBuildResult::openXrFailure;
	}
	const XrViewStateFlags required = XR_VIEW_STATE_ORIENTATION_VALID_BIT
			| XR_VIEW_STATE_POSITION_VALID_BIT
			| XR_VIEW_STATE_ORIENTATION_TRACKED_BIT
			| XR_VIEW_STATE_POSITION_TRACKED_BIT;
	return (state.viewStateFlags & required) == required
			? ImmersiveProjectionBuildResult::success
			: ImmersiveProjectionBuildResult::invalidPoseOrFov;
}

} // namespace

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
		ImmersiveProjectionFrame& output) noexcept {
	const bool supportedFit = fit == HalfSbsFitMode::cover
			|| fit == HalfSbsFitMode::stretch;
	if (!supportedFit
			|| !std::isfinite(sourceVerticalFovDegrees)
			|| sourceVerticalFovDegrees < minimumSourceVerticalFovDegrees
			|| sourceVerticalFovDegrees > maximumSourceVerticalFovDegrees
			|| !std::isfinite(worldViewScale)
			|| worldViewScale < minimumWorldViewScale
			|| worldViewScale > maximumWorldViewScale
			|| (fit == HalfSbsFitMode::stretch
					&& worldViewScale != maximumWorldViewScale)) {
		return ImmersiveProjectionBuildResult::invalidPoseOrFov;
	}

	const std::array<Pose, 2> relativePoses{
			toPose(relativeViews[0].pose), toPose(relativeViews[1].pose)};

	std::array<ProjectionFov, 2> runtimeFovs{};
	std::array<ProjectionFov, 2> requiredFovs{};
	for (std::size_t index = 0; index < relativeViews.size(); ++index) {
		runtimeFovs[index] = toProjectionFov(relativeViews[index].fov);
		const CantedFovExpansionResult expansionResult =
				computeCantedFovForRollCoverage(
					runtimeFovs[index], relativePoses[index].orientation,
					rollCoverageDegrees, requiredFovs[index]);
		if (expansionResult == CantedFovExpansionResult::eyePlaneCrossing) {
			return ImmersiveProjectionBuildResult::eyePlaneCrossing;
		}
		if (expansionResult != CantedFovExpansionResult::success) {
			return ImmersiveProjectionBuildResult::invalidPoseOrFov;
		}
	}

	ImmersiveProjectionCalibration candidateCalibration = calibration;
	if (!candidateCalibration.initialized) {
		candidateCalibration.sourceAspect = sourceAspect;
		candidateCalibration.sourceVerticalFovDegrees = sourceVerticalFovDegrees;
		candidateCalibration.worldViewScale = worldViewScale;
		candidateCalibration.fit = fit;
		for (std::size_t index = 0; index < requiredFovs.size(); ++index) {
			if (!expandProjectionFovByAngularGuard(
					requiredFovs[index], immersiveProjectionCalibrationGuardDegrees,
					candidateCalibration.fovs[index])) {
				return ImmersiveProjectionBuildResult::invalidPoseOrFov;
			}
		}
		if (fit == HalfSbsFitMode::cover) {
			ImmersiveProjectionFitDiagnostics candidateFitDiagnostics;
			std::array<ProjectionFov, 2> samplingFovs{};
			for (std::size_t index = 0; index < requiredFovs.size(); ++index) {
				if (!expandProjectionFovForWorldViewScale(
						candidateCalibration.fovs[index], worldViewScale,
						samplingFovs[index])
						|| !computeMinimumSourceVerticalFovDegrees(
						sourceAspect, samplingFovs[index],
						candidateFitDiagnostics.requiredSourceVerticalFovDegrees[index])
						|| !computeMaximumSupportedRollCoverage(
							runtimeFovs[index], relativePoses[index].orientation,
							sourceAspect, sourceVerticalFovDegrees,
							immersiveProjectionCalibrationGuardDegrees,
							worldViewScale,
							candidateFitDiagnostics.maximumRollCoverages[index])) {
					return ImmersiveProjectionBuildResult::invalidPoseOrFov;
				}
			}
			candidateFitDiagnostics.valid = true;
			// Retain capacity data even when the configured mapping below fails, so
			// the caller can report a precise, non-distorting configuration.
			fitDiagnostics = candidateFitDiagnostics;
			for (std::size_t index = 0; index < requiredFovs.size(); ++index) {
				const SourceProjectionMappingResult mappingResult =
						computeProjectionSourceUvTransform(
							sourceAspect, sourceVerticalFovDegrees,
							samplingFovs[index],
							candidateCalibration.sourceMappings[index]);
				if (mappingResult == SourceProjectionMappingResult::insufficientSourceFov) {
					return ImmersiveProjectionBuildResult::insufficientSourceFov;
				}
				if (mappingResult != SourceProjectionMappingResult::success) {
					return ImmersiveProjectionBuildResult::invalidPoseOrFov;
				}

				// The render mapping above fills the larger roll-stabilization canvas.
				// At aligned physical roll, however, the runtime sees only its raw eye
				// frustum inside that canvas. Freeze that stricter source rectangle for
				// HUD placement without changing any rendered pixels.
				ProjectionFov physicalSamplingFov;
				if (!expandProjectionFovForWorldViewScale(
						runtimeFovs[index], worldViewScale, physicalSamplingFov)) {
					return ImmersiveProjectionBuildResult::invalidPoseOrFov;
				}
				const SourceProjectionMappingResult physicalMappingResult =
						computeProjectionSourceUvTransform(
							sourceAspect, sourceVerticalFovDegrees,
							physicalSamplingFov,
							candidateCalibration.physicalViewSourceMappings[index]);
				if (physicalMappingResult == SourceProjectionMappingResult::insufficientSourceFov) {
					return ImmersiveProjectionBuildResult::insufficientSourceFov;
				}
				if (physicalMappingResult != SourceProjectionMappingResult::success) {
					return ImmersiveProjectionBuildResult::invalidPoseOrFov;
				}
			}
		} else {
			// Stretch uses the complete source and therefore has no cropped edge.
			// Rendering remains identity, but the physical eye still sees only its
			// raw frustum inside the oversized stabilization canvas. Express that
			// sub-frustum directly because source-FOV math does not apply to stretch.
			for (std::size_t index = 0; index < requiredFovs.size(); ++index) {
				candidateCalibration.sourceMappings[index] =
						SourceUvTransform{1.0F, 1.0F, 0.0F, 0.0F};
				if (!computeProjectionSubFovUvTransform(
						candidateCalibration.fovs[index], runtimeFovs[index],
						candidateCalibration.physicalViewSourceMappings[index])) {
					return ImmersiveProjectionBuildResult::invalidPoseOrFov;
				}
			}
		}
		candidateCalibration.initialized = true;
	} else {
		const auto changed = [](float frozen, float current) noexcept {
			const float scale = std::max(std::abs(frozen), std::abs(current));
			return !std::isfinite(current)
					|| std::abs(frozen - current)
							> std::max(1.0F, scale) * 1.0e-6F;
		};
		if (candidateCalibration.fit != fit
				|| changed(candidateCalibration.sourceVerticalFovDegrees,
					sourceVerticalFovDegrees)
				|| changed(candidateCalibration.worldViewScale, worldViewScale)) {
			return ImmersiveProjectionBuildResult::invalidPoseOrFov;
		}
		if (fit == HalfSbsFitMode::cover) {
			const float scale = std::max(
					std::abs(candidateCalibration.sourceAspect), std::abs(sourceAspect));
			if (!std::isfinite(sourceAspect) || sourceAspect <= 0.0F
					|| std::abs(candidateCalibration.sourceAspect - sourceAspect)
							> std::max(1.0F, scale) * 1.0e-4F) {
				return ImmersiveProjectionBuildResult::sourceAspectChanged;
			}
		}
		for (std::size_t index = 0; index < requiredFovs.size(); ++index) {
			if (!projectionFovContains(candidateCalibration.fovs[index], requiredFovs[index])) {
				return ImmersiveProjectionBuildResult::frozenFovExceeded;
			}
		}
	}

	RollFreeBasisState candidateBasisState = basisState;
	std::array<Pose, 2> rollFreePoses;
	if (!composeRollFreeEyePoses(
			toPose(centerViewPose), relativePoses,
			candidateBasisState, rollFreePoses)) {
		return ImmersiveProjectionBuildResult::invalidPoseOrFov;
	}

	ImmersiveProjectionFrame candidateOutput;
	candidateOutput.centerViewPose = centerViewPose;
	for (std::size_t index = 0; index < relativeViews.size(); ++index) {
		candidateOutput.projectionViews[index].pose = toXrPose(rollFreePoses[index]);
		candidateOutput.projectionViews[index].fov = toXrFov(candidateCalibration.fovs[index]);
		candidateOutput.projectionViews[index].subImage.swapchain = swapchains[index].swapchain;
		candidateOutput.projectionViews[index].subImage.imageRect.extent = XrExtent2Di{
				static_cast<std::int32_t>(swapchains[index].width),
				static_cast<std::int32_t>(swapchains[index].height)};
	}
	candidateOutput.sourceMappings = candidateCalibration.sourceMappings;
	candidateOutput.physicalViewSourceMappings =
			candidateCalibration.physicalViewSourceMappings;

	basisState = candidateBasisState;
	calibration = candidateCalibration;
	output = candidateOutput;
	return ImmersiveProjectionBuildResult::success;
}

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
		ImmersiveProjectionFrame& output) {
	XrSpaceLocation centerLocation{XR_TYPE_SPACE_LOCATION};
	XrResult result = xrLocateSpace(viewSpace, localSpace, displayTime, &centerLocation);
	const XrSpaceLocationFlags requiredLocation = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT
			| XR_SPACE_LOCATION_POSITION_VALID_BIT
			| XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT
			| XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	if (result == XR_SESSION_LOSS_PENDING) {
		return ImmersiveProjectionBuildResult::sessionLossPending;
	}
	if (result != XR_SUCCESS) {
		printFailure("locating VIEW center for immersive projection", result);
		return ImmersiveProjectionBuildResult::openXrFailure;
	}
	if ((centerLocation.locationFlags & requiredLocation) != requiredLocation) {
		return ImmersiveProjectionBuildResult::invalidPoseOrFov;
	}

	std::array<XrView, 2> relativeViews{XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW}};
	XrViewState viewState{XR_TYPE_VIEW_STATE};
	const ImmersiveProjectionBuildResult locateResult =
			locateRelativeViews(session, viewSpace, displayTime, relativeViews, viewState);
	if (locateResult != ImmersiveProjectionBuildResult::success) {
		return locateResult;
	}

	return buildImmersiveProjectionFromLocatedViews(
			centerLocation.pose, relativeViews, rollCoverageDegrees, fit,
			sourceAspect, sourceVerticalFovDegrees, worldViewScale,
			basisState, calibration,
			fitDiagnostics, swapchains, output);
}

} // namespace mcxrinput::native
