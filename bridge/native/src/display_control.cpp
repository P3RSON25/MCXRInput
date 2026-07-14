#include <mcxrinput/display_control.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <system_error>
#include <utility>

namespace mcxrinput::native {
namespace {

constexpr std::uint32_t minimumFovMilli = static_cast<std::uint32_t>(
		minimumSourceVerticalFovDegrees * 1000.0F);
constexpr std::uint32_t maximumFovMilli = static_cast<std::uint32_t>(
		maximumSourceVerticalFovDegrees * 1000.0F);
constexpr std::uint32_t maximumAppliedFovMilli = 360000;
constexpr std::uint16_t maximumHudPermille = 450;
constexpr std::uint16_t minimumWorldViewScalePermille = 300;
constexpr std::uint16_t maximumWorldViewScalePermille = 1000;
constexpr double horizontalOpticalMargin = 0.06;
constexpr double verticalOpticalMargin = 0.09;
constexpr double maximumHudInset = 0.45;
constexpr float sourceMappingTolerance = 1.0e-6F;

bool isHexDigit(char value) noexcept {
	return (value >= '0' && value <= '9')
			|| (value >= 'A' && value <= 'F')
			|| (value >= 'a' && value <= 'f');
}

template<typename Integer>
bool parseUnsigned(std::string_view token, Integer& output) noexcept {
	if (token.empty()) {
		return false;
	}

	Integer candidate{};
	const char* const begin = token.data();
	const char* const end = begin + token.size();
	const auto result = std::from_chars(begin, end, candidate, 10);
	if (result.ec != std::errc{} || result.ptr != end) {
		return false;
	}
	output = candidate;
	return true;
}

bool splitExactAsciiTokens(
		std::string_view message,
		std::array<std::string_view, 7>& tokens) noexcept {
	if (message.empty()) {
		return false;
	}

	std::size_t tokenIndex = 0;
	std::size_t tokenStart = 0;
	for (std::size_t index = 0; index < message.size(); ++index) {
		const unsigned char value = static_cast<unsigned char>(message[index]);
		if (value < 0x21U || value > 0x7EU) {
			if (value != static_cast<unsigned char>(' ')
					|| index == tokenStart || tokenIndex >= tokens.size()) {
				return false;
			}
			tokens[tokenIndex++] = message.substr(tokenStart, index - tokenStart);
			tokenStart = index + 1;
		}
	}

	if (tokenStart == message.size() || tokenIndex != tokens.size() - 1) {
		return false;
	}
	tokens[tokenIndex] = message.substr(tokenStart);
	return true;
}

bool parseClientState(std::string_view token, DisplayClientState& output) noexcept {
	if (token == "WORLD") {
		output = DisplayClientState::world;
	} else if (token == "SCREEN") {
		output = DisplayClientState::screen;
	} else if (token == "OVERLAY") {
		output = DisplayClientState::overlay;
	} else if (token == "NO_WORLD") {
		output = DisplayClientState::noWorld;
	} else {
		return false;
	}
	return true;
}

bool validClientState(DisplayClientState state) noexcept {
	switch (state) {
	case DisplayClientState::world:
	case DisplayClientState::screen:
	case DisplayClientState::overlay:
	case DisplayClientState::noWorld:
		return true;
	}
	return false;
}

bool validOffer(const DisplayOffer& offer) noexcept {
	return isValidDisplaySessionToken(offer.session)
			&& offer.sourceFovMilli >= minimumFovMilli
			&& offer.sourceFovMilli <= maximumFovMilli
			&& offer.hudXPermille <= maximumHudPermille
			&& offer.hudYPermille <= maximumHudPermille;
}

bool validCalibration(const DisplayCalibration& calibration) noexcept {
	return isValidDisplaySessionToken(calibration.session)
			&& calibration.worldViewScalePermille >= minimumWorldViewScalePermille
			&& calibration.worldViewScalePermille <= maximumWorldViewScalePermille;
}

bool validSourceMapping(const SourceUvTransform& mapping) noexcept {
	return std::isfinite(mapping.scaleX) && std::isfinite(mapping.scaleY)
			&& std::isfinite(mapping.offsetX) && std::isfinite(mapping.offsetY)
			&& mapping.scaleX > 0.0F && mapping.scaleX <= 1.0F
			&& mapping.scaleY > 0.0F && mapping.scaleY <= 1.0F
			&& mapping.offsetX >= 0.0F && mapping.offsetX <= 1.0F
			&& mapping.offsetY >= 0.0F && mapping.offsetY <= 1.0F
			&& mapping.offsetX + mapping.scaleX <= 1.0F + sourceMappingTolerance
			&& mapping.offsetY + mapping.scaleY <= 1.0F + sourceMappingTolerance;
}

std::uint16_t toPermille(double value) noexcept {
	const double clamped = std::clamp(value, 0.0, maximumHudInset);
	return static_cast<std::uint16_t>(std::lround(clamped * 1000.0));
}

} // namespace

bool isValidDisplaySessionToken(std::string_view token) noexcept {
	return token.size() == 16
			&& std::all_of(token.begin(), token.end(), isHexDigit);
}

bool serializeDisplayOffer(const DisplayOffer& offer, std::string& output) {
	if (!validOffer(offer)) {
		return false;
	}

	std::string candidate;
	candidate.reserve(96);
	candidate.append(displayControlProtocolPrefix);
	candidate.append(" OFFER ");
	candidate.append(offer.session);
	candidate.push_back(' ');
	candidate.append(std::to_string(offer.revision));
	candidate.push_back(' ');
	candidate.append(std::to_string(offer.sourceFovMilli));
	candidate.push_back(' ');
	candidate.append(std::to_string(offer.hudXPermille));
	candidate.push_back(' ');
	candidate.append(std::to_string(offer.hudYPermille));
	output = std::move(candidate);
	return true;
}

bool serializeDisplayCalibration(
		const DisplayCalibration& calibration, std::string& output) {
	if (!validCalibration(calibration)) {
		return false;
	}

	std::string candidate;
	candidate.reserve(72);
	candidate.append(displayControlProtocolPrefix);
	candidate.append(" CALIBRATION ");
	candidate.append(calibration.session);
	candidate.push_back(' ');
	candidate.append(std::to_string(calibration.revision));
	candidate.push_back(' ');
	candidate.append(std::to_string(calibration.worldViewScalePermille));
	output = std::move(candidate);
	return true;
}

bool parseDisplayStateReply(
		std::string_view message, DisplayStateReply& output) {
	std::array<std::string_view, 7> tokens;
	if (!splitExactAsciiTokens(message, tokens)
			|| tokens[0] != displayControlProtocolPrefix
			|| tokens[1] != "STATE"
			|| !isValidDisplaySessionToken(tokens[2])) {
		return false;
	}

	DisplayStateReply candidate;
	candidate.session.assign(tokens[2]);
	if (!parseUnsigned(tokens[3], candidate.sequence)
			|| !parseUnsigned(tokens[4], candidate.revision)
			|| !parseClientState(tokens[5], candidate.state)
			|| !parseUnsigned(tokens[6], candidate.appliedFovMilli)
			|| candidate.appliedFovMilli > maximumAppliedFovMilli) {
		return false;
	}

	output = std::move(candidate);
	return true;
}

bool recommendHudInsets(
		const std::array<SourceUvTransform, 2>& sourceMappings,
		HudInsetRecommendation& output) noexcept {
	if (!validSourceMapping(sourceMappings[0])
			|| !validSourceMapping(sourceMappings[1])) {
		return false;
	}

	double largestHorizontalCrop = 0.0;
	double largestVerticalCrop = 0.0;
	for (const SourceUvTransform& mapping : sourceMappings) {
		const double left = mapping.offsetX;
		const double right = 1.0 - (static_cast<double>(mapping.offsetX)
				+ static_cast<double>(mapping.scaleX));
		const double top = mapping.offsetY;
		const double bottom = 1.0 - (static_cast<double>(mapping.offsetY)
				+ static_cast<double>(mapping.scaleY));
		largestHorizontalCrop = std::max({largestHorizontalCrop, left, right});
		largestVerticalCrop = std::max({largestVerticalCrop, top, bottom});
	}

	HudInsetRecommendation candidate;
	candidate.horizontalPermille =
			toPermille(largestHorizontalCrop + horizontalOpticalMargin);
	candidate.verticalPermille =
			toPermille(largestVerticalCrop + verticalOpticalMargin);
	output = candidate;
	return true;
}

DisplayStateTracker::DisplayStateTracker(DisplayOffer expectedOffer)
		: expectedOffer_(std::move(expectedOffer)),
		  configured_(validOffer(expectedOffer_)) {}

bool DisplayStateTracker::configured() const noexcept {
	return configured_;
}

bool DisplayStateTracker::accept(
		const DisplayStateReply& reply, TimePoint receivedAt) noexcept {
	if (!configured_
			|| reply.session != expectedOffer_.session
			|| reply.revision != expectedOffer_.revision
			|| !validClientState(reply.state)
			|| reply.appliedFovMilli > maximumAppliedFovMilli
			|| (reply.state == DisplayClientState::world
					&& reply.appliedFovMilli != expectedOffer_.sourceFovMilli)
			|| (hasSequence_ && reply.sequence <= lastSequence_)
			|| (lastHeartbeatTime_.has_value()
					&& receivedAt < *lastHeartbeatTime_)) {
		return false;
	}

	const bool freshnessWasLost = lastHeartbeatTime_.has_value()
			&& receivedAt - *lastHeartbeatTime_ > displayStateFreshness;
	const bool transitioned = !currentState_.has_value()
			|| *currentState_ != reply.state || freshnessWasLost;
	currentState_ = reply.state;
	lastHeartbeatTime_ = receivedAt;
	lastSequence_ = reply.sequence;
	hasSequence_ = true;
	if (transitioned) {
		transitionBarrierTime_ = receivedAt;
	}
	return true;
}

DisplayPresentationDecision DisplayStateTracker::decide(
		TimePoint now,
		std::optional<TimePoint> latestCapturedFrameTime) const noexcept {
	if (!currentState_.has_value() || !lastHeartbeatTime_.has_value()
			|| !transitionBarrierTime_.has_value()
			|| now < *lastHeartbeatTime_
			|| now - *lastHeartbeatTime_ > displayStateFreshness) {
		return DisplayPresentationDecision::comfortQuad;
	}

	if (!latestCapturedFrameTime.has_value()
			|| *latestCapturedFrameTime <= *transitionBarrierTime_) {
		return DisplayPresentationDecision::waitForFreshCapture;
	}

	return *currentState_ == DisplayClientState::world
			? DisplayPresentationDecision::immersive
			: DisplayPresentationDecision::comfortQuad;
}

bool DisplayStateTracker::hasAcceptedState() const noexcept {
	return currentState_.has_value();
}

std::optional<DisplayStateTracker::TimePoint>
DisplayStateTracker::lastHeartbeatTime() const noexcept {
	return lastHeartbeatTime_;
}

std::optional<DisplayStateTracker::TimePoint>
DisplayStateTracker::transitionBarrierTime() const noexcept {
	return transitionBarrierTime_;
}

std::optional<DisplayClientState> DisplayStateTracker::currentState() const noexcept {
	return currentState_;
}

} // namespace mcxrinput::native
