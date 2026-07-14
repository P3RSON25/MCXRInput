#pragma once

#include <mcxrinput/screen_pose_math.hpp>

#include <array>

namespace mcxrinput::native {

inline constexpr float minimumWorldViewScale = 0.70F;
inline constexpr float maximumWorldViewScale = 1.0F;

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

enum class CantedFovExpansionResult {
	success,
	invalidInput,
	eyePlaneCrossing,
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

/**
 * Computes the centered tangent-space envelope needed when a runtime eye is
 * canted relative to VIEW and center-eye roll is removed. The complete
 * continuous roll interval is bounded; this is not a sampled approximation.
 */
bool expandCantedFovForRollCoverage(
		ProjectionFov input,
		Quaternion relativeEyeOrientation,
		float coverageDegrees,
		ProjectionFov& output) noexcept;

/** Detailed form used when eye-plane crossing is a supported-capacity limit. */
CantedFovExpansionResult computeCantedFovForRollCoverage(
		ProjectionFov input,
		Quaternion relativeEyeOrientation,
		float coverageDegrees,
		ProjectionFov& output) noexcept;

/** Adds a fixed outward angular guard to all four FOV edges. */
bool expandProjectionFovByAngularGuard(
		ProjectionFov input,
		float guardDegrees,
		ProjectionFov& output) noexcept;

/**
 * Expands every tangent-space edge by 1 / worldViewScale. The resulting FOV is
 * used only for source sampling; callers keep the original FOV for OpenXR
 * submission. A scale of 1 is an exact identity operation.
 */
bool expandProjectionFovForWorldViewScale(
		ProjectionFov input,
		float worldViewScale,
		ProjectionFov& output) noexcept;

bool projectionFovContains(ProjectionFov outer, ProjectionFov inner) noexcept;

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
 * Maps an inner projection frustum into a texture whose complete normalized
 * extent represents outerFov. Both frusta use the same eye-local tangent
 * coordinates. A successful result is contained in [0, 1]; invalid or
 * non-contained input leaves output unchanged.
 */
bool computeProjectionSubFovUvTransform(
		ProjectionFov outerFov,
		ProjectionFov innerFov,
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

/** World-view-scale form used by wider source-sampling diagnostics. */
bool computeMaximumSupportedRollCoverage(
		ProjectionFov runtimeFov,
		float sourceAspect,
		float sourceVerticalFovDegrees,
		float worldViewScale,
		MaximumRollCoverage& output) noexcept;

/** Canted-eye form used by the immersive projection path. */
bool computeMaximumSupportedRollCoverage(
		ProjectionFov runtimeFov,
		Quaternion relativeEyeOrientation,
		float sourceAspect,
		float sourceVerticalFovDegrees,
		float fovGuardDegrees,
		MaximumRollCoverage& output) noexcept;

/** Canted-eye form with an explicit source-sampling view scale. */
bool computeMaximumSupportedRollCoverage(
		ProjectionFov runtimeFov,
		Quaternion relativeEyeOrientation,
		float sourceAspect,
		float sourceVerticalFovDegrees,
		float fovGuardDegrees,
		float worldViewScale,
		MaximumRollCoverage& output) noexcept;

} // namespace mcxrinput::native
