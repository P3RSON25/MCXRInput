#pragma once

#include <mcxrinput/projection_math.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mcxrinput::native {

inline constexpr std::string_view displayControlProtocolPrefix = "MCXRD1";
inline constexpr std::chrono::milliseconds displayStateFreshness{500};

enum class DisplayClientState {
	world,
	screen,
	overlay,
	noWorld,
};

enum class DisplayPresentationDecision {
	immersive,
	comfortQuad,
	waitForFreshCapture,
};

struct DisplayOffer {
	std::string session;
	std::uint64_t revision{0};
	std::uint32_t sourceFovMilli{110000};
	std::uint16_t hudXPermille{0};
	std::uint16_t hudYPermille{0};
};

struct DisplayStateReply {
	std::string session;
	std::uint64_t sequence{0};
	std::uint64_t revision{0};
	DisplayClientState state{DisplayClientState::noWorld};
	std::uint32_t appliedFovMilli{0};
};

struct HudInsetRecommendation {
	std::uint16_t horizontalPermille{0};
	std::uint16_t verticalPermille{0};
};

[[nodiscard]] bool isValidDisplaySessionToken(std::string_view token) noexcept;

/**
 * Serializes the exact bounded ASCII offer grammar. Invalid fields leave output
 * unchanged, allowing callers to fail closed without emitting a partial offer.
 */
[[nodiscard]] bool serializeDisplayOffer(
		const DisplayOffer& offer, std::string& output);

/**
 * Parses only the exact STATE grammar: one ASCII space between tokens, no
 * leading/trailing whitespace, and no unconsumed bytes. Failure leaves output
 * unchanged.
 */
[[nodiscard]] bool parseDisplayStateReply(
		std::string_view message, DisplayStateReply& output);

/**
 * Converts the largest cropped edge across both eyes into one conservative HUD
 * inset per axis, then adds fixed optical margins (6% horizontal, 9% vertical).
 * Invalid or out-of-bounds mappings leave output unchanged.
 */
[[nodiscard]] bool recommendHudInsets(
		const std::array<SourceUvTransform, 2>& sourceMappings,
		HudInsetRecommendation& output) noexcept;

/**
 * Owns only received display state. Socket I/O and capture remain external so
 * every freshness and transition decision is deterministic and unit-testable.
 */
class DisplayStateTracker {
public:
	using Clock = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;

	explicit DisplayStateTracker(DisplayOffer expectedOffer);

	[[nodiscard]] bool configured() const noexcept;
	[[nodiscard]] bool accept(
			const DisplayStateReply& reply, TimePoint receivedAt) noexcept;
	[[nodiscard]] DisplayPresentationDecision decide(
			TimePoint now,
			std::optional<TimePoint> latestCapturedFrameTime) const noexcept;

	[[nodiscard]] bool hasAcceptedState() const noexcept;
	[[nodiscard]] std::optional<TimePoint> lastHeartbeatTime() const noexcept;
	[[nodiscard]] std::optional<TimePoint> transitionBarrierTime() const noexcept;
	[[nodiscard]] std::optional<DisplayClientState> currentState() const noexcept;

private:
	DisplayOffer expectedOffer_;
	bool configured_{false};
	bool hasSequence_{false};
	std::uint64_t lastSequence_{0};
	std::optional<DisplayClientState> currentState_;
	std::optional<TimePoint> lastHeartbeatTime_;
	std::optional<TimePoint> transitionBarrierTime_;
};

} // namespace mcxrinput::native
