#pragma once

namespace mcxrinput::native {

struct Vec3 {
	float x{0.0F};
	float y{0.0F};
	float z{0.0F};
};

struct Quaternion {
	float x{0.0F};
	float y{0.0F};
	float z{0.0F};
	float w{1.0F};
};

struct Pose {
	Quaternion orientation{};
	Vec3 position{};
};

struct RollFreeBasisState {
	Vec3 previousRight{1.0F, 0.0F, 0.0F};
	bool hasPreviousRight{false};
	bool usingVerticalFallback{false};
};

bool normalizeQuaternion(Quaternion input, Quaternion& output) noexcept;
Vec3 rotateVector(Quaternion unitQuaternion, Vec3 vector) noexcept;

/**
 * Places a screen along the headset's physical gaze while rebuilding its up
 * axis from gravity-aligned LOCAL +Y. Head roll therefore cannot roll the
 * resulting screen. Invalid input leaves both output and state unchanged.
 */
bool computeGazeCenteredRollFreePose(
		const Pose& headPose,
		float distanceMeters,
		RollFreeBasisState& state,
		Pose& output) noexcept;

/**
 * Expresses a LOCAL-space pose relative to the HMD using the same
 * gravity-derived, roll-free gaze basis as the external screen. The returned
 * axes retain OpenXR's +X right, +Y up, and -Z forward convention, and the
 * returned position remains in meters. Invalid input leaves both output and
 * basis state unchanged.
 */
bool computeGravityAlignedHmdRelativePose(
		const Pose& headPose,
		const Pose& localPose,
		RollFreeBasisState& state,
		Pose& output) noexcept;

} // namespace mcxrinput::native
