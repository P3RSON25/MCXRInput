#pragma once

#include <mcxrinput/screen_pose_math.hpp>

#include <array>

namespace mcxrinput::native {

struct ProjectionFov {
	float angleLeft{0.0F};
	float angleRight{0.0F};
	float angleUp{0.0F};
	float angleDown{0.0F};
};

struct SourceUvTransform {
	float scaleX{1.0F};
	float scaleY{1.0F};
	float offsetX{0.0F};
	float offsetY{0.0F};
};

struct MaximumRollCoverage {
	float degrees{0.0F};
	bool supportsZeroCoverage{false};
};

enum class SourceProjectionMappingResult {
	success,
	invalidInput,
	insufficientSourceFov,
};

/**
 * Composes runtime-provided eye poses, expressed relative to VIEW, onto the
 * current VIEW pose with its roll removed. Invalid input leaves state and
 * output unchanged.
 */
bool composeRollFreeEyePoses(
		const Pose& headCenter,
		const std::array<Pose, 2>& relativeEyes,
		RollFreeBasisState& state,
		std::array<Pose, 2>& output) noexcept;

/**
 * Centers a runtime frustum around the source video's optical forward ray,
 * then expands it enough to cover the original tangent-plane rectangle while
 * it rotates through the requested roll range. The fixed expansion avoids zoom
 * changes as the headset rolls and safely contains asymmetric runtime FOVs.
 */
bool expandCenteredFovForRollCoverage(
		ProjectionFov input,
		float coverageDegrees,
		ProjectionFov& output) noexcept;

bool projectionFovContains(ProjectionFov outer, ProjectionFov inner) noexcept;
bool orientationNearIdentity(
		Quaternion orientation, float maximumDegrees) noexcept;

/**
 * Maps target-frustum tangent coordinates one-to-one into a centered,
 * rectilinear source eye. A successful transform is always contained within
 * one decoded eye; insufficient source FOV is reported separately.
 */
SourceProjectionMappingResult computeProjectionSourceUvTransform(
		float sourceAspect,
		float sourceVerticalFovDegrees,
		ProjectionFov targetFov,
		SourceUvTransform& output) noexcept;

/**
 * Computes the smallest centered rectilinear source vertical FOV that contains
 * every tangent-space edge of targetFov at sourceAspect. Invalid input leaves
 * output unchanged.
 */
bool computeMinimumSourceVerticalFovDegrees(
		float sourceAspect,
		ProjectionFov targetFov,
		float& output) noexcept;

/**
 * Finds the largest fixed roll interval in [0, 45] whose centered/expanded
 * runtime frustum fits the specified source projection. supportsZeroCoverage
 * distinguishes a true zero-degree limit from a source that cannot contain
 * even the centered runtime frustum. Invalid input leaves output unchanged.
 */
bool computeMaximumSupportedRollCoverage(
		ProjectionFov runtimeFov,
		float sourceAspect,
		float sourceVerticalFovDegrees,
		MaximumRollCoverage& output) noexcept;

} // namespace mcxrinput::native
