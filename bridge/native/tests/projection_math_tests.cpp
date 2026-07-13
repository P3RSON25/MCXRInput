#include <mcxrinput/projection_math.hpp>

#include <algorithm>
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

bool near(float left, float right, float tolerance = epsilon) {
	return std::abs(left - right) <= tolerance;
}

bool near(Vec3 left, Vec3 right, float tolerance = epsilon) {
	return near(left.x, right.x, tolerance)
			&& near(left.y, right.y, tolerance)
			&& near(left.z, right.z, tolerance);
}

Quaternion axisAngle(Vec3 axis, float degrees) {
	const float radians = degrees * pi / 180.0F;
	const float sine = std::sin(radians * 0.5F);
	return Quaternion{axis.x * sine, axis.y * sine, axis.z * sine,
			std::cos(radians * 0.5F)};
}

Vec3 axis(const Pose& pose, Vec3 local) {
	return rotateVector(pose.orientation, local);
}

void eyePoseComposition() {
	const std::array<Pose, 2> relative{
			Pose{{0, 0, 0, 1}, {-0.032F, 0, 0}},
			Pose{{0, 0, 0, 1}, {0.032F, 0, 0}},
	};
	RollFreeBasisState state;
	std::array<Pose, 2> output;
	check(composeRollFreeEyePoses(Pose{{0, 0, 0, 1}, {1, 2, 3}}, relative,
			state, output), "neutral stereo pose composes");
	check(near(output[0].position, {0.968F, 2, 3})
			&& near(output[1].position, {1.032F, 2, 3}),
			"neutral eye positions preserve IPD around translated center");

	RollFreeBasisState rolledState;
	std::array<Pose, 2> rolled;
	check(composeRollFreeEyePoses(
			Pose{axisAngle({0, 0, 1}, 60), {1, 2, 3}}, relative,
			rolledState, rolled), "rolled stereo pose composes");
	check(near(rolled[0].position, output[0].position)
			&& near(rolled[1].position, output[1].position),
			"roll-free eye baseline remains gravity-horizontal");
	check(near(axis(rolled[0], {0, 1, 0}), Vec3{0, 1, 0}),
			"pure roll is removed from eye orientation");

	const float originalIpd = relative[1].position.x - relative[0].position.x;
	const Vec3 delta{
			rolled[1].position.x - rolled[0].position.x,
			rolled[1].position.y - rolled[0].position.y,
			rolled[1].position.z - rolled[0].position.z,
	};
	const float outputIpd = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
	check(near(outputIpd, originalIpd), "roll-free composition preserves IPD length");
}

void invalidEyePoseIsTransactional() {
	RollFreeBasisState state{{0, 0, 1}, true, true};
	std::array<Pose, 2> output{
			Pose{{1, 2, 3, 4}, {5, 6, 7}},
			Pose{{4, 3, 2, 1}, {7, 6, 5}},
	};
	const auto originalOutput = output;
	const auto originalState = state;
	std::array<Pose, 2> relative{};
	relative[1].orientation.w = std::numeric_limits<float>::quiet_NaN();
	check(!composeRollFreeEyePoses(Pose{}, relative, state, output),
			"invalid relative eye pose is rejected");
	check(near(output[0].position, originalOutput[0].position)
			&& near(output[1].position, originalOutput[1].position)
			&& near(state.previousRight, originalState.previousRight)
			&& state.hasPreviousRight == originalState.hasPreviousRight
			&& state.usingVerticalFallback == originalState.usingVerticalFallback,
			"invalid relative eye pose preserves output and state");
}

void fovExpansion() {
	const ProjectionFov square{-0.7853982F, 0.7853982F, 0.7853982F, -0.7853982F};
	ProjectionFov unchanged;
	check(expandCenteredFovForRollCoverage(square, 0.0F, unchanged),
			"zero-degree FOV expansion succeeds");
	check(near(unchanged.angleLeft, square.angleLeft)
			&& near(unchanged.angleRight, square.angleRight)
			&& near(unchanged.angleUp, square.angleUp)
			&& near(unchanged.angleDown, square.angleDown),
			"zero-degree FOV expansion is identity");

	ProjectionFov expanded;
	check(expandCenteredFovForRollCoverage(square, 20.0F, expanded),
			"20-degree FOV expansion succeeds");
	check(expanded.angleLeft < square.angleLeft
			&& expanded.angleRight > square.angleRight
			&& expanded.angleUp > square.angleUp
			&& expanded.angleDown < square.angleDown,
			"roll coverage expands all square-frustum sides");

	check(near(std::tan(expanded.angleRight) - std::tan(expanded.angleLeft),
			std::tan(expanded.angleUp) - std::tan(expanded.angleDown)),
			"symmetric expanded square retains unit tangent aspect");
	const ProjectionFov asymmetric{-0.6F, 0.8F, 0.7F, -0.5F};
	ProjectionFov centered;
	check(expandCenteredFovForRollCoverage(asymmetric, 0.0F, centered),
			"asymmetric runtime FOV centers successfully");
	check(near(centered.angleLeft, -centered.angleRight)
			&& near(centered.angleDown, -centered.angleUp)
			&& centered.angleLeft <= asymmetric.angleLeft
			&& centered.angleRight >= asymmetric.angleRight
			&& centered.angleDown <= asymmetric.angleDown
			&& centered.angleUp >= asymmetric.angleUp,
			"centered video FOV contains asymmetric runtime FOV around tan zero");
	const float expandedLeft = std::tan(centered.angleLeft);
	const float expandedRight = std::tan(centered.angleRight);
	const float expandedDown = std::tan(centered.angleDown);
	const float expandedUp = std::tan(centered.angleUp);
	for (float degrees = -20.0F; degrees <= 20.0F; degrees += 0.25F) {
		ProjectionFov covered;
		check(expandCenteredFovForRollCoverage(asymmetric, 20.0F, covered),
				"asymmetric roll-covered FOV computes");
		const float angle = degrees * pi / 180.0F;
		const float cosine = std::cos(angle);
		const float sine = std::sin(angle);
		for (float x : {std::tan(asymmetric.angleLeft), std::tan(asymmetric.angleRight)}) {
			for (float y : {std::tan(asymmetric.angleDown), std::tan(asymmetric.angleUp)}) {
				const float rotatedX = x * cosine - y * sine;
				const float rotatedY = x * sine + y * cosine;
				check(rotatedX >= std::tan(covered.angleLeft) - epsilon
						&& rotatedX <= std::tan(covered.angleRight) + epsilon
						&& rotatedY >= std::tan(covered.angleDown) - epsilon
						&& rotatedY <= std::tan(covered.angleUp) + epsilon,
						"fixed expanded FOV contains every sampled rolled runtime corner");
			}
		}
	}
	check(expandedLeft <= std::tan(asymmetric.angleLeft)
			&& expandedRight >= std::tan(asymmetric.angleRight)
			&& expandedDown <= std::tan(asymmetric.angleDown)
			&& expandedUp >= std::tan(asymmetric.angleUp),
			"zero-roll centered FOV contains asymmetric tangent bounds");

	check(!expandCenteredFovForRollCoverage(square, 46.0F, expanded),
			"unsupported roll coverage is rejected");
	check(!expandCenteredFovForRollCoverage(
			ProjectionFov{0.5F, -0.5F, 0.5F, -0.5F}, 20.0F, expanded),
			"reversed FOV is rejected");
	check(projectionFovContains(expanded, square),
			"expanded FOV contains its original runtime FOV");
	check(!projectionFovContains(square, expanded),
			"smaller frozen FOV rejects a later expanded requirement");
}

void eyeOrientationValidation() {
	check(orientationNearIdentity(Quaternion{}, 0.01F),
			"identity eye orientation is supported");
	check(orientationNearIdentity(axisAngle({0, 1, 0}, 0.005F), 0.01F),
			"sub-epsilon runtime eye rotation is supported");
	check(!orientationNearIdentity(axisAngle({0, 1, 0}, 0.02F), 0.01F),
			"canted runtime eye view fails conservatively");
	check(orientationNearIdentity(Quaternion{0, 0, 0, -2}, 0.0F),
			"negative equivalent identity quaternion is supported");
	check(!orientationNearIdentity(Quaternion{0, 0, 0, 0}, 0.01F),
			"invalid eye orientation is rejected");
}

void sourceFovMappings() {
	SourceUvTransform transform;
	const ProjectionFov square45{-pi / 4.0F, pi / 4.0F, pi / 4.0F, -pi / 4.0F};
	check(computeProjectionSourceUvTransform(2.0F, 90.0F, square45, transform)
			== SourceProjectionMappingResult::success,
			"sufficient source FOV maps successfully");
	check(near(transform.scaleX, 0.5F) && near(transform.offsetX, 0.25F)
			&& near(transform.scaleY, 1.0F) && near(transform.offsetY, 0.0F),
			"projection source mapping preserves tangent scale and centers crop");

	const ProjectionFov vertical120{-pi / 4.0F, pi / 4.0F, pi / 3.0F, -pi / 3.0F};
	const SourceUvTransform original = transform;
	check(computeProjectionSourceUvTransform(2.0F, 90.0F, vertical120, transform)
			== SourceProjectionMappingResult::insufficientSourceFov,
			"insufficient source vertical FOV is reported distinctly");
	check(near(transform.scaleX, original.scaleX)
			&& near(transform.scaleY, original.scaleY)
			&& near(transform.offsetX, original.offsetX)
			&& near(transform.offsetY, original.offsetY),
			"insufficient source FOV leaves mapping unchanged");

	ProjectionFov expanded;
	check(expandCenteredFovForRollCoverage(square45, 20.0F, expanded)
			&& computeProjectionSourceUvTransform(16.0F / 9.0F, 110.0F,
					expanded, transform) == SourceProjectionMappingResult::success,
			"default 110-degree 16:9 source covers a 90-degree square view with roll margin");
	check(transform.offsetX >= 0.0F && transform.offsetY >= 0.0F
			&& transform.offsetX + transform.scaleX <= 1.0F
			&& transform.offsetY + transform.scaleY <= 1.0F,
			"successful projection crop remains inside one decoded eye");

	const ProjectionFov asymmetric{-0.5F, 0.7F, 0.6F, -0.4F};
	check(computeProjectionSourceUvTransform(16.0F / 9.0F, 110.0F,
			asymmetric, transform) == SourceProjectionMappingResult::success
			&& transform.offsetX != (1.0F - transform.scaleX) * 0.5F
			&& transform.offsetY != (1.0F - transform.scaleY) * 0.5F,
			"asymmetric target FOV maps tan-zero away from texture center correctly");
	check(computeProjectionSourceUvTransform(0.0F, 90.0F, square45, transform)
			== SourceProjectionMappingResult::invalidInput,
			"invalid source projection metadata is rejected");
}

void projectionCapacityDiagnostics() {
	const ProjectionFov square45{-pi / 4.0F, pi / 4.0F, pi / 4.0F, -pi / 4.0F};
	float minimumDegrees = -1.0F;
	check(computeMinimumSourceVerticalFovDegrees(
			1.0F, square45, minimumDegrees)
			&& near(minimumDegrees, 90.0F),
			"unit-aspect square requires a 90-degree centered source");
	check(computeMinimumSourceVerticalFovDegrees(
			2.0F, square45, minimumDegrees)
			&& near(minimumDegrees, 90.0F),
			"wider source cannot reduce a vertical FOV requirement");

	const ProjectionFov horizontal120Vertical60{
			-pi / 3.0F, pi / 3.0F, pi / 6.0F, -pi / 6.0F};
	check(computeMinimumSourceVerticalFovDegrees(
			1.0F, horizontal120Vertical60, minimumDegrees)
			&& near(minimumDegrees, 120.0F),
			"horizontal tangent extent contributes through source aspect");
	const float expectedWideDegrees = 2.0F * std::atan(
			std::tan(pi / 3.0F) / 2.0F) * 180.0F / pi;
	check(computeMinimumSourceVerticalFovDegrees(
			2.0F, horizontal120Vertical60, minimumDegrees)
			&& near(minimumDegrees, expectedWideDegrees),
			"decoded source aspect reduces only the horizontal requirement");

	const float preservedMinimum = minimumDegrees;
	check(!computeMinimumSourceVerticalFovDegrees(
			0.0F, square45, minimumDegrees)
			&& near(minimumDegrees, preservedMinimum),
			"invalid minimum-FOV input is transactional");
	check(!computeMinimumSourceVerticalFovDegrees(
			1.0F, ProjectionFov{0.5F, -0.5F, 0.5F, -0.5F}, minimumDegrees)
			&& near(minimumDegrees, preservedMinimum),
			"invalid target FOV cannot produce a minimum source FOV");

	MaximumRollCoverage coverage{-1.0F, false};
	check(computeMaximumSupportedRollCoverage(
			square45, 1.0F, 120.0F, coverage)
			&& coverage.supportsZeroCoverage && near(coverage.degrees, 45.0F),
			"ample source FOV supports the complete 45-degree diagnostic range");

	check(computeMaximumSupportedRollCoverage(
			square45, 1.0F, 100.0F, coverage)
			&& coverage.supportsZeroCoverage,
			"partial fixed-roll capacity computes");
	const float sourceHalfHeight = std::tan(50.0F * pi / 180.0F);
	const float expectedCoverage =
			std::asin(sourceHalfHeight / std::sqrt(2.0F)) * 180.0F / pi - 45.0F;
	check(near(coverage.degrees, expectedCoverage, 2.0e-4F),
			"square-frustum roll limit matches its analytic tangent bound");

	ProjectionFov justInside;
	ProjectionFov justOutside;
	SourceUvTransform mapping;
	check(expandCenteredFovForRollCoverage(
			square45, std::max(0.0F, coverage.degrees - 0.001F), justInside)
			&& computeProjectionSourceUvTransform(
					1.0F, 100.0F, justInside, mapping)
					== SourceProjectionMappingResult::success,
			"reported maximum has a supported neighbor below it");
	check(expandCenteredFovForRollCoverage(
			square45, coverage.degrees + 0.001F, justOutside)
			&& computeProjectionSourceUvTransform(
					1.0F, 100.0F, justOutside, mapping)
					== SourceProjectionMappingResult::insufficientSourceFov,
			"reported maximum has an unsupported neighbor above it");

	check(computeMaximumSupportedRollCoverage(
			square45, 1.0F, 90.0F, coverage)
			&& coverage.supportsZeroCoverage
			&& near(coverage.degrees, 0.0F, 2.0e-4F),
			"exactly fitting source reports a true zero-degree roll limit");
	const ProjectionFov vertical120{
			-pi / 4.0F, pi / 4.0F, pi / 3.0F, -pi / 3.0F};
	check(computeMaximumSupportedRollCoverage(
			vertical120, 1.0F, 90.0F, coverage)
			&& !coverage.supportsZeroCoverage && near(coverage.degrees, 0.0F),
			"source that misses the centered runtime frustum reports no roll capacity");
	const ProjectionFov wideRuntime{
			-std::atan(1.5F), std::atan(1.5F), std::atan(0.5F), -std::atan(0.5F)};
	check(computeMaximumSupportedRollCoverage(
			wideRuntime, 1.0F, 100.0F, coverage)
			&& !coverage.supportsZeroCoverage,
			"narrow decoded source cannot contain a wide runtime frustum");
	check(computeMaximumSupportedRollCoverage(
			wideRuntime, 2.0F, 100.0F, coverage)
			&& coverage.supportsZeroCoverage && coverage.degrees > 0.0F,
			"roll-capacity calculation honors decoded source aspect");

	const MaximumRollCoverage preservedCoverage = coverage;
	check(!computeMaximumSupportedRollCoverage(
			square45, -1.0F, 100.0F, coverage)
			&& near(coverage.degrees, preservedCoverage.degrees)
			&& coverage.supportsZeroCoverage == preservedCoverage.supportsZeroCoverage,
			"invalid roll-capacity input is transactional");
}

} // namespace

int main() {
	eyePoseComposition();
	invalidEyePoseIsTransactional();
	fovExpansion();
	eyeOrientationValidation();
	sourceFovMappings();
	projectionCapacityDiagnostics();

	if (failures != 0) {
		std::cerr << failures << " projection-math test(s) failed.\n";
		return 1;
	}
	std::cout << "All projection-math tests passed.\n";
	return 0;
}
