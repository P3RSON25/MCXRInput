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
		RollFreeBasisState& basisState,
		ImmersiveProjectionCalibration& calibration,
		ImmersiveProjectionFitDiagnostics& fitDiagnostics,
		const std::array<SwapchainBundle, 2>& swapchains,
		ImmersiveProjectionFrame& output) noexcept {
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
		for (std::size_t index = 0; index < requiredFovs.size(); ++index) {
			if (!expandProjectionFovByAngularGuard(
					requiredFovs[index], immersiveProjectionCalibrationGuardDegrees,
					candidateCalibration.fovs[index])) {
				return ImmersiveProjectionBuildResult::invalidPoseOrFov;
			}
		}
		if (fit == HalfSbsFitMode::cover) {
			ImmersiveProjectionFitDiagnostics candidateFitDiagnostics;
			for (std::size_t index = 0; index < requiredFovs.size(); ++index) {
				if (!computeMinimumSourceVerticalFovDegrees(
						sourceAspect, candidateCalibration.fovs[index],
						candidateFitDiagnostics.requiredSourceVerticalFovDegrees[index])
						|| !computeMaximumSupportedRollCoverage(
							runtimeFovs[index], relativePoses[index].orientation,
							sourceAspect, sourceVerticalFovDegrees,
							immersiveProjectionCalibrationGuardDegrees,
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
							candidateCalibration.fovs[index],
							candidateCalibration.sourceMappings[index]);
				if (mappingResult == SourceProjectionMappingResult::insufficientSourceFov) {
					return ImmersiveProjectionBuildResult::insufficientSourceFov;
				}
				if (mappingResult != SourceProjectionMappingResult::success) {
					return ImmersiveProjectionBuildResult::invalidPoseOrFov;
				}
			}
		} else {
			// Stretch uses the complete source and therefore has no cropped edge.
			// Keep the mapping contract meaningful for downstream HUD visibility
			// calculations even though the renderer itself ignores it in this mode.
			for (SourceUvTransform& mapping : candidateCalibration.sourceMappings) {
				mapping = SourceUvTransform{1.0F, 1.0F, 0.0F, 0.0F};
			}
		}
		candidateCalibration.initialized = true;
	} else {
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
			sourceAspect, sourceVerticalFovDegrees, basisState, calibration,
			fitDiagnostics, swapchains, output);
}

} // namespace mcxrinput::native
