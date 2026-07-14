#include <mcxrinput/projection_math.hpp>

#include <algorithm>
#include <array>
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

Quaternion inverse(Quaternion value) {
	return Quaternion{-value.x, -value.y, -value.z, value.w};
}

Quaternion multiply(Quaternion left, Quaternion right) {
	return Quaternion{
			left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
			left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
			left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
			left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z};
}

bool coversSampledCantedFrustum(
		ProjectionFov envelope, ProjectionFov runtime,
		Quaternion relativeOrientation, float coverageDegrees) {
	const Vec3 rollAxis = rotateVector(
			inverse(relativeOrientation), Vec3{0.0F, 0.0F, -1.0F});
	const float minimumX = std::tan(envelope.angleLeft) - 5.0e-4F;
	const float maximumX = std::tan(envelope.angleRight) + 5.0e-4F;
	const float minimumY = std::tan(envelope.angleDown) - 5.0e-4F;
	const float maximumY = std::tan(envelope.angleUp) + 5.0e-4F;
	const float left = std::tan(runtime.angleLeft);
	const float right = std::tan(runtime.angleRight);
	const float down = std::tan(runtime.angleDown);
	const float up = std::tan(runtime.angleUp);
	for (int step = 0; step <= 800; ++step) {
		const float degrees = -coverageDegrees
				+ 2.0F * coverageDegrees * static_cast<float>(step) / 800.0F;
		const Quaternion rotation = axisAngle(rollAxis, degrees);
		for (int xStep = 0; xStep <= 4; ++xStep) {
			const float x = left + (right - left) * static_cast<float>(xStep) / 4.0F;
			for (int yStep = 0; yStep <= 4; ++yStep) {
				const float y = down + (up - down) * static_cast<float>(yStep) / 4.0F;
				const Vec3 ray = rotateVector(rotation, Vec3{x, y, -1.0F});
				if (!(ray.z < -1.0e-6F)) {
					return false;
				}
				const float tangentX = -ray.x / ray.z;
				const float tangentY = -ray.y / ray.z;
				if (tangentX < minimumX || tangentX > maximumX
						|| tangentY < minimumY || tangentY > maximumY) {
					return false;
				}
			}
		}
	}
	return true;
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

	const std::array<Pose, 2> cantedRelative{
			Pose{axisAngle({0, 1, 0}, 5.0F), {-0.032F, 0, 0}},
			Pose{axisAngle({0, 1, 0}, -5.0F), {0.032F, 0, 0}},
	};
	RollFreeBasisState cantedState;
	std::array<Pose, 2> cantedOutput;
	check(composeRollFreeEyePoses(
			Pose{axisAngle({0, 0, 1}, 45.0F), {1, 2, 3}},
			cantedRelative, cantedState, cantedOutput),
			"canted runtime eye poses compose through roll removal");
	check(near(axis(cantedOutput[0], {0, 0, -1}),
			axis(cantedRelative[0], {0, 0, -1}))
			&& near(axis(cantedOutput[1], {0, 0, -1}),
					axis(cantedRelative[1], {0, 0, -1})),
			"per-frame eye cant is preserved while center-eye roll is removed");
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

void cantedFovExpansion() {
	const ProjectionFov asymmetric{-0.6F, 0.8F, 0.7F, -0.5F};
	for (float coverage : {0.0F, 5.0F, 20.0F, 45.0F}) {
		ProjectionFov legacy;
		ProjectionFov cantedIdentity;
		check(expandCenteredFovForRollCoverage(asymmetric, coverage, legacy)
				&& expandCantedFovForRollCoverage(
						asymmetric, Quaternion{}, coverage, cantedIdentity),
				"identity canted envelope computes alongside legacy envelope");
		check(near(legacy.angleLeft, cantedIdentity.angleLeft, 2.0e-6F)
				&& near(legacy.angleRight, cantedIdentity.angleRight, 2.0e-6F)
				&& near(legacy.angleUp, cantedIdentity.angleUp, 2.0e-6F)
				&& near(legacy.angleDown, cantedIdentity.angleDown, 2.0e-6F),
				"identity canted envelope preserves legacy tangent-roll math");
	}

	const Quaternion cant = axisAngle({0, 1, 0}, 15.0F);
	ProjectionFov canted;
	check(expandCantedFovForRollCoverage(asymmetric, cant, 30.0F, canted),
			"yaw-canted runtime view computes exact roll envelope");
	check(coversSampledCantedFrustum(canted, asymmetric, cant, 30.0F),
			"canted envelope contains dense roll and frustum-interior oracle");
	const Quaternion mixedCant = multiply(
			axisAngle({1, 0, 0}, 7.0F), axisAngle({0, 1, 0}, 12.0F));
	ProjectionFov mixedEnvelope;
	check(expandCantedFovForRollCoverage(
			asymmetric, mixedCant, 30.0F, mixedEnvelope)
			&& coversSampledCantedFrustum(
					mixedEnvelope, asymmetric, mixedCant, 30.0F),
			"mixed-axis canted envelope contains the dense independent oracle");

	// Fixed world-space composition reference. For a +20-degree eye yaw,
	// the runtime right/up corner (0.7, 0.6) reaches these tangents after
	// conjugating +/-15 degrees of center roll into eye space.
	const ProjectionFov fixedRuntime{-0.4F, 0.7F, 0.6F, -0.5F};
	ProjectionFov fixedEnvelope;
	check(expandCantedFovForRollCoverage(
			fixedRuntime, axisAngle({0, 1, 0}, 20.0F), 15.0F, fixedEnvelope)
			&& std::tan(fixedEnvelope.angleRight) >= 1.05250F
			&& std::tan(fixedEnvelope.angleUp) >= 0.72917F
			&& fixedEnvelope.angleLeft < 0.0F
			&& fixedEnvelope.angleRight > 0.0F
			&& fixedEnvelope.angleDown < 0.0F
			&& fixedEnvelope.angleUp > 0.0F,
			"fixed world-space canted reference preserves tangent signs and FOV order");

	const Quaternion negativeCant{-cant.x, -cant.y, -cant.z, -cant.w};
	ProjectionFov negativeResult;
	check(expandCantedFovForRollCoverage(
			asymmetric, negativeCant, 30.0F, negativeResult)
			&& near(canted.angleLeft, negativeResult.angleLeft, 2.0e-6F)
			&& near(canted.angleRight, negativeResult.angleRight, 2.0e-6F)
			&& near(canted.angleUp, negativeResult.angleUp, 2.0e-6F)
			&& near(canted.angleDown, negativeResult.angleDown, 2.0e-6F),
			"equivalent q and -q produce identical canted envelopes");

	// This asymmetric case reaches its most-negative horizontal tangent near
	// -8.7 degrees, far from the configured endpoints. It guards against an
	// endpoint-only approximation silently clipping the continuous interval.
	const ProjectionFov interiorCase{-1.2F, 0.2F, 0.4F, -0.3F};
	ProjectionFov interiorEnvelope;
	check(expandCantedFovForRollCoverage(
			interiorCase, axisAngle({0, 1, 0}, 20.0F), 45.0F,
			interiorEnvelope),
			"interior-extremum canted envelope computes");
	check(coversSampledCantedFrustum(
			interiorEnvelope, interiorCase,
			axisAngle({0, 1, 0}, 20.0F), 45.0F),
			"analytic stationary roots contain interior roll extrema");
	check(std::tan(interiorEnvelope.angleRight) > 2.57F,
			"interior stationary root contributes to centered envelope extent");

	ProjectionFov preserved{1, 2, 3, 4};
	const ProjectionFov original = preserved;
	check(computeCantedFovForRollCoverage(
			asymmetric, Quaternion{0, 0, 0, 0}, 20.0F, preserved)
					== CantedFovExpansionResult::invalidInput
			&& near(preserved.angleLeft, original.angleLeft)
			&& near(preserved.angleRight, original.angleRight),
			"invalid canted quaternion rejects transactionally");
	const float seventyDegrees = 70.0F * pi / 180.0F;
	check(computeCantedFovForRollCoverage(
			ProjectionFov{-seventyDegrees, seventyDegrees,
					seventyDegrees, -seventyDegrees},
			axisAngle({0, 1, 0}, 30.0F), 40.0F, preserved)
					== CantedFovExpansionResult::eyePlaneCrossing
			&& near(preserved.angleLeft, original.angleLeft)
			&& near(preserved.angleRight, original.angleRight),
			"canted envelope rejects a frustum ray crossing the eye plane");
}

void projectionCalibrationGuard() {
	const ProjectionFov runtime{-0.7F, 0.7F, 0.75F, -0.75F};
	ProjectionFov base;
	ProjectionFov frozen;
	check(expandCantedFovForRollCoverage(
			runtime, axisAngle({0, 1, 0}, 1.0F), 20.0F, base)
			&& expandProjectionFovByAngularGuard(base, 0.25F, frozen),
			"fixed projection guard expands a valid canted calibration");
	const float smallGrowth = 0.05F * pi / 180.0F;
	const ProjectionFov jitteredRuntime{
			runtime.angleLeft - smallGrowth,
			runtime.angleRight + smallGrowth,
			runtime.angleUp + smallGrowth,
			runtime.angleDown - smallGrowth};
	ProjectionFov jittered;
	check(expandCantedFovForRollCoverage(
			jitteredRuntime, axisAngle({0, 1, 0}, 1.1F), 20.0F, jittered)
			&& projectionFovContains(frozen, jittered),
			"frozen guard contains small runtime FOV and cant jitter");
	const float largeGrowth = 0.5F * pi / 180.0F;
	ProjectionFov grown;
	check(expandCantedFovForRollCoverage(
			ProjectionFov{runtime.angleLeft - largeGrowth,
					runtime.angleRight + largeGrowth,
					runtime.angleUp + largeGrowth,
					runtime.angleDown - largeGrowth},
			axisAngle({0, 1, 0}, 2.0F), 20.0F, grown)
			&& !projectionFovContains(frozen, grown),
			"geometry growth outside the fixed guard fails frozen containment");
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

void projectionSubFovMappings() {
	const ProjectionFov outer{
			std::atan(-2.0F), std::atan(2.0F),
			std::atan(2.0F), std::atan(-2.0F)};
	const ProjectionFov inner{
			std::atan(-1.0F), std::atan(0.5F),
			std::atan(1.5F), std::atan(-0.5F)};
	SourceUvTransform mapping;
	check(computeProjectionSubFovUvTransform(outer, inner, mapping)
			&& near(mapping.scaleX, 0.375F)
			&& near(mapping.scaleY, 0.5F)
			&& near(mapping.offsetX, 0.25F)
			&& near(mapping.offsetY, 0.125F),
			"asymmetric inner tangent bounds map into the outer projection texture");

	SourceUvTransform identity;
	check(computeProjectionSubFovUvTransform(outer, outer, identity)
			&& near(identity.scaleX, 1.0F)
			&& near(identity.scaleY, 1.0F)
			&& near(identity.offsetX, 0.0F)
			&& near(identity.offsetY, 0.0F),
			"an identical sub-frustum maps to the complete texture");

	const SourceUvTransform preserved = mapping;
	const ProjectionFov outside{
			std::atan(-2.1F), std::atan(0.5F),
			std::atan(1.5F), std::atan(-0.5F)};
	check(!computeProjectionSubFovUvTransform(outer, outside, mapping)
			&& near(mapping.scaleX, preserved.scaleX)
			&& near(mapping.scaleY, preserved.scaleY)
			&& near(mapping.offsetX, preserved.offsetX)
			&& near(mapping.offsetY, preserved.offsetY),
			"non-contained sub-frustum is rejected transactionally");
	check(!computeProjectionSubFovUvTransform(
			ProjectionFov{0.5F, -0.5F, 0.5F, -0.5F}, inner, mapping),
			"invalid outer projection is rejected");
}

void worldViewSamplingExpansion() {
	const ProjectionFov square45{-pi / 4.0F, pi / 4.0F, pi / 4.0F, -pi / 4.0F};
	ProjectionFov identity;
	check(expandProjectionFovForWorldViewScale(square45, 1.0F, identity)
			&& near(identity.angleLeft, square45.angleLeft)
			&& near(identity.angleRight, square45.angleRight)
			&& near(identity.angleUp, square45.angleUp)
			&& near(identity.angleDown, square45.angleDown),
			"one-to-one world view scale is an exact FOV identity");

	ProjectionFov expanded;
	const float expectedEdge = std::atan(1.0F / minimumWorldViewScale);
	check(expandProjectionFovForWorldViewScale(
			square45, minimumWorldViewScale, expanded)
			&& near(expanded.angleLeft, -expectedEdge)
			&& near(expanded.angleRight, expectedEdge)
			&& near(expanded.angleUp, expectedEdge)
			&& near(expanded.angleDown, -expectedEdge),
			"strongest experimental wider view expands every source-sampling tangent edge");

	float minimumDegrees = 0.0F;
	const float expectedMinimum = 2.0F * expectedEdge * 180.0F / pi;
	check(computeMinimumSourceVerticalFovDegrees(1.0F, expanded, minimumDegrees)
			&& near(minimumDegrees, expectedMinimum),
			"minimum source FOV includes the world-view tangent expansion");

	SourceUvTransform mapping;
	check(computeProjectionSourceUvTransform(
			1.0F, maximumSourceVerticalFovDegrees, expanded, mapping)
			== SourceProjectionMappingResult::success
			&& mapping.scaleX > 0.0F && mapping.scaleY > 0.0F,
			"expanded checkpoint fits the bounded maximum source FOV");

	const ProjectionFov preserved = expanded;
	check(!expandProjectionFovForWorldViewScale(square45, 0.299F, expanded)
			&& near(expanded.angleLeft, preserved.angleLeft)
			&& near(expanded.angleRight, preserved.angleRight),
			"out-of-range world view scale leaves FOV output unchanged");
	check(!expandProjectionFovForWorldViewScale(square45, 1.001F, expanded),
			"world view scale cannot magnify above the one-to-one default");

	MaximumRollCoverage oneToOne;
	MaximumRollCoverage wider;
	constexpr float capacityComparisonSourceFov = 150.0F;
	check(computeMaximumSupportedRollCoverage(
				square45, 1.0F, capacityComparisonSourceFov, oneToOne)
			&& computeMaximumSupportedRollCoverage(
				square45, 1.0F, capacityComparisonSourceFov,
				minimumWorldViewScale, wider)
			&& wider.supportsZeroCoverage
			&& wider.degrees < oneToOne.degrees,
			"roll-capacity diagnostics account for expanded source sampling");

	// The Quest/SteamVR hardware log was effectively +/-45 degrees horizontal
	// and +/-50 degrees vertical before the fixed roll envelope. Keep this
	// production-like capacity case explicit so the lower CLI bound cannot drift
	// beyond the 160-degree source contract unnoticed.
	const float degreesToRadians = pi / 180.0F;
	const ProjectionFov questLike{
			-45.0F * degreesToRadians, 45.0F * degreesToRadians,
			50.0F * degreesToRadians, -50.0F * degreesToRadians};
	ProjectionFov questRolled;
	ProjectionFov questGuarded;
	ProjectionFov questSampled;
	float questRequiredSourceFov = 0.0F;
	SourceUvTransform questMapping;
	check(computeCantedFovForRollCoverage(
			questLike, Quaternion{}, 15.0F, questRolled)
				== CantedFovExpansionResult::success
			&& expandProjectionFovByAngularGuard(questRolled, 0.25F, questGuarded),
			"Quest-like runtime FOV produces the fixed guarded roll envelope");

	struct HardwareCheckpoint {
		float sourceFovDegrees;
		float worldViewScale;
	};
	constexpr std::array<HardwareCheckpoint, 5> checkpoints{{
			{140.0F, 0.60F},
			{145.0F, 0.50F},
			{150.0F, 0.40F},
			{155.0F, 0.35F},
			{160.0F, 0.30F},
	}};
	bool everyCheckpointFits = true;
	for (const HardwareCheckpoint checkpoint : checkpoints) {
		ProjectionFov checkpointSamplingFov;
		float checkpointRequiredSourceFov = 0.0F;
		SourceUvTransform checkpointMapping;
		everyCheckpointFits = everyCheckpointFits
				&& expandProjectionFovForWorldViewScale(
						questGuarded, checkpoint.worldViewScale,
						checkpointSamplingFov)
				&& computeMinimumSourceVerticalFovDegrees(
						16.0F / 9.0F, checkpointSamplingFov,
						checkpointRequiredSourceFov)
				&& checkpointRequiredSourceFov < checkpoint.sourceFovDegrees
				&& computeProjectionSourceUvTransform(
						16.0F / 9.0F, checkpoint.sourceFovDegrees,
						checkpointSamplingFov, checkpointMapping)
						== SourceProjectionMappingResult::success;
	}
	check(everyCheckpointFits,
			"every documented Quest-like FOV/scale checkpoint retains source capacity");

	check(expandProjectionFovForWorldViewScale(
			questGuarded, minimumWorldViewScale, questSampled)
			&& computeMinimumSourceVerticalFovDegrees(
					16.0F / 9.0F, questSampled, questRequiredSourceFov)
			&& questRequiredSourceFov > 155.0F
			&& questRequiredSourceFov < 157.0F
			&& computeProjectionSourceUvTransform(
					16.0F / 9.0F, maximumSourceVerticalFovDegrees,
					questSampled, questMapping)
					== SourceProjectionMappingResult::success,
			"Quest-like roll envelope retains source capacity at scale 0.30");
	MaximumRollCoverage questCoverage;
	check(computeMaximumSupportedRollCoverage(
			questLike, Quaternion{}, 16.0F / 9.0F,
			maximumSourceVerticalFovDegrees, 0.25F,
			minimumWorldViewScale, questCoverage)
			&& questCoverage.supportsZeroCoverage
			&& questCoverage.degrees >= 15.0F,
			"Quest-like diagnostic confirms at least fifteen degrees of fixed roll");
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

	const Quaternion cant = axisAngle({0, 1, 0}, 10.0F);
	check(computeMaximumSupportedRollCoverage(
			square45, cant, 1.0F, 100.0F, 0.25F, coverage)
			&& coverage.supportsZeroCoverage && coverage.degrees > 0.0F
			&& coverage.degrees < 45.0F,
			"canted guarded roll-capacity diagnostic computes a partial range");
	ProjectionFov cantedExpanded;
	ProjectionFov cantedGuarded;
	check(expandCantedFovForRollCoverage(
			square45, cant, std::max(0.0F, coverage.degrees - 0.001F),
			cantedExpanded)
			&& expandProjectionFovByAngularGuard(cantedExpanded, 0.25F, cantedGuarded)
			&& computeProjectionSourceUvTransform(
					1.0F, 100.0F, cantedGuarded, mapping)
					== SourceProjectionMappingResult::success,
			"reported canted guarded maximum has a supported neighbor below it");
	check(expandCantedFovForRollCoverage(
			square45, cant, coverage.degrees + 0.001F, cantedExpanded)
			&& expandProjectionFovByAngularGuard(cantedExpanded, 0.25F, cantedGuarded)
			&& computeProjectionSourceUvTransform(
					1.0F, 100.0F, cantedGuarded, mapping)
					== SourceProjectionMappingResult::insufficientSourceFov,
			"reported canted guarded maximum has an unsupported neighbor above it");

	const float seventyDegrees = 70.0F * pi / 180.0F;
	check(computeMaximumSupportedRollCoverage(
			ProjectionFov{-seventyDegrees, seventyDegrees,
					seventyDegrees, -seventyDegrees},
			axisAngle({0, 1, 0}, 30.0F), 1.0F, 150.0F, 0.0F, coverage)
			&& coverage.supportsZeroCoverage && coverage.degrees > 0.0F
			&& coverage.degrees < 45.0F,
			"eye-plane crossing at the maximum query becomes a supported-capacity limit");
}

} // namespace

int main() {
	eyePoseComposition();
	invalidEyePoseIsTransactional();
	fovExpansion();
	cantedFovExpansion();
	projectionCalibrationGuard();
	sourceFovMappings();
	projectionSubFovMappings();
	worldViewSamplingExpansion();
	projectionCapacityDiagnostics();

	if (failures != 0) {
		std::cerr << failures << " projection-math test(s) failed.\n";
		return 1;
	}
	std::cout << "All projection-math tests passed.\n";
	return 0;
}
