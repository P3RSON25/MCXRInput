#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <optional>
#include <string>

namespace mcxrinput::native {

/** Optional gravity-aligned, HMD-relative OpenXR grip pose for one hand. */
struct BridgeGripPoseSnapshot {
	bool active{false};
	bool positionValid{false};
	bool positionTracked{false};
	bool orientationValid{false};
	bool orientationTracked{false};
	XrPosef pose{
			XrQuaternionf{0.0F, 0.0F, 0.0F, 1.0F},
			XrVector3f{0.0F, 0.0F, 0.0F}};
};

/** Current physical state for one controller hand. */
struct BridgeControllerSnapshot {
	bool active{false};
	XrVector2f stick{0.0F, 0.0F};
	float trigger{0.0F};
	float squeeze{0.0F};
	bool stickClick{false};
	bool a{false};
	bool b{false};
	bool x{false};
	bool y{false};
	bool menu{false};
	std::optional<BridgeGripPoseSnapshot> gripPose;
};

/**
 * Inputs to the production bridge's fail-closed publication gate. A submitted
 * frame means xrEndFrame accepted a layer for the same physical sample. In
 * display mode that layer must also contain a fresh captured Minecraft frame.
 */
struct BridgeInputReadiness {
	bool stopRequested{false};
	bool sessionRunning{false};
	bool sessionFocused{false};
	bool frameShouldRender{false};
	bool frameSubmitted{false};
	bool hmdOrientationValid{false};
	bool hmdOrientationTracked{false};
	bool actionsSynchronized{false};
	bool displayMode{false};
	bool hmdPositionValid{false};
	bool hmdPositionTracked{false};
	bool captureFresh{false};
};

[[nodiscard]] bool bridgeInputIsReady(const BridgeInputReadiness& readiness) noexcept;
[[nodiscard]] bool bridgeQuaternionIsPlausible(const XrQuaternionf& orientation) noexcept;

/** Returns the current wall-clock timestamp used only as protocol metadata. */
[[nodiscard]] std::int64_t bridgeTimestampNanos() noexcept;

/**
 * Serializes one protocol-v2 loopback datagram. If globalActive is false, the
 * quaternion is invalid, or a hand contains non-finite analog values, the
 * affected state is neutralized. An invalid optional grip pose is omitted
 * without changing that hand's buttons or analog inputs. Controller data can
 * never remain active in a globally inactive frame.
 */
[[nodiscard]] std::string makeBridgeDatagram(
		XrQuaternionf orientation,
		bool globalActive,
		BridgeControllerSnapshot leftController,
		BridgeControllerSnapshot rightController,
		std::int64_t timestampNanos);

[[nodiscard]] std::string makeNeutralBridgeDatagram(std::int64_t timestampNanos);

} // namespace mcxrinput::native
