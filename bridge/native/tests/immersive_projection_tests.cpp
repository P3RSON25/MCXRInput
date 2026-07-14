#include <mcxrinput/immersive_projection.hpp>

#include <cmath>
#include <iostream>
#include <limits>

using namespace mcxrinput::native;

namespace {

constexpr float epsilon = 1.0e-4F;
constexpr float pi = 3.14159265358979323846F;
int failures = 0;

void check(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

bool nearValue(float left, float right, float tolerance = epsilon) {
	return std::abs(left - right) <= tolerance;
}

bool nearMapping(
		const SourceUvTransform& left,
		const SourceUvTransform& right,
		float tolerance = epsilon) {
	return nearValue(left.scaleX, right.scaleX, tolerance)
			&& nearValue(left.scaleY, right.scaleY, tolerance)
			&& nearValue(left.offsetX, right.offsetX, tolerance)
			&& nearValue(left.offsetY, right.offsetY, tolerance);
}

bool containedMapping(const SourceUvTransform& mapping) {
	return std::isfinite(mapping.scaleX) && std::isfinite(mapping.scaleY)
			&& std::isfinite(mapping.offsetX) && std::isfinite(mapping.offsetY)
			&& mapping.scaleX > 0.0F && mapping.scaleY > 0.0F
			&& mapping.offsetX >= 0.0F && mapping.offsetY >= 0.0F
			&& mapping.offsetX + mapping.scaleX <= 1.0F + epsilon
			&& mapping.offsetY + mapping.scaleY <= 1.0F + epsilon;
}

ProjectionFov projectionFov(const XrFovf& fov) {
	return ProjectionFov{fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown};
}

XrQuaternionf zRotation(float degrees) {
	const float halfRadians = degrees * pi / 360.0F;
	return XrQuaternionf{0.0F, 0.0F, std::sin(halfRadians), std::cos(halfRadians)};
}

XrQuaternionf yRotation(float degrees) {
	const float halfRadians = degrees * pi / 360.0F;
	return XrQuaternionf{0.0F, std::sin(halfRadians), 0.0F, std::cos(halfRadians)};
}

XrPosef centerPose() {
	XrPosef pose{};
	pose.orientation = zRotation(30.0F);
	pose.position = XrVector3f{1.0F, 2.0F, 3.0F};
	return pose;
}

std::array<XrView, 2> stereoViews(float fovDegrees = 45.0F) {
	const float angle = fovDegrees * pi / 180.0F;
	std::array<XrView, 2> views{XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW}};
	for (std::size_t index = 0; index < views.size(); ++index) {
		views[index].pose.orientation.w = 1.0F;
		views[index].pose.position.x = index == 0 ? -0.032F : 0.032F;
		views[index].fov = XrFovf{-angle, angle, angle, -angle};
	}
	return views;
}

std::array<SwapchainBundle, 2> targets() {
	std::array<SwapchainBundle, 2> swapchains;
	swapchains[0].width = 2244;
	swapchains[0].height = 2352;
	swapchains[1].width = 2200;
	swapchains[1].height = 2300;
	return swapchains;
}

void successfulFrameReturnsAcceptedCenterPose() {
	RollFreeBasisState basis;
	ImmersiveProjectionCalibration calibration;
	ImmersiveProjectionFitDiagnostics diagnostics;
	ImmersiveProjectionFrame frame;
	const XrPosef center = centerPose();
	const auto views = stereoViews();
	const auto swapchains = targets();

	const ImmersiveProjectionBuildResult result =
			buildImmersiveProjectionFromLocatedViews(
					center, views, 15.0F, HalfSbsFitMode::cover,
					16.0F / 9.0F, 110.0F, 1.0F, basis, calibration,
					diagnostics, swapchains, frame);
	check(result == ImmersiveProjectionBuildResult::success,
			"valid stereo projection is accepted");
	check(calibration.initialized && diagnostics.valid,
			"first accepted cover frame freezes calibration and fit diagnostics");
	check(nearValue(frame.centerViewPose.orientation.z, center.orientation.z)
			&& nearValue(frame.centerViewPose.orientation.w, center.orientation.w)
			&& nearValue(frame.centerViewPose.position.x, center.position.x)
			&& nearValue(frame.centerViewPose.position.y, center.position.y)
			&& nearValue(frame.centerViewPose.position.z, center.position.z),
			"accepted frame returns the exact tracked center VIEW pose");
	check(frame.projectionViews[0].type == XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW
			&& frame.projectionViews[1].type == XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
			"both output views retain their OpenXR structure types");
	check(frame.projectionViews[0].subImage.imageRect.extent.width == 2244
			&& frame.projectionViews[0].subImage.imageRect.extent.height == 2352
			&& frame.projectionViews[1].subImage.imageRect.extent.width == 2200
			&& frame.projectionViews[1].subImage.imageRect.extent.height == 2300,
			"projection output uses the caller-owned swapchain dimensions");
	check(frame.sourceMappings[0].scaleX > 0.0F
			&& frame.sourceMappings[0].scaleX <= 1.0F
			&& frame.sourceMappings[0].scaleY > 0.0F
			&& frame.sourceMappings[0].scaleY <= 1.0F,
			"cover mode returns a contained source mapping");
	SourceUvTransform expectedPhysicalMapping;
	check(computeProjectionSourceUvTransform(
			16.0F / 9.0F, 110.0F, projectionFov(views[0].fov),
			expectedPhysicalMapping) == SourceProjectionMappingResult::success
			&& nearMapping(frame.physicalViewSourceMappings[0], expectedPhysicalMapping)
			&& frame.physicalViewSourceMappings[0].scaleX
					< frame.sourceMappings[0].scaleX
			&& frame.physicalViewSourceMappings[0].scaleY
					< frame.sourceMappings[0].scaleY,
			"cover freezes the raw physical eye crop separately from the larger roll canvas");
	check(nearValue(frame.projectionViews[0].pose.orientation.z, 0.0F)
			&& nearValue(frame.projectionViews[0].pose.orientation.w, 1.0F),
			"projection pose removes center-eye roll without altering accepted input pose");
}

void insufficientSourceRetainsCapacityDiagnostics() {
	RollFreeBasisState basis;
	ImmersiveProjectionCalibration calibration;
	ImmersiveProjectionFitDiagnostics diagnostics;
	ImmersiveProjectionFrame frame;
	frame.centerViewPose.position.x = 99.0F;
	const auto swapchains = targets();

	const ImmersiveProjectionBuildResult result =
			buildImmersiveProjectionFromLocatedViews(
					centerPose(), stereoViews(), 15.0F, HalfSbsFitMode::cover,
					16.0F / 9.0F, 30.0F, 1.0F, basis, calibration,
					diagnostics, swapchains, frame);
	check(result == ImmersiveProjectionBuildResult::insufficientSourceFov,
			"undersized rectilinear source FOV fails explicitly");
	check(diagnostics.valid
			&& diagnostics.requiredSourceVerticalFovDegrees[0] > 30.0F
			&& diagnostics.requiredSourceVerticalFovDegrees[1] > 30.0F,
			"source-FOV failure retains both-eye capacity diagnostics");
	check(!calibration.initialized && !basis.hasPreviousRight
			&& nearValue(frame.centerViewPose.position.x, 99.0F),
			"source-FOV failure does not commit calibration, basis, or output");

	const ImmersiveProjectionBuildResult strongScaleResult =
			buildImmersiveProjectionFromLocatedViews(
					centerPose(), stereoViews(), 15.0F, HalfSbsFitMode::cover,
					16.0F / 9.0F, 150.0F, minimumWorldViewScale,
					basis, calibration, diagnostics, swapchains, frame);
	check(strongScaleResult == ImmersiveProjectionBuildResult::insufficientSourceFov,
			"150-degree source explicitly fails the strongest supported view scale");
	check(diagnostics.valid
			&& diagnostics.requiredSourceVerticalFovDegrees[0] > 150.0F
			&& diagnostics.requiredSourceVerticalFovDegrees[0]
					< maximumSourceVerticalFovDegrees
			&& diagnostics.requiredSourceVerticalFovDegrees[1] > 150.0F
			&& diagnostics.requiredSourceVerticalFovDegrees[1]
					< maximumSourceVerticalFovDegrees,
			"strong-scale failure reports the bounded source capacity required by both eyes");
	check(!calibration.initialized && !basis.hasPreviousRight
			&& nearValue(frame.centerViewPose.position.x, 99.0F),
			"strong-scale source failure remains transactional");
}

void stretchReturnsFullSourceMappings() {
	RollFreeBasisState basis;
	ImmersiveProjectionCalibration calibration;
	ImmersiveProjectionFitDiagnostics diagnostics;
	ImmersiveProjectionFrame frame;
	const auto swapchains = targets();

	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), stereoViews(), 15.0F, HalfSbsFitMode::stretch,
			16.0F / 9.0F, 110.0F, 1.0F, basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::success,
			"stretch comparison remains a valid projection mode");
	for (std::size_t index = 0; index < frame.sourceMappings.size(); ++index) {
		const SourceUvTransform mapping = frame.sourceMappings[index];
		check(nearValue(mapping.scaleX, 1.0F)
				&& nearValue(mapping.scaleY, 1.0F)
				&& nearValue(mapping.offsetX, 0.0F)
				&& nearValue(mapping.offsetY, 0.0F),
				"stretch reports an uncropped identity source mapping");
		SourceUvTransform expectedPhysicalMapping;
		check(computeProjectionSubFovUvTransform(
				calibration.fovs[index], projectionFov(stereoViews()[index].fov),
				expectedPhysicalMapping)
				&& nearMapping(
						frame.physicalViewSourceMappings[index], expectedPhysicalMapping)
				&& frame.physicalViewSourceMappings[index].scaleX < 1.0F
				&& frame.physicalViewSourceMappings[index].scaleY < 1.0F,
				"stretch HUD mapping describes the raw eye inside its frozen canvas");
	}
	const ImmersiveProjectionCalibration frozenCalibration = calibration;
	const ImmersiveProjectionFrame frozenFrame = frame;
	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), stereoViews(), 15.0F, HalfSbsFitMode::stretch,
			16.0F / 9.0F, 130.0F, 0.9F, basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::invalidPoseOrFov,
			"stretch rejects an ambiguous non-default world view scale");
	check(nearValue(calibration.fovs[0].angleLeft,
				frozenCalibration.fovs[0].angleLeft)
			&& nearValue(frame.centerViewPose.position.x,
				frozenFrame.centerViewPose.position.x),
			"rejected stretch scale leaves calibration and output unchanged");
}

void widerViewExpandsSamplingWithoutChangingSubmittedFov() {
	const auto swapchains = targets();
	RollFreeBasisState oneToOneBasis;
	ImmersiveProjectionCalibration oneToOneCalibration;
	ImmersiveProjectionFitDiagnostics oneToOneDiagnostics;
	ImmersiveProjectionFrame oneToOneFrame;
	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), stereoViews(), 15.0F, HalfSbsFitMode::cover,
			16.0F / 9.0F, maximumSourceVerticalFovDegrees,
			maximumWorldViewScale, oneToOneBasis,
			oneToOneCalibration, oneToOneDiagnostics, swapchains, oneToOneFrame)
			== ImmersiveProjectionBuildResult::success,
			"maximum-source one-to-one fixture establishes a projection baseline");

	RollFreeBasisState widerBasis;
	ImmersiveProjectionCalibration widerCalibration;
	ImmersiveProjectionFitDiagnostics widerDiagnostics;
	ImmersiveProjectionFrame widerFrame;
	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), stereoViews(), 15.0F, HalfSbsFitMode::cover,
			16.0F / 9.0F, maximumSourceVerticalFovDegrees,
			minimumWorldViewScale, widerBasis,
			widerCalibration, widerDiagnostics, swapchains, widerFrame)
			== ImmersiveProjectionBuildResult::success,
			"strongest experimental wider-view sampling fits the maximum source FOV");

	for (std::size_t index = 0; index < widerFrame.projectionViews.size(); ++index) {
		check(nearValue(widerFrame.projectionViews[index].fov.angleLeft,
					oneToOneFrame.projectionViews[index].fov.angleLeft)
				&& nearValue(widerFrame.projectionViews[index].fov.angleRight,
					oneToOneFrame.projectionViews[index].fov.angleRight)
				&& nearValue(widerFrame.projectionViews[index].fov.angleUp,
					oneToOneFrame.projectionViews[index].fov.angleUp)
				&& nearValue(widerFrame.projectionViews[index].fov.angleDown,
					oneToOneFrame.projectionViews[index].fov.angleDown),
				"world view scale never changes the submitted OpenXR FOV");
		check(widerFrame.sourceMappings[index].scaleX
					> oneToOneFrame.sourceMappings[index].scaleX
				&& widerFrame.sourceMappings[index].scaleY
					> oneToOneFrame.sourceMappings[index].scaleY,
				"wider view samples a larger tangent-space source region");
		check(widerFrame.physicalViewSourceMappings[index].scaleX
					> oneToOneFrame.physicalViewSourceMappings[index].scaleX
				&& widerFrame.physicalViewSourceMappings[index].scaleY
					> oneToOneFrame.physicalViewSourceMappings[index].scaleY,
				"physical HUD crop accounts for the same wider-view sampling scale");
		check(containedMapping(widerFrame.sourceMappings[index])
				&& containedMapping(widerFrame.physicalViewSourceMappings[index]),
				"maximum-source render and physical HUD mappings remain contained");
		check(widerDiagnostics.requiredSourceVerticalFovDegrees[index]
					> oneToOneDiagnostics.requiredSourceVerticalFovDegrees[index],
				"wider-view source requirement is reflected in diagnostics");
	}
}

void physicalViewMappingsFreezeAndRespectAsymmetricEyes() {
	const auto swapchains = targets();
	auto views = stereoViews();
	const float degrees = pi / 180.0F;
	views[0].fov = XrFovf{-40.0F * degrees, 46.0F * degrees,
			48.0F * degrees, -42.0F * degrees};
	views[1].fov = XrFovf{-47.0F * degrees, 39.0F * degrees,
			43.0F * degrees, -49.0F * degrees};
	views[0].pose.orientation = yRotation(5.0F);
	views[1].pose.orientation = yRotation(-5.0F);

	RollFreeBasisState basis;
	ImmersiveProjectionCalibration calibration;
	ImmersiveProjectionFitDiagnostics diagnostics;
	ImmersiveProjectionFrame frame;
	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), views, 10.0F, HalfSbsFitMode::cover,
			16.0F / 9.0F, 130.0F, 0.9F, basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::success,
			"asymmetric canted-eye fixture establishes physical HUD mappings");
	for (std::size_t index = 0; index < views.size(); ++index) {
		ProjectionFov samplingFov;
		SourceUvTransform expected;
		check(expandProjectionFovForWorldViewScale(
				projectionFov(views[index].fov), 0.9F, samplingFov)
				&& computeProjectionSourceUvTransform(
						16.0F / 9.0F, 130.0F, samplingFov, expected)
						== SourceProjectionMappingResult::success
				&& nearMapping(frame.physicalViewSourceMappings[index], expected),
				"each canted eye keeps its exact aligned raw-FOV source crop");
	}
	check(!nearMapping(frame.physicalViewSourceMappings[0],
			frame.physicalViewSourceMappings[1]),
			"asymmetric eyes retain distinct physical source mappings");

	const auto frozenMappings = calibration.physicalViewSourceMappings;
	auto jittered = views;
	constexpr float jitter = 0.04F * pi / 180.0F;
	for (XrView& view : jittered) {
		view.fov.angleLeft -= jitter;
		view.fov.angleRight += jitter;
		view.fov.angleUp += jitter;
		view.fov.angleDown -= jitter;
	}
	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), jittered, 10.0F, HalfSbsFitMode::cover,
			16.0F / 9.0F, 130.0F, 0.9F, basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::success,
			"small runtime FOV jitter remains inside frozen calibration guard");
	check(nearMapping(frame.physicalViewSourceMappings[0], frozenMappings[0])
			&& nearMapping(frame.physicalViewSourceMappings[1], frozenMappings[1]),
			"accepted runtime jitter cannot move the frozen HUD recommendation");
}

void frozenCalibrationRejectsAspectAndFovChangesTransactionally() {
	RollFreeBasisState basis;
	ImmersiveProjectionCalibration calibration;
	ImmersiveProjectionFitDiagnostics diagnostics;
	ImmersiveProjectionFrame frame;
	const auto swapchains = targets();
	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), stereoViews(), 15.0F, HalfSbsFitMode::cover,
			16.0F / 9.0F, 110.0F, 1.0F, basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::success,
			"test fixture establishes frozen projection calibration");

	const RollFreeBasisState frozenBasis = basis;
	const ImmersiveProjectionCalibration frozenCalibration = calibration;
	const ImmersiveProjectionFrame frozenFrame = frame;
	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), stereoViews(), 15.0F, HalfSbsFitMode::cover,
			16.0F / 9.0F, 110.0F, 0.95F, basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::invalidPoseOrFov,
			"world view scale cannot change after projection calibration");
	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), stereoViews(), 15.0F, HalfSbsFitMode::cover,
			16.0F / 9.0F, 120.0F, 1.0F, basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::invalidPoseOrFov,
			"source vertical FOV cannot change after projection calibration");
	check(nearValue(calibration.sourceVerticalFovDegrees,
				frozenCalibration.sourceVerticalFovDegrees)
			&& nearValue(calibration.worldViewScale,
				frozenCalibration.worldViewScale)
			&& nearValue(calibration.fovs[0].angleLeft,
				frozenCalibration.fovs[0].angleLeft)
			&& nearValue(frame.centerViewPose.position.x,
				frozenFrame.centerViewPose.position.x),
			"mapping-parameter rejection preserves frozen calibration and output");
	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), stereoViews(), 15.0F, HalfSbsFitMode::cover,
			4.0F / 3.0F, 110.0F, 1.0F, basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::sourceAspectChanged,
			"decoded-eye aspect change is rejected after calibration");
	check(nearValue(calibration.sourceAspect, frozenCalibration.sourceAspect)
			&& nearValue(calibration.fovs[0].angleLeft, frozenCalibration.fovs[0].angleLeft)
			&& nearValue(basis.previousRight.x, frozenBasis.previousRight.x)
			&& nearValue(basis.previousRight.y, frozenBasis.previousRight.y)
			&& nearValue(basis.previousRight.z, frozenBasis.previousRight.z)
			&& nearValue(frame.centerViewPose.position.x, frozenFrame.centerViewPose.position.x),
			"aspect rejection preserves frozen projection state and output");

	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), stereoViews(70.0F), 15.0F, HalfSbsFitMode::cover,
			16.0F / 9.0F, 110.0F, 1.0F, basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::frozenFovExceeded,
			"runtime FOV growth beyond the frozen envelope is rejected");
	check(nearValue(calibration.fovs[0].angleLeft, frozenCalibration.fovs[0].angleLeft)
			&& nearValue(frame.centerViewPose.position.x, frozenFrame.centerViewPose.position.x),
			"FOV rejection cannot introduce zoom breathing or replace accepted output");
}

void invalidPoseIsTransactional() {
	RollFreeBasisState basis;
	ImmersiveProjectionCalibration calibration;
	ImmersiveProjectionFitDiagnostics diagnostics;
	ImmersiveProjectionFrame frame;
	frame.centerViewPose.position.y = 77.0F;
	auto views = stereoViews();
	views[0].pose.orientation.w = std::numeric_limits<float>::quiet_NaN();
	const auto swapchains = targets();

	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), views, 15.0F, HalfSbsFitMode::cover,
			16.0F / 9.0F, 110.0F, 1.0F, basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::invalidPoseOrFov,
			"non-finite relative eye pose is rejected");
	check(!calibration.initialized && !basis.hasPreviousRight
			&& nearValue(frame.centerViewPose.position.y, 77.0F),
			"invalid pose preserves all caller-owned projection state");

	views = stereoViews();
	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), views, 15.0F, HalfSbsFitMode::cover,
			16.0F / 9.0F, maximumSourceVerticalFovDegrees, 0.299F,
			basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::invalidPoseOrFov,
			"out-of-range world view scale is rejected by the projection adapter");
	check(!calibration.initialized && !basis.hasPreviousRight
			&& nearValue(frame.centerViewPose.position.y, 77.0F),
			"invalid world view scale preserves calibration, basis, and output");

	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), views, 15.0F, HalfSbsFitMode::cover,
			16.0F / 9.0F, maximumSourceVerticalFovDegrees + 0.001F,
			maximumWorldViewScale, basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::invalidPoseOrFov,
			"source FOV just above the application bound is rejected");
	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), views, 15.0F, HalfSbsFitMode::cover,
			16.0F / 9.0F, minimumSourceVerticalFovDegrees - 0.001F,
			maximumWorldViewScale, basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::invalidPoseOrFov,
			"source FOV just below the application bound is rejected");
	check(!calibration.initialized && !basis.hasPreviousRight
			&& nearValue(frame.centerViewPose.position.y, 77.0F),
			"invalid source bounds preserve calibration, basis, and output");

	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), views, 15.0F, HalfSbsFitMode::contain,
			16.0F / 9.0F, 110.0F, 1.0F, basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::invalidPoseOrFov,
			"finite-screen contain fit is rejected by the immersive projection adapter");
	check(!calibration.initialized && !basis.hasPreviousRight
			&& nearValue(frame.centerViewPose.position.y, 77.0F),
			"unsupported fit preserves calibration, basis, and output");

	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), views, 15.0F, static_cast<HalfSbsFitMode>(999),
			16.0F / 9.0F, 110.0F, 1.0F, basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::invalidPoseOrFov,
			"unknown fit enum is rejected by the immersive projection adapter");
	check(!calibration.initialized && !basis.hasPreviousRight
			&& nearValue(frame.centerViewPose.position.y, 77.0F),
			"unknown fit preserves calibration, basis, and output");
}

} // namespace

int main() {
	successfulFrameReturnsAcceptedCenterPose();
	insufficientSourceRetainsCapacityDiagnostics();
	stretchReturnsFullSourceMappings();
	widerViewExpandsSamplingWithoutChangingSubmittedFov();
	physicalViewMappingsFreezeAndRespectAsymmetricEyes();
	frozenCalibrationRejectsAspectAndFovChangesTransactionally();
	invalidPoseIsTransactional();

	if (failures != 0) {
		std::cerr << failures << " immersive-projection test(s) failed.\n";
		return 1;
	}
	std::cout << "All immersive-projection tests passed.\n";
	return 0;
}
