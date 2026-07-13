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

XrQuaternionf zRotation(float degrees) {
	const float halfRadians = degrees * pi / 360.0F;
	return XrQuaternionf{0.0F, 0.0F, std::sin(halfRadians), std::cos(halfRadians)};
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
					16.0F / 9.0F, 110.0F, basis, calibration,
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
					16.0F / 9.0F, 30.0F, basis, calibration,
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
}

void frozenCalibrationRejectsAspectAndFovChangesTransactionally() {
	RollFreeBasisState basis;
	ImmersiveProjectionCalibration calibration;
	ImmersiveProjectionFitDiagnostics diagnostics;
	ImmersiveProjectionFrame frame;
	const auto swapchains = targets();
	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), stereoViews(), 15.0F, HalfSbsFitMode::cover,
			16.0F / 9.0F, 110.0F, basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::success,
			"test fixture establishes frozen projection calibration");

	const RollFreeBasisState frozenBasis = basis;
	const ImmersiveProjectionCalibration frozenCalibration = calibration;
	const ImmersiveProjectionFrame frozenFrame = frame;
	check(buildImmersiveProjectionFromLocatedViews(
			centerPose(), stereoViews(), 15.0F, HalfSbsFitMode::cover,
			4.0F / 3.0F, 110.0F, basis, calibration, diagnostics,
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
			16.0F / 9.0F, 110.0F, basis, calibration, diagnostics,
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
			16.0F / 9.0F, 110.0F, basis, calibration, diagnostics,
			swapchains, frame) == ImmersiveProjectionBuildResult::invalidPoseOrFov,
			"non-finite relative eye pose is rejected");
	check(!calibration.initialized && !basis.hasPreviousRight
			&& nearValue(frame.centerViewPose.position.y, 77.0F),
			"invalid pose preserves all caller-owned projection state");
}

} // namespace

int main() {
	successfulFrameReturnsAcceptedCenterPose();
	insufficientSourceRetainsCapacityDiagnostics();
	frozenCalibrationRejectsAspectAndFovChangesTransactionally();
	invalidPoseIsTransactional();

	if (failures != 0) {
		std::cerr << failures << " immersive-projection test(s) failed.\n";
		return 1;
	}
	std::cout << "All immersive-projection tests passed.\n";
	return 0;
}
