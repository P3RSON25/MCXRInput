#include <mcxrinput/display_control.hpp>

#include <chrono>
#include <iostream>
#include <limits>
#include <string>

using namespace mcxrinput::native;
using namespace std::chrono_literals;

namespace {

int failures = 0;

void check(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

DisplayOffer offer() {
	return DisplayOffer{"0123aBcDeF456789", 42, 110000, 160, 190};
}

DisplayStateReply reply(
		std::uint64_t sequence, DisplayClientState state) {
	return DisplayStateReply{
			"0123aBcDeF456789", sequence, 42, state, 110000};
}

void offerSerializationIsStrictAndTransactional() {
	std::string output = "unchanged";
	check(serializeDisplayOffer(offer(), output), "valid offer serializes");
	check(output == "MCXRD1 OFFER 0123aBcDeF456789 42 110000 160 190",
			"offer uses the exact ASCII wire grammar");

	DisplayOffer invalid = offer();
	invalid.session = "not-hex-session!";
	output = "unchanged";
	check(!serializeDisplayOffer(invalid, output) && output == "unchanged",
			"invalid session cannot emit a partial offer");
	invalid = offer();
	invalid.sourceFovMilli = 160000;
	check(serializeDisplayOffer(invalid, output)
			&& output == "MCXRD1 OFFER 0123aBcDeF456789 42 160000 160 190",
			"exact maximum source FOV serializes");
	invalid.sourceFovMilli = 30000;
	check(serializeDisplayOffer(invalid, output)
			&& output == "MCXRD1 OFFER 0123aBcDeF456789 42 30000 160 190",
			"exact minimum source FOV serializes");
	output = "unchanged";
	invalid.sourceFovMilli = 160001;
	check(!serializeDisplayOffer(invalid, output) && output == "unchanged",
			"source FOV just above the bound is rejected transactionally");
	invalid.sourceFovMilli = 29999;
	check(!serializeDisplayOffer(invalid, output) && output == "unchanged",
			"source FOV just below the bound is rejected transactionally");
	invalid = offer();
	invalid.hudXPermille = 451;
	check(!serializeDisplayOffer(invalid, output), "out-of-range HUD inset is rejected");
}

void calibrationSerializationIsStrictAndTransactional() {
	DisplayCalibration calibration{
			"0123aBcDeF456789", 42, 400};
	std::string output = "unchanged";
	check(serializeDisplayCalibration(calibration, output),
			"valid display calibration serializes");
	check(output == "MCXRD1 CALIBRATION 0123aBcDeF456789 42 400",
			"calibration uses the exact additive ASCII wire grammar");

	calibration.worldViewScalePermille = 300;
	check(serializeDisplayCalibration(calibration, output)
			&& output == "MCXRD1 CALIBRATION 0123aBcDeF456789 42 300",
			"minimum world-view scale serializes");
	calibration.worldViewScalePermille = 1000;
	check(serializeDisplayCalibration(calibration, output)
			&& output == "MCXRD1 CALIBRATION 0123aBcDeF456789 42 1000",
			"identity world-view scale serializes");

	output = "unchanged";
	calibration.worldViewScalePermille = 299;
	check(!serializeDisplayCalibration(calibration, output)
			&& output == "unchanged",
			"scale below the supported range is rejected transactionally");
	calibration.worldViewScalePermille = 1001;
	check(!serializeDisplayCalibration(calibration, output)
			&& output == "unchanged",
			"scale above the supported range is rejected transactionally");
	calibration = DisplayCalibration{"invalid", 42, 400};
	check(!serializeDisplayCalibration(calibration, output)
			&& output == "unchanged",
			"invalid calibration identity cannot emit a partial message");
}

void stateParserAcceptsEveryStateAndUint64Boundary() {
	DisplayStateReply parsed;
	check(parseDisplayStateReply(
			"MCXRD1 STATE ABCDEFabcdef0123 18446744073709551615 0 WORLD 30000",
			parsed), "mixed-case hex and uint64 boundaries parse");
	check(parsed.session == "ABCDEFabcdef0123"
			&& parsed.sequence == std::numeric_limits<std::uint64_t>::max()
			&& parsed.revision == 0
			&& parsed.state == DisplayClientState::world
			&& parsed.appliedFovMilli == 30000,
			"parsed WORLD fields are exact");
	check(parseDisplayStateReply(
			"MCXRD1 STATE ABCDEFabcdef0123 1 2 SCREEN 360000", parsed)
			&& parsed.state == DisplayClientState::screen,
			"SCREEN and representable applied-FOV upper bound parse");
	check(parseDisplayStateReply(
			"MCXRD1 STATE ABCDEFabcdef0123 2 2 OVERLAY 110000", parsed)
			&& parsed.state == DisplayClientState::overlay,
			"OVERLAY parses");
	check(parseDisplayStateReply(
			"MCXRD1 STATE ABCDEFabcdef0123 3 2 NO_WORLD 0", parsed)
			&& parsed.state == DisplayClientState::noWorld
			&& parsed.appliedFovMilli == 0,
			"NO_WORLD accepts an unavailable camera-FOV sentinel");
}

void stateParserRejectsMalformedInputTransactionally() {
	DisplayStateReply parsed = reply(99, DisplayClientState::overlay);
	const auto unchanged = parsed;
	const std::string invalidMessages[] = {
			" MCXRD1 STATE 0123aBcDeF456789 1 42 WORLD 110000",
			"MCXRD1  STATE 0123aBcDeF456789 1 42 WORLD 110000",
			"MCXRD1\tSTATE 0123aBcDeF456789 1 42 WORLD 110000",
			"MCXRD1 STATE 0123aBcDeF456789 1 42 WORLD 110000\n",
			"MCXRD1 STATE 0123aBcDeF456789 1 42 WORLD 110000 trailing",
			"MCXRD1 STATE 0123aBcDeF45678Z 1 42 WORLD 110000",
			"MCXRD1 STATE 0123aBcDeF456789 -1 42 WORLD 110000",
			"MCXRD1 STATE 0123aBcDeF456789 1 +42 WORLD 110000",
			"MCXRD1 STATE 0123aBcDeF456789 18446744073709551616 42 WORLD 110000",
			"MCXRD1 STATE 0123aBcDeF456789 1 42 UNKNOWN 110000",
			"MCXRD1 STATE 0123aBcDeF456789 1 42 WORLD 360001",
			"MCXRD2 STATE 0123aBcDeF456789 1 42 WORLD 110000",
			"MCXRD1 OFFER 0123aBcDeF456789 1 42 WORLD 110000",
	};
	for (const std::string& message : invalidMessages) {
		check(!parseDisplayStateReply(message, parsed),
				"malformed STATE message is rejected");
		check(parsed.sequence == unchanged.sequence
				&& parsed.state == unchanged.state,
				"parse failure leaves prior state unchanged");
	}

	std::string nonAscii = "MCXRD1 STATE 0123aBcDeF456789 1 42 WORLD 110000";
	nonAscii[0] = static_cast<char>(0xFF);
	check(!parseDisplayStateReply(nonAscii, parsed), "non-ASCII input is rejected");
}

void hudRecommendationUsesWorstEyeEdgeAndMargins() {
	std::array<SourceUvTransform, 2> identityMappings{
			SourceUvTransform{1.0F, 1.0F, 0.0F, 0.0F},
			SourceUvTransform{1.0F, 1.0F, 0.0F, 0.0F},
	};
	HudInsetRecommendation recommendation;
	check(recommendHudInsets(identityMappings, recommendation)
			&& recommendation.horizontalPermille == 60
			&& recommendation.verticalPermille == 90,
			"uncropped stretch mapping keeps only the optical HUD margins");

	std::array<SourceUvTransform, 2> mappings{
			SourceUvTransform{0.80F, 0.90F, 0.10F, 0.05F},
			SourceUvTransform{0.70F, 0.80F, 0.20F, 0.15F},
	};
	check(recommendHudInsets(mappings, recommendation),
			"valid contained source mappings produce HUD insets");
	check(recommendation.horizontalPermille == 260,
			"largest horizontal crop across both eyes gains six percent");
	check(recommendation.verticalPermille == 240,
			"largest vertical crop across both eyes gains nine percent");

	mappings[1] = SourceUvTransform{0.01F, 0.01F, 0.49F, 0.49F};
	check(recommendHudInsets(mappings, recommendation)
			&& recommendation.horizontalPermille == 450
			&& recommendation.verticalPermille == 450,
			"recommendations clamp to the existing 45-percent limit");

	const HudInsetRecommendation unchanged{12, 34};
	recommendation = unchanged;
	mappings[0].scaleX = std::numeric_limits<float>::quiet_NaN();
	check(!recommendHudInsets(mappings, recommendation)
			&& recommendation.horizontalPermille == unchanged.horizontalPermille
			&& recommendation.verticalPermille == unchanged.verticalPermille,
			"invalid mapping fails without replacing the prior recommendation");
	mappings[0] = SourceUvTransform{0.0F, 1.0F, 0.0F, 0.0F};
	check(!recommendHudInsets(mappings, recommendation),
			"zero-area source mapping is rejected");
}

void trackerMatchesIdentityRevisionFovAndSequence() {
	DisplayStateTracker tracker(offer());
	const auto start = DisplayStateTracker::TimePoint{} + 1s;
	check(tracker.configured(), "valid expected offer configures tracker");

	auto candidate = reply(1, DisplayClientState::world);
	candidate.session = "FFFFFFFFFFFFFFFF";
	check(!tracker.accept(candidate, start), "wrong session is rejected");
	candidate = reply(1, DisplayClientState::world);
	candidate.revision = 43;
	check(!tracker.accept(candidate, start), "wrong revision is rejected");
	candidate = reply(1, DisplayClientState::world);
	candidate.appliedFovMilli = 109999;
	check(!tracker.accept(candidate, start), "unacknowledged FOV is rejected");
	check(!tracker.hasAcceptedState(), "rejected replies cannot create state");

	check(tracker.accept(reply(1, DisplayClientState::world), start),
			"matching initial reply is accepted");
	check(!tracker.accept(reply(1, DisplayClientState::screen), start + 1ms),
			"duplicate sequence is rejected");
	check(!tracker.accept(reply(0, DisplayClientState::screen), start + 1ms),
			"decreasing sequence is rejected");
	check(!tracker.accept(reply(2, DisplayClientState::screen), start - 1ms),
			"monotonic sequence cannot move receive time backward");
	candidate = reply(2, DisplayClientState::world);
	candidate.state = static_cast<DisplayClientState>(99);
	check(!tracker.accept(candidate, start + 1ms),
			"invalid directly constructed state enum is rejected");
	check(tracker.currentState() == DisplayClientState::world,
			"rejected replies cannot replace accepted state");
}

void trackerRequiresOfferedFovOnlyForWorld() {
	DisplayStateTracker tracker(offer());
	const auto start = DisplayStateTracker::TimePoint{} + 5s;

	auto candidate = reply(1, DisplayClientState::screen);
	candidate.appliedFovMilli = 90000;
	check(tracker.accept(candidate, start),
			"SCREEN accepts a parser-valid restored vanilla FOV");
	check(tracker.decide(start + 1ms, start + 1ms)
			== DisplayPresentationDecision::comfortQuad,
			"SCREEN can select comfort presentation without waiting for FOV equality");

	candidate = reply(2, DisplayClientState::overlay);
	candidate.appliedFovMilli = 0;
	check(tracker.accept(candidate, start + 2ms),
			"OVERLAY accepts an unavailable camera-FOV sentinel");
	candidate = reply(3, DisplayClientState::noWorld);
	candidate.appliedFovMilli = 360000;
	check(tracker.accept(candidate, start + 4ms),
			"NO_WORLD accepts the representable applied-FOV upper bound");

	candidate = reply(4, DisplayClientState::screen);
	candidate.appliedFovMilli = 360001;
	check(!tracker.accept(candidate, start + 6ms),
			"non-WORLD state rejects values above the parser range");

	candidate = reply(4, DisplayClientState::world);
	candidate.appliedFovMilli = 109999;
	check(!tracker.accept(candidate, start + 6ms),
			"WORLD continues to require exact offered-FOV acknowledgment");
	candidate.appliedFovMilli = 110000;
	check(tracker.accept(candidate, start + 6ms),
			"WORLD accepts the exact offered FOV");
}

void trackerSeparatesHeartbeatAndTransitionBarrier() {
	DisplayStateTracker tracker(offer());
	const auto start = DisplayStateTracker::TimePoint{} + 10s;
	check(tracker.accept(reply(1, DisplayClientState::world), start),
			"initial WORLD reply is accepted");
	check(tracker.lastHeartbeatTime() == start
			&& tracker.transitionBarrierTime() == start,
			"initial reply establishes heartbeat and transition barrier");
	check(tracker.decide(start, start - 1ms)
			== DisplayPresentationDecision::waitForFreshCapture,
			"old capture waits instead of flashing a pre-transition frame");
	check(tracker.decide(start, start)
			== DisplayPresentationDecision::waitForFreshCapture,
			"capture at the barrier is not new enough for WORLD presentation");
	check(tracker.decide(start + 1ms, start + 1ms)
			== DisplayPresentationDecision::immersive,
			"capture strictly newer than the barrier permits WORLD presentation");

	check(tracker.accept(reply(2, DisplayClientState::world), start + 100ms),
			"same-state heartbeat is accepted");
	check(tracker.lastHeartbeatTime() == start + 100ms
			&& tracker.transitionBarrierTime() == start,
			"heartbeat refreshes freshness without advancing capture barrier");
	check(tracker.decide(start + 100ms, start + 1ms)
			== DisplayPresentationDecision::immersive,
			"heartbeat does not continuously postpone immersive output");

	check(tracker.accept(reply(3, DisplayClientState::screen), start + 200ms),
			"WORLD to SCREEN transition is accepted");
	check(tracker.transitionBarrierTime() == start + 200ms,
			"state transition advances capture barrier");
	check(tracker.decide(start + 200ms, start + 199ms)
			== DisplayPresentationDecision::waitForFreshCapture,
			"SCREEN transition blanks while capture still contains WORLD");
	check(tracker.decide(start + 201ms, start + 201ms)
			== DisplayPresentationDecision::comfortQuad,
			"fresh SCREEN capture selects comfort quad");

	check(tracker.accept(reply(4, DisplayClientState::world), start + 300ms),
			"SCREEN to WORLD transition is accepted");
	check(tracker.decide(start + 300ms, start + 299ms)
			== DisplayPresentationDecision::waitForFreshCapture,
			"return to WORLD also waits for a post-transition capture");
	check(tracker.decide(start + 301ms, start + 301ms)
			== DisplayPresentationDecision::immersive,
			"post-transition WORLD capture restores immersive output");
}

void trackerFallsBackWhenMissingOrStale() {
	DisplayStateTracker tracker(offer());
	const auto start = DisplayStateTracker::TimePoint{} + 20s;
	check(tracker.decide(start, std::nullopt)
			== DisplayPresentationDecision::comfortQuad,
			"missing client state falls back to comfort quad");
	check(tracker.accept(reply(1, DisplayClientState::world), start),
			"freshness fixture is accepted");
	check(tracker.decide(start + 500ms, start + 1ms)
			== DisplayPresentationDecision::immersive,
			"state is fresh through exactly 500 milliseconds");
	check(tracker.decide(start + 501ms, start)
			== DisplayPresentationDecision::comfortQuad,
			"state older than 500 milliseconds fails closed to comfort quad");
	check(tracker.decide(start - 1ms, start)
			== DisplayPresentationDecision::comfortQuad,
			"invalid backward clock observation fails closed");
	check(tracker.accept(reply(2, DisplayClientState::world), start + 600ms),
			"same logical state can recover after heartbeat staleness");
	check(tracker.transitionBarrierTime() == start + 600ms,
			"stale recovery establishes a new capture barrier");
	check(tracker.decide(start + 600ms, start + 599ms)
			== DisplayPresentationDecision::waitForFreshCapture,
			"stale recovery cannot jump immersive on an old capture");
	check(tracker.decide(start + 601ms, start + 601ms)
			== DisplayPresentationDecision::immersive,
			"stale recovery resumes only after a strictly newer capture");

	DisplayOffer invalid = offer();
	invalid.session = "short";
	DisplayStateTracker invalidTracker(invalid);
	check(!invalidTracker.configured()
			&& !invalidTracker.accept(reply(1, DisplayClientState::world), start),
			"invalid expected identity cannot accept state");
}

} // namespace

int main() {
	offerSerializationIsStrictAndTransactional();
	calibrationSerializationIsStrictAndTransactional();
	stateParserAcceptsEveryStateAndUint64Boundary();
	stateParserRejectsMalformedInputTransactionally();
	hudRecommendationUsesWorstEyeEdgeAndMargins();
	trackerMatchesIdentityRevisionFovAndSequence();
	trackerRequiresOfferedFovOnlyForWorld();
	trackerSeparatesHeartbeatAndTransitionBarrier();
	trackerFallsBackWhenMissingOrStale();

	if (failures != 0) {
		std::cerr << failures << " display-control test(s) failed.\n";
		return 1;
	}
	std::cout << "All display-control tests passed.\n";
	return 0;
}
