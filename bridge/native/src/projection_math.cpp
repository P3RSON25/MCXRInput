#include <mcxrinput/projection_math.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace mcxrinput::native {
namespace {

constexpr double pi = 3.1415926535897932384626433832795;
constexpr double halfPi = pi * 0.5;
constexpr double minimumSpan = 1.0e-9;
constexpr double minimumForward = 1.0e-8;

bool finite(float value) noexcept {
	return std::isfinite(value);
}

bool finite(Vec3 value) noexcept {
	return finite(value.x) && finite(value.y) && finite(value.z);
}

bool finite(Quaternion value) noexcept {
	return finite(value.x) && finite(value.y) && finite(value.z) && finite(value.w);
}

bool multiplyQuaternion(Quaternion left, Quaternion right, Quaternion& output) noexcept {
	Quaternion candidate{
			left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
			left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
			left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
			left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
	};
	return normalizeQuaternion(candidate, output);
}

Vec3 add(Vec3 left, Vec3 right) noexcept {
	return Vec3{left.x + right.x, left.y + right.y, left.z + right.z};
}

bool validFov(ProjectionFov fov) noexcept {
	if (!finite(fov.angleLeft) || !finite(fov.angleRight)
			|| !finite(fov.angleUp) || !finite(fov.angleDown)) {
		return false;
	}
	const double left = fov.angleLeft;
	const double right = fov.angleRight;
	const double up = fov.angleUp;
	const double down = fov.angleDown;
	return left > -halfPi && left < halfPi
			&& right > -halfPi && right < halfPi
			&& up > -halfPi && up < halfPi
			&& down > -halfPi && down < halfPi
			&& left < right && down < up;
}

struct Bounds {
	double minimumX{std::numeric_limits<double>::infinity()};
	double maximumX{-std::numeric_limits<double>::infinity()};
	double minimumY{std::numeric_limits<double>::infinity()};
	double maximumY{-std::numeric_limits<double>::infinity()};
};

struct DVec3 {
	double x{0.0};
	double y{0.0};
	double z{0.0};
};

double dot(DVec3 left, DVec3 right) noexcept {
	return left.x * right.x + left.y * right.y + left.z * right.z;
}

DVec3 cross(DVec3 left, DVec3 right) noexcept {
	return DVec3{
			left.y * right.z - left.z * right.y,
			left.z * right.x - left.x * right.z,
			left.x * right.y - left.y * right.x};
}

DVec3 add(DVec3 left, DVec3 right) noexcept {
	return DVec3{left.x + right.x, left.y + right.y, left.z + right.z};
}

DVec3 subtract(DVec3 left, DVec3 right) noexcept {
	return DVec3{left.x - right.x, left.y - right.y, left.z - right.z};
}

DVec3 multiply(DVec3 value, double scale) noexcept {
	return DVec3{value.x * scale, value.y * scale, value.z * scale};
}

struct SinusoidVector {
	DVec3 constant{};
	DVec3 cosine{};
	DVec3 sine{};
};

DVec3 evaluate(SinusoidVector value, double angle) noexcept {
	return add(value.constant, add(
			multiply(value.cosine, std::cos(angle)),
			multiply(value.sine, std::sin(angle))));
}

template<typename Callback>
void forEachPeriodicAngle(
		double base, double period, double minimum, double maximum,
		Callback&& callback) {
	const int first = static_cast<int>(std::ceil((minimum - base) / period));
	const int last = static_cast<int>(std::floor((maximum - base) / period));
	for (int turn = first; turn <= last; ++turn) {
		callback(base + static_cast<double>(turn) * period);
	}
}

template<typename Callback>
void includeRatioStationaryAngles(
		double numeratorConstant, double numeratorCosine, double numeratorSine,
		double denominatorConstant, double denominatorCosine, double denominatorSine,
		double minimum, double maximum, Callback&& callback) {
	// d((A+B cos+C sin)/(D+E cos+F sin))/d(theta) has the
	// form k0 + kc cos(theta) + ks sin(theta). Enumerating its roots gives
	// the exact continuous extrema without frame-rate-dependent sampling.
	const double k0 = numeratorSine * denominatorCosine
			- numeratorCosine * denominatorSine;
	const double kc = numeratorSine * denominatorConstant
			- numeratorConstant * denominatorSine;
	const double ks = -numeratorCosine * denominatorConstant
			+ numeratorConstant * denominatorCosine;
	const double radius = std::hypot(kc, ks);
	if (!std::isfinite(radius) || radius <= 1.0e-15) {
		return;
	}
	const double ratio = -k0 / radius;
	if (!std::isfinite(ratio) || ratio < -1.0 - 1.0e-12 || ratio > 1.0 + 1.0e-12) {
		return;
	}
	const double phase = std::atan2(ks, kc);
	const double offset = std::acos(std::clamp(ratio, -1.0, 1.0));
	forEachPeriodicAngle(phase + offset, 2.0 * pi, minimum, maximum, callback);
	if (offset > 1.0e-14 && std::abs(offset - pi) > 1.0e-14) {
		forEachPeriodicAngle(phase - offset, 2.0 * pi, minimum, maximum, callback);
	}
}

CantedFovExpansionResult includeCantedCornerEnvelope(
		DVec3 corner, DVec3 rollAxis, double coverage, Bounds& bounds) noexcept {
	const DVec3 parallel = multiply(rollAxis, dot(rollAxis, corner));
	const SinusoidVector rotated{
			parallel,
			subtract(corner, parallel),
			cross(rollAxis, corner)};
	CantedFovExpansionResult status = CantedFovExpansionResult::success;
	const auto include = [&](double angle) {
		if (status != CantedFovExpansionResult::success
				|| angle < -coverage - 1.0e-12 || angle > coverage + 1.0e-12) {
			return;
		}
		const DVec3 ray = evaluate(rotated, std::clamp(angle, -coverage, coverage));
		if (!std::isfinite(ray.x) || !std::isfinite(ray.y) || !std::isfinite(ray.z)) {
			status = CantedFovExpansionResult::invalidInput;
			return;
		}
		if (ray.z >= -minimumForward) {
			status = CantedFovExpansionResult::eyePlaneCrossing;
			return;
		}
		const double tangentX = -ray.x / ray.z;
		const double tangentY = -ray.y / ray.z;
		if (!std::isfinite(tangentX) || !std::isfinite(tangentY)) {
			status = CantedFovExpansionResult::invalidInput;
			return;
		}
		bounds.minimumX = std::min(bounds.minimumX, tangentX);
		bounds.maximumX = std::max(bounds.maximumX, tangentX);
		bounds.minimumY = std::min(bounds.minimumY, tangentY);
		bounds.maximumY = std::max(bounds.maximumY, tangentY);
	};

	include(-coverage);
	include(coverage);
	include(0.0);
	includeRatioStationaryAngles(
			rotated.constant.x, rotated.cosine.x, rotated.sine.x,
			rotated.constant.z, rotated.cosine.z, rotated.sine.z,
			-coverage, coverage, include);
	includeRatioStationaryAngles(
			rotated.constant.y, rotated.cosine.y, rotated.sine.y,
			rotated.constant.z, rotated.cosine.z, rotated.sine.z,
			-coverage, coverage, include);

	// A perspective ratio is only bounded while the complete ray stays in
	// front of the submitted eye. Check the maximum z analytically as well.
	const double zPhase = std::atan2(rotated.sine.z, rotated.cosine.z);
	forEachPeriodicAngle(zPhase, 2.0 * pi, -coverage, coverage, include);
	forEachPeriodicAngle(zPhase + pi, 2.0 * pi, -coverage, coverage, include);
	return status;
}

void includeRotated(double x, double y, double angle, Bounds& bounds) noexcept {
	const double cosine = std::cos(angle);
	const double sine = std::sin(angle);
	const double rotatedX = x * cosine - y * sine;
	const double rotatedY = x * sine + y * cosine;
	bounds.minimumX = std::min(bounds.minimumX, rotatedX);
	bounds.maximumX = std::max(bounds.maximumX, rotatedX);
	bounds.minimumY = std::min(bounds.minimumY, rotatedY);
	bounds.maximumY = std::max(bounds.maximumY, rotatedY);
}

void includeIfCovered(
		double x, double y, double angle, double coverage, Bounds& bounds) noexcept {
	if (angle >= -coverage - 1.0e-12 && angle <= coverage + 1.0e-12) {
		includeRotated(x, y, std::clamp(angle, -coverage, coverage), bounds);
	}
}

void includeCornerExtrema(
		double x, double y, double coverage, Bounds& bounds) noexcept {
	includeRotated(x, y, -coverage, bounds);
	includeRotated(x, y, coverage, bounds);
	includeRotated(x, y, 0.0, bounds);

	// x' = r*cos(phi + angle), y' = r*sin(phi + angle). Check every
	// stationary point that can lie inside the configured roll interval.
	const double phase = std::atan2(y, x);
	for (int turn = -2; turn <= 2; ++turn) {
		includeIfCovered(x, y, static_cast<double>(turn) * pi - phase, coverage, bounds);
		includeIfCovered(
				x, y, halfPi + static_cast<double>(turn) * pi - phase, coverage, bounds);
	}
}

} // namespace

bool composeRollFreeEyePoses(
		const Pose& headCenter,
		const std::array<Pose, 2>& relativeEyes,
		RollFreeBasisState& state,
		std::array<Pose, 2>& output) noexcept {
	if (!finite(headCenter.position) || !finite(headCenter.orientation)) {
		return false;
	}
	for (const Pose& eye : relativeEyes) {
		if (!finite(eye.position) || !finite(eye.orientation)) {
			return false;
		}
	}

	RollFreeBasisState candidateState = state;
	Pose screenPose;
	if (!computeGazeCenteredRollFreePose(
			headCenter, 1.0F, candidateState, screenPose)) {
		return false;
	}

	std::array<Pose, 2> candidateOutput;
	for (std::size_t index = 0; index < relativeEyes.size(); ++index) {
		Quaternion relativeOrientation;
		if (!normalizeQuaternion(relativeEyes[index].orientation, relativeOrientation)) {
			return false;
		}
		Quaternion orientation;
		if (!multiplyQuaternion(screenPose.orientation, relativeOrientation, orientation)) {
			return false;
		}
		const Vec3 rotatedOffset = rotateVector(
				screenPose.orientation, relativeEyes[index].position);
		const Vec3 position = add(headCenter.position, rotatedOffset);
		if (!finite(position)) {
			return false;
		}
		candidateOutput[index] = Pose{orientation, position};
	}

	state = candidateState;
	output = candidateOutput;
	return true;
}

bool expandCenteredFovForRollCoverage(
		ProjectionFov input,
		float coverageDegrees,
		ProjectionFov& output) noexcept {
	if (!validFov(input) || !finite(coverageDegrees)
			|| coverageDegrees < 0.0F || coverageDegrees > 45.0F) {
		return false;
	}

	const double coverage = static_cast<double>(coverageDegrees) * pi / 180.0;
	const double inputLeft = std::tan(static_cast<double>(input.angleLeft));
	const double inputRight = std::tan(static_cast<double>(input.angleRight));
	const double inputDown = std::tan(static_cast<double>(input.angleDown));
	const double inputUp = std::tan(static_cast<double>(input.angleUp));
	if (!std::isfinite(inputLeft) || !std::isfinite(inputRight)
			|| !std::isfinite(inputDown) || !std::isfinite(inputUp)) {
		return false;
	}
	// A flat ReShade eye is centered at UV 0.5. Symmetrizing in tangent
	// space keeps that optical center at tan(0) while still containing the
	// runtime's potentially asymmetric per-eye frustum.
	const double horizontal = std::max(std::abs(inputLeft), std::abs(inputRight));
	const double vertical = std::max(std::abs(inputDown), std::abs(inputUp));
	const double left = -horizontal;
	const double right = horizontal;
	const double down = -vertical;
	const double up = vertical;

	Bounds bounds;
	for (double x : std::array<double, 2>{left, right}) {
		for (double y : std::array<double, 2>{down, up}) {
			includeCornerExtrema(x, y, coverage, bounds);
		}
	}
	if (!std::isfinite(bounds.minimumX) || !std::isfinite(bounds.maximumX)
			|| !std::isfinite(bounds.minimumY) || !std::isfinite(bounds.maximumY)
			|| bounds.maximumX - bounds.minimumX <= minimumSpan
			|| bounds.maximumY - bounds.minimumY <= minimumSpan) {
		return false;
	}

	ProjectionFov candidate{
			static_cast<float>(std::atan(bounds.minimumX)),
			static_cast<float>(std::atan(bounds.maximumX)),
			static_cast<float>(std::atan(bounds.maximumY)),
			static_cast<float>(std::atan(bounds.minimumY)),
	};
	if (!validFov(candidate)) {
		return false;
	}
	output = candidate;
	return true;
}

bool expandCantedFovForRollCoverage(
		ProjectionFov input,
		Quaternion relativeEyeOrientation,
		float coverageDegrees,
		ProjectionFov& output) noexcept {
	return computeCantedFovForRollCoverage(
			input, relativeEyeOrientation, coverageDegrees, output)
			== CantedFovExpansionResult::success;
}

CantedFovExpansionResult computeCantedFovForRollCoverage(
		ProjectionFov input,
		Quaternion relativeEyeOrientation,
		float coverageDegrees,
		ProjectionFov& output) noexcept {
	if (!validFov(input) || !finite(coverageDegrees)
			|| coverageDegrees < 0.0F || coverageDegrees > 45.0F) {
		return CantedFovExpansionResult::invalidInput;
	}
	Quaternion orientation;
	if (!normalizeQuaternion(relativeEyeOrientation, orientation)) {
		return CantedFovExpansionResult::invalidInput;
	}
	const Quaternion inverse{
			-orientation.x, -orientation.y, -orientation.z, orientation.w};
	const Vec3 axisFloat = rotateVector(inverse, Vec3{0.0F, 0.0F, -1.0F});
	DVec3 rollAxis{axisFloat.x, axisFloat.y, axisFloat.z};
	const double axisLength = std::sqrt(dot(rollAxis, rollAxis));
	if (!std::isfinite(axisLength) || axisLength <= 1.0e-12) {
		return CantedFovExpansionResult::invalidInput;
	}
	rollAxis = multiply(rollAxis, 1.0 / axisLength);

	const double left = std::tan(static_cast<double>(input.angleLeft));
	const double right = std::tan(static_cast<double>(input.angleRight));
	const double down = std::tan(static_cast<double>(input.angleDown));
	const double up = std::tan(static_cast<double>(input.angleUp));
	if (!std::isfinite(left) || !std::isfinite(right)
			|| !std::isfinite(down) || !std::isfinite(up)) {
		return CantedFovExpansionResult::invalidInput;
	}

	const double coverage = static_cast<double>(coverageDegrees) * pi / 180.0;
	Bounds bounds;
	for (double x : std::array<double, 2>{left, right}) {
		for (double y : std::array<double, 2>{down, up}) {
			const CantedFovExpansionResult cornerResult = includeCantedCornerEnvelope(
					DVec3{x, y, -1.0}, rollAxis, coverage, bounds);
			if (cornerResult != CantedFovExpansionResult::success) {
				return cornerResult;
			}
		}
	}
	if (!std::isfinite(bounds.minimumX) || !std::isfinite(bounds.maximumX)
			|| !std::isfinite(bounds.minimumY) || !std::isfinite(bounds.maximumY)) {
		return CantedFovExpansionResult::invalidInput;
	}

	// The ReShade eye is a centered rectilinear camera. Symmetrizing the exact
	// envelope retains its optical forward ray and makes roll coverage constant.
	const double horizontal = std::max(
			std::abs(bounds.minimumX), std::abs(bounds.maximumX));
	const double vertical = std::max(
			std::abs(bounds.minimumY), std::abs(bounds.maximumY));
	if (!std::isfinite(horizontal) || !std::isfinite(vertical)
			|| horizontal <= minimumSpan || vertical <= minimumSpan) {
		return CantedFovExpansionResult::invalidInput;
	}
	const ProjectionFov candidate{
			static_cast<float>(-std::atan(horizontal)),
			static_cast<float>(std::atan(horizontal)),
			static_cast<float>(std::atan(vertical)),
			static_cast<float>(-std::atan(vertical))};
	if (!validFov(candidate)) {
		return CantedFovExpansionResult::invalidInput;
	}
	output = candidate;
	return CantedFovExpansionResult::success;
}

bool expandProjectionFovByAngularGuard(
		ProjectionFov input,
		float guardDegrees,
		ProjectionFov& output) noexcept {
	if (!validFov(input) || !finite(guardDegrees)
			|| guardDegrees < 0.0F || guardDegrees > 45.0F) {
		return false;
	}
	const double guard = static_cast<double>(guardDegrees) * pi / 180.0;
	const ProjectionFov candidate{
			static_cast<float>(static_cast<double>(input.angleLeft) - guard),
			static_cast<float>(static_cast<double>(input.angleRight) + guard),
			static_cast<float>(static_cast<double>(input.angleUp) + guard),
			static_cast<float>(static_cast<double>(input.angleDown) - guard)};
	if (!validFov(candidate)) {
		return false;
	}
	output = candidate;
	return true;
}

bool expandProjectionFovForWorldViewScale(
		ProjectionFov input,
		float worldViewScale,
		ProjectionFov& output) noexcept {
	if (!validFov(input) || !finite(worldViewScale)
			|| worldViewScale < minimumWorldViewScale
			|| worldViewScale > maximumWorldViewScale) {
		return false;
	}
	if (worldViewScale == maximumWorldViewScale) {
		output = input;
		return true;
	}

	const double inverseScale = 1.0 / static_cast<double>(worldViewScale);
	const double left = std::tan(static_cast<double>(input.angleLeft)) * inverseScale;
	const double right = std::tan(static_cast<double>(input.angleRight)) * inverseScale;
	const double up = std::tan(static_cast<double>(input.angleUp)) * inverseScale;
	const double down = std::tan(static_cast<double>(input.angleDown)) * inverseScale;
	if (!std::isfinite(left) || !std::isfinite(right)
			|| !std::isfinite(up) || !std::isfinite(down)) {
		return false;
	}

	ProjectionFov candidate{
			static_cast<float>(std::atan(left)),
			static_cast<float>(std::atan(right)),
			static_cast<float>(std::atan(up)),
			static_cast<float>(std::atan(down)),
	};
	if (!validFov(candidate)) {
		return false;
	}
	output = candidate;
	return true;
}

bool projectionFovContains(ProjectionFov outer, ProjectionFov inner) noexcept {
	if (!validFov(outer) || !validFov(inner)) {
		return false;
	}
	const double outerLeft = std::tan(static_cast<double>(outer.angleLeft));
	const double outerRight = std::tan(static_cast<double>(outer.angleRight));
	const double outerDown = std::tan(static_cast<double>(outer.angleDown));
	const double outerUp = std::tan(static_cast<double>(outer.angleUp));
	const double innerLeft = std::tan(static_cast<double>(inner.angleLeft));
	const double innerRight = std::tan(static_cast<double>(inner.angleRight));
	const double innerDown = std::tan(static_cast<double>(inner.angleDown));
	const double innerUp = std::tan(static_cast<double>(inner.angleUp));
	// Ignore sub-pixel-scale float jitter without changing the frozen mapping.
	constexpr double tolerance = 1.0e-4;
	return outerLeft <= innerLeft + tolerance
			&& outerRight + tolerance >= innerRight
			&& outerDown <= innerDown + tolerance
			&& outerUp + tolerance >= innerUp;
}

SourceProjectionMappingResult computeProjectionSourceUvTransform(
		float sourceAspect,
		float sourceVerticalFovDegrees,
		ProjectionFov targetFov,
		SourceUvTransform& output) noexcept {
	if (!finite(sourceAspect) || sourceAspect <= 0.0F
			|| !finite(sourceVerticalFovDegrees)
			|| sourceVerticalFovDegrees <= 0.0F
			|| sourceVerticalFovDegrees >= 180.0F
			|| !validFov(targetFov)) {
		return SourceProjectionMappingResult::invalidInput;
	}

	const double sourceHalfHeight = std::tan(
			static_cast<double>(sourceVerticalFovDegrees) * pi / 360.0);
	const double sourceHalfWidth = sourceHalfHeight * sourceAspect;
	const double targetLeft = std::tan(static_cast<double>(targetFov.angleLeft));
	const double targetRight = std::tan(static_cast<double>(targetFov.angleRight));
	const double targetUp = std::tan(static_cast<double>(targetFov.angleUp));
	const double targetDown = std::tan(static_cast<double>(targetFov.angleDown));
	if (!std::isfinite(sourceHalfHeight) || !std::isfinite(sourceHalfWidth)
			|| sourceHalfHeight <= 0.0 || sourceHalfWidth <= 0.0
			|| !std::isfinite(targetLeft) || !std::isfinite(targetRight)
			|| !std::isfinite(targetUp) || !std::isfinite(targetDown)) {
		return SourceProjectionMappingResult::invalidInput;
	}

	const double scaleX = (targetRight - targetLeft) / (2.0 * sourceHalfWidth);
	const double offsetX = 0.5 + targetLeft / (2.0 * sourceHalfWidth);
	// Texture V grows downward while positive projection tangent Y points up.
	const double scaleY = (targetUp - targetDown) / (2.0 * sourceHalfHeight);
	const double offsetY = 0.5 - targetUp / (2.0 * sourceHalfHeight);
	constexpr double boundsTolerance = 1.0e-7;
	if (offsetX < -boundsTolerance || offsetY < -boundsTolerance
			|| offsetX + scaleX > 1.0 + boundsTolerance
			|| offsetY + scaleY > 1.0 + boundsTolerance) {
		return SourceProjectionMappingResult::insufficientSourceFov;
	}

	SourceUvTransform candidate{
			static_cast<float>(scaleX),
			static_cast<float>(scaleY),
			static_cast<float>(std::clamp(offsetX, 0.0, 1.0)),
			static_cast<float>(std::clamp(offsetY, 0.0, 1.0)),
	};
	if (!finite(candidate.scaleX) || !finite(candidate.scaleY)
			|| !finite(candidate.offsetX) || !finite(candidate.offsetY)
			|| candidate.scaleX <= 0.0F || candidate.scaleY <= 0.0F
			|| candidate.offsetX + candidate.scaleX > 1.0F + 1.0e-5F
			|| candidate.offsetY + candidate.scaleY > 1.0F + 1.0e-5F) {
		return SourceProjectionMappingResult::invalidInput;
	}
	output = candidate;
	return SourceProjectionMappingResult::success;
}

bool computeProjectionSubFovUvTransform(
		ProjectionFov outerFov,
		ProjectionFov innerFov,
		SourceUvTransform& output) noexcept {
	if (!validFov(outerFov) || !validFov(innerFov)
			|| !projectionFovContains(outerFov, innerFov)) {
		return false;
	}

	const double outerLeft = std::tan(static_cast<double>(outerFov.angleLeft));
	const double outerRight = std::tan(static_cast<double>(outerFov.angleRight));
	const double outerDown = std::tan(static_cast<double>(outerFov.angleDown));
	const double outerUp = std::tan(static_cast<double>(outerFov.angleUp));
	const double innerLeft = std::tan(static_cast<double>(innerFov.angleLeft));
	const double innerRight = std::tan(static_cast<double>(innerFov.angleRight));
	const double innerDown = std::tan(static_cast<double>(innerFov.angleDown));
	const double innerUp = std::tan(static_cast<double>(innerFov.angleUp));
	const double outerWidth = outerRight - outerLeft;
	const double outerHeight = outerUp - outerDown;
	if (!std::isfinite(outerWidth) || !std::isfinite(outerHeight)
			|| outerWidth <= minimumSpan || outerHeight <= minimumSpan) {
		return false;
	}

	const double offsetX = (innerLeft - outerLeft) / outerWidth;
	const double right = (innerRight - outerLeft) / outerWidth;
	// Texture V grows down from the projection's upper tangent edge.
	const double offsetY = (outerUp - innerUp) / outerHeight;
	const double bottom = (outerUp - innerDown) / outerHeight;
	constexpr double boundsTolerance = 1.0e-7;
	if (!std::isfinite(offsetX) || !std::isfinite(right)
			|| !std::isfinite(offsetY) || !std::isfinite(bottom)
			|| offsetX < -boundsTolerance || offsetY < -boundsTolerance
			|| right > 1.0 + boundsTolerance || bottom > 1.0 + boundsTolerance
			|| right - offsetX <= minimumSpan || bottom - offsetY <= minimumSpan) {
		return false;
	}

	const double clampedLeft = std::clamp(offsetX, 0.0, 1.0);
	const double clampedRight = std::clamp(right, 0.0, 1.0);
	const double clampedTop = std::clamp(offsetY, 0.0, 1.0);
	const double clampedBottom = std::clamp(bottom, 0.0, 1.0);
	const SourceUvTransform candidate{
			static_cast<float>(clampedRight - clampedLeft),
			static_cast<float>(clampedBottom - clampedTop),
			static_cast<float>(clampedLeft),
			static_cast<float>(clampedTop),
	};
	if (!finite(candidate.scaleX) || !finite(candidate.scaleY)
			|| !finite(candidate.offsetX) || !finite(candidate.offsetY)
			|| candidate.scaleX <= 0.0F || candidate.scaleY <= 0.0F) {
		return false;
	}
	output = candidate;
	return true;
}

bool computeMinimumSourceVerticalFovDegrees(
		float sourceAspect,
		ProjectionFov targetFov,
		float& output) noexcept {
	if (!finite(sourceAspect) || sourceAspect <= 0.0F || !validFov(targetFov)) {
		return false;
	}

	const double left = std::tan(static_cast<double>(targetFov.angleLeft));
	const double right = std::tan(static_cast<double>(targetFov.angleRight));
	const double up = std::tan(static_cast<double>(targetFov.angleUp));
	const double down = std::tan(static_cast<double>(targetFov.angleDown));
	if (!std::isfinite(left) || !std::isfinite(right)
			|| !std::isfinite(up) || !std::isfinite(down)) {
		return false;
	}

	// A centered source spans +/-halfHeight vertically and
	// +/-halfHeight*aspect horizontally in tangent space.
	const double requiredHalfHeight = std::max(
			std::max(std::abs(up), std::abs(down)),
			std::max(std::abs(left), std::abs(right))
					/ static_cast<double>(sourceAspect));
	const double degrees = 2.0 * std::atan(requiredHalfHeight) * 180.0 / pi;
	if (!std::isfinite(degrees) || degrees <= 0.0 || degrees >= 180.0) {
		return false;
	}
	const float candidate = static_cast<float>(degrees);
	if (!finite(candidate)) {
		return false;
	}
	output = candidate;
	return true;
}

bool computeMaximumSupportedRollCoverage(
		ProjectionFov runtimeFov,
		float sourceAspect,
		float sourceVerticalFovDegrees,
		MaximumRollCoverage& output) noexcept {
	return computeMaximumSupportedRollCoverage(
			runtimeFov, Quaternion{}, sourceAspect, sourceVerticalFovDegrees,
			0.0F, maximumWorldViewScale, output);
}

bool computeMaximumSupportedRollCoverage(
		ProjectionFov runtimeFov,
		float sourceAspect,
		float sourceVerticalFovDegrees,
		float worldViewScale,
		MaximumRollCoverage& output) noexcept {
	return computeMaximumSupportedRollCoverage(
			runtimeFov, Quaternion{}, sourceAspect, sourceVerticalFovDegrees,
			0.0F, worldViewScale, output);
}

bool computeMaximumSupportedRollCoverage(
		ProjectionFov runtimeFov,
		Quaternion relativeEyeOrientation,
		float sourceAspect,
		float sourceVerticalFovDegrees,
		float fovGuardDegrees,
		MaximumRollCoverage& output) noexcept {
	return computeMaximumSupportedRollCoverage(
			runtimeFov, relativeEyeOrientation, sourceAspect,
			sourceVerticalFovDegrees, fovGuardDegrees,
			maximumWorldViewScale, output);
}

bool computeMaximumSupportedRollCoverage(
		ProjectionFov runtimeFov,
		Quaternion relativeEyeOrientation,
		float sourceAspect,
		float sourceVerticalFovDegrees,
		float fovGuardDegrees,
		float worldViewScale,
		MaximumRollCoverage& output) noexcept {
	if (!validFov(runtimeFov)
			|| !finite(sourceAspect) || sourceAspect <= 0.0F
			|| !finite(sourceVerticalFovDegrees)
			|| sourceVerticalFovDegrees <= 0.0F
			|| sourceVerticalFovDegrees >= 180.0F
			|| !finite(fovGuardDegrees) || fovGuardDegrees < 0.0F
			|| fovGuardDegrees > 45.0F
			|| !finite(worldViewScale)
			|| worldViewScale < minimumWorldViewScale
			|| worldViewScale > maximumWorldViewScale) {
		return false;
	}
	Quaternion normalizedOrientation;
	if (!normalizeQuaternion(relativeEyeOrientation, normalizedOrientation)) {
		return false;
	}

	auto mappingAt = [&](float coverageDegrees) noexcept {
		ProjectionFov expanded;
		ProjectionFov guarded;
		ProjectionFov samplingFov;
		const CantedFovExpansionResult expansionResult =
				computeCantedFovForRollCoverage(
					runtimeFov, normalizedOrientation, coverageDegrees, expanded);
		if (expansionResult == CantedFovExpansionResult::eyePlaneCrossing) {
			return SourceProjectionMappingResult::insufficientSourceFov;
		}
		if (expansionResult != CantedFovExpansionResult::success
				|| !expandProjectionFovByAngularGuard(
					expanded, fovGuardDegrees, guarded)
				|| !expandProjectionFovForWorldViewScale(
					guarded, worldViewScale, samplingFov)) {
			return SourceProjectionMappingResult::invalidInput;
		}
		SourceUvTransform mapping;
		return computeProjectionSourceUvTransform(
				sourceAspect, sourceVerticalFovDegrees, samplingFov, mapping);
	};

	const SourceProjectionMappingResult zeroResult = mappingAt(0.0F);
	if (zeroResult == SourceProjectionMappingResult::invalidInput) {
		return false;
	}
	if (zeroResult == SourceProjectionMappingResult::insufficientSourceFov) {
		output = MaximumRollCoverage{0.0F, false};
		return true;
	}

	const SourceProjectionMappingResult maximumResult = mappingAt(45.0F);
	if (maximumResult == SourceProjectionMappingResult::invalidInput) {
		return false;
	}
	if (maximumResult == SourceProjectionMappingResult::success) {
		output = MaximumRollCoverage{45.0F, true};
		return true;
	}

	// Containment is monotonic because the canted expansion takes
	// the union of every rolled frustum in [-coverage, +coverage]. Bisection is
	// therefore deterministic and avoids duplicating that bound's corner logic.
	float supported = 0.0F;
	float unsupported = 45.0F;
	for (int iteration = 0; iteration < 32; ++iteration) {
		const float midpoint = supported + (unsupported - supported) * 0.5F;
		if (midpoint == supported || midpoint == unsupported) {
			break;
		}
		const SourceProjectionMappingResult result = mappingAt(midpoint);
		if (result == SourceProjectionMappingResult::invalidInput) {
			return false;
		}
		if (result == SourceProjectionMappingResult::success) {
			supported = midpoint;
		} else {
			unsupported = midpoint;
		}
	}

	output = MaximumRollCoverage{supported, true};
	return true;
}

} // namespace mcxrinput::native
