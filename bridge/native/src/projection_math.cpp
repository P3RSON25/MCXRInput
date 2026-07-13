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

bool orientationNearIdentity(
		Quaternion orientation, float maximumDegrees) noexcept {
	if (!finite(maximumDegrees) || maximumDegrees < 0.0F || maximumDegrees > 180.0F) {
		return false;
	}
	Quaternion normalized;
	if (!normalizeQuaternion(orientation, normalized)) {
		return false;
	}
	// q and -q encode the same orientation, so use the shortest angular path.
	const double vectorLength = std::sqrt(
			static_cast<double>(normalized.x) * normalized.x
			+ static_cast<double>(normalized.y) * normalized.y
			+ static_cast<double>(normalized.z) * normalized.z);
	const double angleDegrees = 2.0 * std::atan2(
			vectorLength, std::abs(static_cast<double>(normalized.w))) * 180.0 / pi;
	return std::isfinite(angleDegrees)
			&& angleDegrees <= static_cast<double>(maximumDegrees) + 1.0e-6;
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

} // namespace mcxrinput::native
