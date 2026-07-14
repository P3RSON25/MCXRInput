#include <mcxrinput/screen_pose_math.hpp>

#include <algorithm>
#include <cmath>

namespace mcxrinput::native {
namespace {

constexpr double minimumLengthSquared = 1.0e-12;
// When gaze approaches gravity-up/down, a gravity-level screen has no unique
// horizontal heading. Preserve the last valid right axis in that small cone;
// separate enter/exit thresholds prevent tracking noise from toggling modes.
constexpr double verticalFallbackEnterLengthSquared = 0.0012183459618085; // sin(2 deg)^2
constexpr double verticalFallbackExitLengthSquared = 0.0027390523158633; // sin(3 deg)^2

struct DVec3 {
	double x;
	double y;
	double z;
};

bool finite(double value) noexcept {
	return std::isfinite(value);
}

bool finite(Vec3 value) noexcept {
	return finite(value.x) && finite(value.y) && finite(value.z);
}

DVec3 toDouble(Vec3 value) noexcept {
	return DVec3{value.x, value.y, value.z};
}

Vec3 toFloat(DVec3 value) noexcept {
	return Vec3{
			static_cast<float>(value.x),
			static_cast<float>(value.y),
			static_cast<float>(value.z),
	};
}

DVec3 add(DVec3 left, DVec3 right) noexcept {
	return DVec3{left.x + right.x, left.y + right.y, left.z + right.z};
}

DVec3 subtract(DVec3 left, DVec3 right) noexcept {
	return DVec3{left.x - right.x, left.y - right.y, left.z - right.z};
}

DVec3 multiply(DVec3 value, double scalar) noexcept {
	return DVec3{value.x * scalar, value.y * scalar, value.z * scalar};
}

double dot(DVec3 left, DVec3 right) noexcept {
	return left.x * right.x + left.y * right.y + left.z * right.z;
}

DVec3 cross(DVec3 left, DVec3 right) noexcept {
	return DVec3{
			left.y * right.z - left.z * right.y,
			left.z * right.x - left.x * right.z,
			left.x * right.y - left.y * right.x,
	};
}

double lengthSquared(DVec3 value) noexcept {
	return dot(value, value);
}

bool normalize(DVec3 input, DVec3& output, double minimum = minimumLengthSquared) noexcept {
	const double squared = lengthSquared(input);
	if (!finite(squared) || squared <= minimum) {
		return false;
	}
	output = multiply(input, 1.0 / std::sqrt(squared));
	return finite(output.x) && finite(output.y) && finite(output.z);
}

DVec3 projectOntoPlane(DVec3 value, DVec3 unitNormal) noexcept {
	return subtract(value, multiply(unitNormal, dot(value, unitNormal)));
}

Quaternion multiply(Quaternion left, Quaternion right) noexcept {
	return Quaternion{
			left.w * right.x + left.x * right.w
					+ left.y * right.z - left.z * right.y,
			left.w * right.y - left.x * right.z
					+ left.y * right.w + left.z * right.x,
			left.w * right.z + left.x * right.y
					- left.y * right.x + left.z * right.w,
			left.w * right.w - left.x * right.x
					- left.y * right.y - left.z * right.z,
	};
}

bool quaternionFromBasis(
		DVec3 right, DVec3 up, DVec3 front,
		Quaternion& output) noexcept {
	// Matrix columns map quad-local +X/+Y/+Z into LOCAL coordinates.
	const double m00 = right.x;
	const double m01 = up.x;
	const double m02 = front.x;
	const double m10 = right.y;
	const double m11 = up.y;
	const double m12 = front.y;
	const double m20 = right.z;
	const double m21 = up.z;
	const double m22 = front.z;

	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
	double w = 1.0;
	const double trace = m00 + m11 + m22;
	if (trace > 0.0) {
		const double scale = std::sqrt(trace + 1.0) * 2.0;
		if (scale <= 0.0) {
			return false;
		}
		w = 0.25 * scale;
		x = (m21 - m12) / scale;
		y = (m02 - m20) / scale;
		z = (m10 - m01) / scale;
	} else if (m00 > m11 && m00 > m22) {
		const double scale = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
		if (scale <= 0.0) {
			return false;
		}
		w = (m21 - m12) / scale;
		x = 0.25 * scale;
		y = (m01 + m10) / scale;
		z = (m02 + m20) / scale;
	} else if (m11 > m22) {
		const double scale = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
		if (scale <= 0.0) {
			return false;
		}
		w = (m02 - m20) / scale;
		x = (m01 + m10) / scale;
		y = 0.25 * scale;
		z = (m12 + m21) / scale;
	} else {
		const double scale = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
		if (scale <= 0.0) {
			return false;
		}
		w = (m10 - m01) / scale;
		x = (m02 + m20) / scale;
		y = (m12 + m21) / scale;
		z = 0.25 * scale;
	}

	Quaternion candidate{
			static_cast<float>(x),
			static_cast<float>(y),
			static_cast<float>(z),
			static_cast<float>(w),
	};
	if (!normalizeQuaternion(candidate, candidate)) {
		return false;
	}
	if (candidate.w < 0.0F) {
		candidate.x = -candidate.x;
		candidate.y = -candidate.y;
		candidate.z = -candidate.z;
		candidate.w = -candidate.w;
	}
	output = candidate;
	return true;
}

} // namespace

bool normalizeQuaternion(Quaternion input, Quaternion& output) noexcept {
	const double x = input.x;
	const double y = input.y;
	const double z = input.z;
	const double w = input.w;
	const double squared = x * x + y * y + z * z + w * w;
	if (!finite(squared) || squared <= minimumLengthSquared) {
		return false;
	}
	const double inverse = 1.0 / std::sqrt(squared);
	Quaternion candidate{
			static_cast<float>(x * inverse),
			static_cast<float>(y * inverse),
			static_cast<float>(z * inverse),
			static_cast<float>(w * inverse),
	};
	if (!finite(candidate.x) || !finite(candidate.y)
			|| !finite(candidate.z) || !finite(candidate.w)) {
		return false;
	}
	output = candidate;
	return true;
}

Vec3 rotateVector(Quaternion unitQuaternion, Vec3 vector) noexcept {
	const DVec3 q{unitQuaternion.x, unitQuaternion.y, unitQuaternion.z};
	const DVec3 v = toDouble(vector);
	const DVec3 twiceCross = multiply(cross(q, v), 2.0);
	return toFloat(add(v, add(multiply(twiceCross, unitQuaternion.w), cross(q, twiceCross))));
}

bool computeGazeCenteredRollFreePose(
		const Pose& headPose,
		float distanceMeters,
		RollFreeBasisState& state,
		Pose& output) noexcept {
	if (!finite(headPose.position) || !finite(distanceMeters) || distanceMeters <= 0.0F) {
		return false;
	}

	Quaternion headOrientation;
	if (!normalizeQuaternion(headPose.orientation, headOrientation)) {
		return false;
	}

	DVec3 gaze;
	if (!normalize(toDouble(rotateVector(headOrientation, Vec3{0.0F, 0.0F, -1.0F})), gaze)) {
		return false;
	}
	const DVec3 front = multiply(gaze, -1.0);
	const DVec3 worldUp{0.0, 1.0, 0.0};

	DVec3 rightCandidate = cross(gaze, worldUp);
	DVec3 right;
	const double horizontalLengthSquared = lengthSquared(rightCandidate);
	const bool useVerticalFallback = state.hasPreviousRight
			&& (state.usingVerticalFallback
					? horizontalLengthSquared <= verticalFallbackExitLengthSquared
					: horizontalLengthSquared <= verticalFallbackEnterLengthSquared);
	if (useVerticalFallback || horizontalLengthSquared <= minimumLengthSquared) {
		if (state.hasPreviousRight) {
			rightCandidate = projectOntoPlane(toDouble(state.previousRight), front);
		}
		if (!normalize(rightCandidate, right)) {
			rightCandidate = projectOntoPlane(DVec3{1.0, 0.0, 0.0}, front);
		}
		if (!normalize(rightCandidate, right)) {
			rightCandidate = projectOntoPlane(DVec3{0.0, 0.0, 1.0}, front);
		}
		if (!normalize(rightCandidate, right)) {
			return false;
		}
	} else if (!normalize(rightCandidate, right)) {
		return false;
	}

	DVec3 up;
	if (!normalize(cross(front, right), up)) {
		return false;
	}
	if (!normalize(cross(up, front), right)) {
		return false;
	}

	Quaternion screenOrientation;
	if (!quaternionFromBasis(right, up, front, screenOrientation)) {
		return false;
	}
	const DVec3 screenPosition = add(
			toDouble(headPose.position), multiply(gaze, distanceMeters));
	if (!finite(screenPosition.x) || !finite(screenPosition.y) || !finite(screenPosition.z)) {
		return false;
	}

	const Vec3 nextRight = toFloat(right);
	const Vec3 nextPosition = toFloat(screenPosition);
	if (!finite(nextRight) || !finite(nextPosition)) {
		return false;
	}
	const Pose nextOutput{screenOrientation, nextPosition};
	state.previousRight = nextRight;
	state.hasPreviousRight = true;
	state.usingVerticalFallback = useVerticalFallback
			|| horizontalLengthSquared <= minimumLengthSquared;
	output = nextOutput;
	return true;
}

bool computeGravityAlignedHmdRelativePose(
		const Pose& headPose,
		const Pose& localPose,
		RollFreeBasisState& state,
		Pose& output) noexcept {
	if (!finite(localPose.position)) {
		return false;
	}

	Quaternion localOrientation;
	if (!normalizeQuaternion(localPose.orientation, localOrientation)) {
		return false;
	}

	// Work on a copy so a valid HMD basis is not committed when the tracked
	// pose itself later proves unusable.
	RollFreeBasisState candidateState = state;
	Pose referencePose;
	if (!computeGazeCenteredRollFreePose(
			headPose, 1.0F, candidateState, referencePose)) {
		return false;
	}

	const Quaternion inverseReference{
			-referencePose.orientation.x,
			-referencePose.orientation.y,
			-referencePose.orientation.z,
			referencePose.orientation.w,
	};
	Quaternion relativeOrientation;
	if (!normalizeQuaternion(
			multiply(inverseReference, localOrientation),
			relativeOrientation)) {
		return false;
	}

	const Vec3 localOffset{
			localPose.position.x - headPose.position.x,
			localPose.position.y - headPose.position.y,
			localPose.position.z - headPose.position.z,
	};
	if (!finite(localOffset)) {
		return false;
	}
	const Vec3 relativePosition = rotateVector(inverseReference, localOffset);
	if (!finite(relativePosition)) {
		return false;
	}

	state = candidateState;
	output = Pose{relativeOrientation, relativePosition};
	return true;
}

} // namespace mcxrinput::native
