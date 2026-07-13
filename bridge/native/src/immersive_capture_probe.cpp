#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <mcxrinput/half_sbs_renderer.hpp>
#include <mcxrinput/openxr_d3d11.hpp>
#include <mcxrinput/projection_math.hpp>
#include <mcxrinput/window_capture.hpp>

#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace mcxrinput::native;

namespace {

constexpr XrFormFactor formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
constexpr auto initialCaptureTimeout = std::chrono::seconds{10};
constexpr auto firstDisplayTimeout = std::chrono::seconds{30};
constexpr auto maximumCaptureAge = std::chrono::milliseconds{500};
constexpr auto maximumLiveStarvation = std::chrono::seconds{5};
constexpr auto shutdownGrace = std::chrono::seconds{5};
constexpr auto statusInterval = std::chrono::seconds{1};
// Reserve a fixed edge margin once instead of changing projection scale when
// runtime-predicted per-eye geometry shifts by a fraction of a degree.
constexpr float projectionCalibrationGuardDegrees = 0.25F;

std::atomic_bool stopRequested{false};

enum class ExitCode : int {
	success = 0,
	usage = 1,
	windowSelection = 2,
	openXrRuntime = 3,
	noHeadset = 4,
	openXrSession = 5,
	capture = 6,
	invalidStereo = 7,
	rendering = 8,
	cancelledBeforeValidation = 9,
};

enum class EyeOrder {
	leftRight,
	rightLeft,
};

struct Options {
	bool help{false};
	bool listWindows{false};
	std::optional<std::filesystem::path> executable;
	std::optional<HWND> window;
	int seconds{30};
	float rollCoverageDegrees{20.0F};
	float sourceVerticalFovDegrees{110.0F};
	HalfSbsFitMode fit{HalfSbsFitMode::cover};
	EyeOrder eyeOrder{EyeOrder::leftRight};
	bool secondsSeen{false};
	bool rollCoverageSeen{false};
	bool sourceVerticalFovSeen{false};
	bool fitSeen{false};
	bool eyeOrderSeen{false};
};

BOOL WINAPI handleConsoleControl(DWORD controlType) {
	switch (controlType) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		stopRequested.store(true);
		return TRUE;
	default:
		return FALSE;
	}
}

template <std::size_t Size>
void copyText(std::string_view text, char (&target)[Size]) {
	static_assert(Size > 0);
	const std::size_t count = std::min(text.size(), Size - 1);
	std::memcpy(target, text.data(), count);
	target[count] = '\0';
}

void printUsage() {
	std::wcout
			<< L"Usage:\n"
			<< L"  MCXRInputOpenXRImmersiveCaptureProbe.exe --list-windows [--executable <absolute-path>]\n"
			<< L"  MCXRInputOpenXRImmersiveCaptureProbe.exe (--executable <absolute-path> | --window <0xHWND>)\n"
			<< L"      [--seconds <1..300>] [--eye-order lr|rl]\n"
			<< L"      [--fit cover|stretch] [--source-vfov-deg <30..110>]\n"
			<< L"      [--roll-coverage-deg <0..45>]\n\n"
			<< L"Displays live ReShade half-SBS capture through a full-FOV core OpenXR\n"
			<< L"projection layer. This bounded 3DoF diagnostic sends no UDP and generates\n"
			<< L"no Minecraft input. Cover preserves aspect by cropping; stretch preserves\n"
			<< L"the complete source while accepting aspect distortion. Cover treats\n"
			<< L"--source-vfov-deg (default 110) as the captured rectilinear eye FOV and\n"
			<< L"fails if it cannot cover the headset plus roll margin.\n";
}

bool narrowAscii(std::wstring_view text, std::string& output) {
	output.clear();
	output.reserve(text.size());
	for (wchar_t character : text) {
		if (character < 0 || character > 127) {
			return false;
		}
		output.push_back(static_cast<char>(character));
	}
	return true;
}

bool parseInteger(std::wstring_view text, int minimum, int maximum, int& output) {
	std::string narrow;
	if (!narrowAscii(text, narrow)) {
		return false;
	}
	int candidate = 0;
	const auto parsed = std::from_chars(
			narrow.data(), narrow.data() + narrow.size(), candidate);
	if (parsed.ec != std::errc{} || parsed.ptr != narrow.data() + narrow.size()
			|| candidate < minimum || candidate > maximum) {
		return false;
	}
	output = candidate;
	return true;
}

bool parseFloat(std::wstring_view text, float minimum, float maximum, float& output) {
	std::string narrow;
	if (!narrowAscii(text, narrow)) {
		return false;
	}
	float candidate = 0.0F;
	const auto parsed = std::from_chars(
			narrow.data(), narrow.data() + narrow.size(), candidate);
	if (parsed.ec != std::errc{} || parsed.ptr != narrow.data() + narrow.size()
			|| !std::isfinite(candidate) || candidate < minimum || candidate > maximum) {
		return false;
	}
	output = candidate;
	return true;
}

bool parseOptions(int argc, wchar_t** argv, Options& options) {
	bool executableSeen = false;
	bool windowSeen = false;
	for (int index = 1; index < argc; ++index) {
		const std::wstring_view argument{argv[index]};
		if (argument == L"--help" || argument == L"-h") {
			if (options.help) {
				std::wcerr << L"Duplicate --help option.\n";
				return false;
			}
			options.help = true;
		} else if (argument == L"--list-windows") {
			if (options.listWindows) {
				std::wcerr << L"Duplicate --list-windows option.\n";
				return false;
			}
			options.listWindows = true;
		} else if (argument == L"--executable") {
			if (executableSeen || index + 1 >= argc) {
				std::wcerr << L"Expected one --executable followed by an absolute path.\n";
				return false;
			}
			executableSeen = true;
			options.executable = std::filesystem::path{argv[++index]};
		} else if (argument == L"--window") {
			if (windowSeen || index + 1 >= argc) {
				std::wcerr << L"Expected one --window followed by a hexadecimal handle.\n";
				return false;
			}
			HWND candidate = nullptr;
			if (!parseWindowHandle(argv[++index], candidate)) {
				std::wcerr << L"Expected --window in hexadecimal form, for example 0x123ABC.\n";
				return false;
			}
			windowSeen = true;
			options.window = candidate;
		} else if (argument == L"--seconds") {
			if (options.secondsSeen || index + 1 >= argc
					|| !parseInteger(argv[++index], 1, 300, options.seconds)) {
				std::wcerr << L"Expected one --seconds value from 1 to 300.\n";
				return false;
			}
			options.secondsSeen = true;
		} else if (argument == L"--roll-coverage-deg") {
			if (options.rollCoverageSeen || index + 1 >= argc
					|| !parseFloat(argv[++index], 0.0F, 45.0F,
							options.rollCoverageDegrees)) {
				std::wcerr << L"Expected one --roll-coverage-deg value from 0 to 45.\n";
				return false;
			}
			options.rollCoverageSeen = true;
		} else if (argument == L"--source-vfov-deg") {
			if (options.sourceVerticalFovSeen || index + 1 >= argc
					|| !parseFloat(argv[++index], 30.0F, 110.0F,
							options.sourceVerticalFovDegrees)) {
				std::wcerr << L"Expected one --source-vfov-deg value from 30 to 110.\n";
				return false;
			}
			options.sourceVerticalFovSeen = true;
		} else if (argument == L"--fit") {
			if (options.fitSeen || index + 1 >= argc) {
				std::wcerr << L"Expected one --fit value: cover or stretch.\n";
				return false;
			}
			const std::wstring_view value{argv[++index]};
			if (value == L"cover") {
				options.fit = HalfSbsFitMode::cover;
			} else if (value == L"stretch") {
				options.fit = HalfSbsFitMode::stretch;
			} else {
				std::wcerr << L"Expected --fit to be cover or stretch.\n";
				return false;
			}
			options.fitSeen = true;
		} else if (argument == L"--eye-order") {
			if (options.eyeOrderSeen || index + 1 >= argc) {
				std::wcerr << L"Expected one --eye-order value: lr or rl.\n";
				return false;
			}
			const std::wstring_view value{argv[++index]};
			if (value == L"lr") {
				options.eyeOrder = EyeOrder::leftRight;
			} else if (value == L"rl") {
				options.eyeOrder = EyeOrder::rightLeft;
			} else {
				std::wcerr << L"Expected --eye-order to be lr or rl.\n";
				return false;
			}
			options.eyeOrderSeen = true;
		} else {
			std::wcerr << L"Unknown argument: " << argument << L'\n';
			return false;
		}
	}

	if (options.help) {
		if (argc != 2) {
			std::wcerr << L"--help cannot be combined with other options.\n";
			return false;
		}
		return true;
	}
	if (options.listWindows) {
		if (options.window || options.secondsSeen || options.rollCoverageSeen
				|| options.sourceVerticalFovSeen || options.fitSeen || options.eyeOrderSeen) {
			std::wcerr << L"--list-windows accepts only the optional --executable filter.\n";
			return false;
		}
	} else if (options.executable.has_value() == options.window.has_value()) {
		std::wcerr << L"Immersive capture requires exactly one of --executable or --window.\n";
		return false;
	}
	if (options.executable) {
		if (options.executable->empty() || !options.executable->is_absolute()) {
			std::wcerr << L"--executable must be an absolute path.\n";
			return false;
		}
		*options.executable = options.executable->lexically_normal();
	}
	return true;
}

std::string_view sessionStateName(XrSessionState state) {
	switch (state) {
	case XR_SESSION_STATE_UNKNOWN: return "UNKNOWN";
	case XR_SESSION_STATE_IDLE: return "IDLE";
	case XR_SESSION_STATE_READY: return "READY";
	case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
	case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
	case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
	case XR_SESSION_STATE_STOPPING: return "STOPPING";
	case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
	case XR_SESSION_STATE_EXITING: return "EXITING";
	default: return "unrecognized";
	}
}

bool pollEvents(
		XrInstance instance, XrSession session, bool& sessionRunning,
		XrSessionState& sessionState, bool& shouldExit, bool& runtimeLoss) {
	XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
	XrResult result = xrPollEvent(instance, &event);
	while (result == XR_SUCCESS) {
		if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
			const auto& changed = reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
			sessionState = changed.state;
			std::cout << "Session state: " << sessionStateName(sessionState) << '\n';
			if (sessionState == XR_SESSION_STATE_READY) {
				XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
				beginInfo.primaryViewConfigurationType = primaryStereoViewConfiguration;
				const XrResult beginResult = xrBeginSession(session, &beginInfo);
				if (XR_FAILED(beginResult)) {
					printFailure("beginning immersive-capture session", beginResult);
					return false;
				}
				sessionRunning = true;
			} else if (sessionState == XR_SESSION_STATE_STOPPING) {
				if (sessionRunning) {
					const XrResult endResult = xrEndSession(session);
					sessionRunning = false;
					if (XR_FAILED(endResult)) {
						printFailure("ending immersive-capture session", endResult);
						return false;
					}
				}
				shouldExit = true;
			} else if (sessionState == XR_SESSION_STATE_EXITING) {
				shouldExit = true;
			} else if (sessionState == XR_SESSION_STATE_LOSS_PENDING) {
				runtimeLoss = true;
				shouldExit = true;
			}
		} else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
			std::cerr << "OpenXR runtime reported instance loss pending.\n";
			runtimeLoss = true;
			shouldExit = true;
		} else if (event.type == XR_TYPE_EVENT_DATA_EVENTS_LOST) {
			const auto& lost = reinterpret_cast<const XrEventDataEventsLost&>(event);
			std::cerr << "OpenXR runtime dropped " << lost.lostEventCount << " event(s).\n";
		}
		event = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
		result = xrPollEvent(instance, &event);
	}
	if (result != XR_EVENT_UNAVAILABLE) {
		printFailure("polling immersive-capture events", result);
		return false;
	}
	return true;
}

bool createReferenceSpace(XrSession session, XrReferenceSpaceType type, XrSpace& space) {
	XrReferenceSpaceCreateInfo info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	info.referenceSpaceType = type;
	info.poseInReferenceSpace.orientation.w = 1.0F;
	const XrResult result = xrCreateReferenceSpace(session, &info, &space);
	if (XR_FAILED(result)) {
		printFailure(type == XR_REFERENCE_SPACE_TYPE_LOCAL
				? "creating LOCAL reference space" : "creating VIEW reference space", result);
		return false;
	}
	return true;
}

bool chooseDisplayFormat(XrSession session, std::int64_t& format) {
	std::uint32_t count = 0;
	XrResult result = xrEnumerateSwapchainFormats(session, 0, &count, nullptr);
	if (XR_FAILED(result) || count == 0) {
		if (XR_FAILED(result)) {
			printFailure("enumerating immersive swapchain formats", result);
		} else {
			std::cerr << "OpenXR runtime reported no swapchain formats.\n";
		}
		return false;
	}
	std::vector<std::int64_t> formats(count);
	result = xrEnumerateSwapchainFormats(session, count, &count, formats.data());
	if (XR_FAILED(result)) {
		printFailure("enumerating immersive swapchain formats", result);
		return false;
	}
	formats.resize(count);
	const std::array<DXGI_FORMAT, 4> preferred{
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
			DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_FORMAT_B8G8R8A8_UNORM,
	};
	for (DXGI_FORMAT candidate : preferred) {
		const std::int64_t encoded = static_cast<std::int64_t>(candidate);
		if (std::find(formats.begin(), formats.end(), encoded) != formats.end()) {
			format = encoded;
			const bool srgb = candidate == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
					|| candidate == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
			std::cout << "Immersive swapchain format: " << format
					  << (srgb ? " (sRGB)\n" : " (linear fallback)\n");
			return true;
		}
	}
	std::cerr << "No supported RGBA8/BGRA8 immersive swapchain format is available.\n";
	return false;
}

bool waitForGpuIdle(ID3D11Device* device, ID3D11DeviceContext* context) {
	if (device == nullptr || context == nullptr) {
		return true;
	}
	D3D11_QUERY_DESC description{};
	description.Query = D3D11_QUERY_EVENT;
	ComPtr<ID3D11Query> query;
	const HRESULT createResult = device->CreateQuery(&description, &query);
	if (FAILED(createResult)) {
		printHresult("creating immersive-capture completion query", createResult);
		return false;
	}
	context->End(query.Get());
	context->Flush();
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
	while (std::chrono::steady_clock::now() < deadline) {
		BOOL complete = FALSE;
		const HRESULT result = context->GetData(query.Get(), &complete, sizeof(complete), 0);
		if (result == S_OK && complete) {
			return true;
		}
		if (FAILED(result)) {
			printHresult("waiting for immersive-capture D3D11 work", result);
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{1});
	}
	std::cerr << "Timed out waiting for immersive-capture D3D11 work.\n";
	return false;
}

std::optional<ExitCode> waitForInitialCapture(
		WindowCaptureSource& capture, ID3D11DeviceContext* context) {
	const auto deadline = std::chrono::steady_clock::now() + initialCaptureTimeout;
	bool invalidFrameObserved = false;
	while (!stopRequested.load() && std::chrono::steady_clock::now() < deadline) {
		const WindowCaptureUpdate update = capture.poll(context);
		if (update == WindowCaptureUpdate::frameReady
				&& capture.hasFreshFrame(maximumCaptureAge)) {
			return std::nullopt;
		}
		if (update == WindowCaptureUpdate::windowClosed) {
			std::wcerr << L"The selected Minecraft window closed before capture began.\n";
			return ExitCode::windowSelection;
		}
		if (update == WindowCaptureUpdate::invalidStereoFrame) {
			if (!invalidFrameObserved) {
				std::wcerr << capture.lastError() << L" Waiting for a valid frame.\n";
				invalidFrameObserved = true;
			}
			continue;
		}
		if (update == WindowCaptureUpdate::failure) {
			std::wcerr << capture.lastError() << L'\n';
			return ExitCode::capture;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	if (stopRequested.load()) {
		return ExitCode::cancelledBeforeValidation;
	}
	std::wcerr << L"No fresh half-SBS capture frame arrived within ten seconds.\n";
	return invalidFrameObserved ? ExitCode::invalidStereo : ExitCode::capture;
}

Pose toPose(const XrPosef& pose) {
	return Pose{
			Quaternion{pose.orientation.x, pose.orientation.y,
					pose.orientation.z, pose.orientation.w},
			Vec3{pose.position.x, pose.position.y, pose.position.z},
	};
}

XrPosef toXrPose(const Pose& pose) {
	XrPosef result{};
	result.orientation = XrQuaternionf{
			pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w};
	result.position = XrVector3f{pose.position.x, pose.position.y, pose.position.z};
	return result;
}

ProjectionFov toProjectionFov(const XrFovf& fov) {
	return ProjectionFov{fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown};
}

XrFovf toXrFov(ProjectionFov fov) {
	return XrFovf{fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown};
}

bool locateRelativeViews(
		XrSession session, XrSpace viewSpace, XrTime displayTime,
		std::array<XrView, 2>& views, XrViewState& state) {
	views = {XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW}};
	state = XrViewState{XR_TYPE_VIEW_STATE};
	XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};
	info.viewConfigurationType = primaryStereoViewConfiguration;
	info.displayTime = displayTime;
	info.space = viewSpace;
	std::uint32_t outputCount = 0;
	const XrResult result = xrLocateViews(
			session, &info, &state, static_cast<std::uint32_t>(views.size()),
			&outputCount, views.data());
	if (XR_FAILED(result)) {
		printFailure("locating immersive stereo views", result);
		return false;
	}
	if (outputCount != views.size()) {
		std::cerr << "OpenXR returned " << outputCount
				  << " immersive view(s), expected two.\n";
		return false;
	}
	const XrViewStateFlags required = XR_VIEW_STATE_ORIENTATION_VALID_BIT
			| XR_VIEW_STATE_POSITION_VALID_BIT
			| XR_VIEW_STATE_ORIENTATION_TRACKED_BIT
			| XR_VIEW_STATE_POSITION_TRACKED_BIT;
	return (state.viewStateFlags & required) == required;
}

enum class ProjectionViewBuildResult {
	success,
	invalidPoseOrFov,
	eyePlaneCrossing,
	insufficientSourceFov,
	frozenFovExceeded,
	sourceAspectChanged,
};

struct FrozenProjectionCalibration {
	bool initialized{false};
	float sourceAspect{0.0F};
	std::array<ProjectionFov, 2> fovs{};
	std::array<SourceUvTransform, 2> sourceMappings{};
};

struct ProjectionFitDiagnostics {
	bool valid{false};
	std::array<float, 2> requiredSourceVerticalFovDegrees{};
	std::array<MaximumRollCoverage, 2> maximumRollCoverages{};
};

ProjectionViewBuildResult makeProjectionViews(
		XrSession session, XrSpace viewSpace, XrSpace localSpace,
		XrTime displayTime, float rollCoverageDegrees,
		HalfSbsFitMode fit, float sourceAspect, float sourceVerticalFovDegrees,
		RollFreeBasisState& basisState,
		FrozenProjectionCalibration& calibration,
		ProjectionFitDiagnostics& fitDiagnostics,
		const std::array<SwapchainBundle, 2>& swapchains,
		std::array<XrCompositionLayerProjectionView, 2>& projectionViews,
		std::array<SourceUvTransform, 2>& sourceMappings) {
	XrSpaceLocation centerLocation{XR_TYPE_SPACE_LOCATION};
	XrResult result = xrLocateSpace(viewSpace, localSpace, displayTime, &centerLocation);
	const XrSpaceLocationFlags requiredLocation = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT
			| XR_SPACE_LOCATION_POSITION_VALID_BIT
			| XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT
			| XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	if (XR_FAILED(result) || (centerLocation.locationFlags & requiredLocation) != requiredLocation) {
		if (XR_FAILED(result)) {
			printFailure("locating VIEW center for immersive projection", result);
		}
		return ProjectionViewBuildResult::invalidPoseOrFov;
	}

	std::array<XrView, 2> relativeViews{XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW}};
	XrViewState viewState{XR_TYPE_VIEW_STATE};
	if (!locateRelativeViews(session, viewSpace, displayTime, relativeViews, viewState)) {
		return ProjectionViewBuildResult::invalidPoseOrFov;
	}
	const std::array<Pose, 2> relativePoses{
			toPose(relativeViews[0].pose), toPose(relativeViews[1].pose)};

	std::array<ProjectionFov, 2> runtimeFovs{};
	std::array<ProjectionFov, 2> requiredFovs{};
	for (std::size_t index = 0; index < relativeViews.size(); ++index) {
		runtimeFovs[index] = toProjectionFov(relativeViews[index].fov);
		const CantedFovExpansionResult expansionResult =
				computeCantedFovForRollCoverage(
					runtimeFovs[index], relativePoses[index].orientation,
					rollCoverageDegrees, requiredFovs[index]);
		if (expansionResult == CantedFovExpansionResult::eyePlaneCrossing) {
			return ProjectionViewBuildResult::eyePlaneCrossing;
		}
		if (expansionResult != CantedFovExpansionResult::success) {
			return ProjectionViewBuildResult::invalidPoseOrFov;
		}
	}

	FrozenProjectionCalibration candidateCalibration = calibration;
	if (!candidateCalibration.initialized) {
		candidateCalibration.sourceAspect = sourceAspect;
		for (std::size_t index = 0; index < requiredFovs.size(); ++index) {
			if (!expandProjectionFovByAngularGuard(
					requiredFovs[index], projectionCalibrationGuardDegrees,
					candidateCalibration.fovs[index])) {
				return ProjectionViewBuildResult::invalidPoseOrFov;
			}
		}
		if (fit == HalfSbsFitMode::cover) {
			ProjectionFitDiagnostics candidateFitDiagnostics;
			for (std::size_t index = 0; index < requiredFovs.size(); ++index) {
				if (!computeMinimumSourceVerticalFovDegrees(
						sourceAspect, candidateCalibration.fovs[index],
						candidateFitDiagnostics.requiredSourceVerticalFovDegrees[index])
						|| !computeMaximumSupportedRollCoverage(
							runtimeFovs[index], relativePoses[index].orientation,
							sourceAspect, sourceVerticalFovDegrees,
							projectionCalibrationGuardDegrees,
							candidateFitDiagnostics.maximumRollCoverages[index])) {
					return ProjectionViewBuildResult::invalidPoseOrFov;
				}
			}
			candidateFitDiagnostics.valid = true;
			// Retain capacity data even when the configured mapping below fails, so
			// the caller can report a precise, non-distorting configuration.
			fitDiagnostics = candidateFitDiagnostics;
			for (std::size_t index = 0; index < requiredFovs.size(); ++index) {
				const SourceProjectionMappingResult mappingResult =
						computeProjectionSourceUvTransform(
							sourceAspect, sourceVerticalFovDegrees,
							candidateCalibration.fovs[index],
							candidateCalibration.sourceMappings[index]);
				if (mappingResult == SourceProjectionMappingResult::insufficientSourceFov) {
					return ProjectionViewBuildResult::insufficientSourceFov;
				}
				if (mappingResult != SourceProjectionMappingResult::success) {
					return ProjectionViewBuildResult::invalidPoseOrFov;
				}
			}
		}
		candidateCalibration.initialized = true;
	} else {
		if (fit == HalfSbsFitMode::cover) {
			const float scale = std::max(
					std::abs(candidateCalibration.sourceAspect), std::abs(sourceAspect));
			if (!std::isfinite(sourceAspect) || sourceAspect <= 0.0F
					|| std::abs(candidateCalibration.sourceAspect - sourceAspect)
							> std::max(1.0F, scale) * 1.0e-4F) {
				return ProjectionViewBuildResult::sourceAspectChanged;
			}
		}
		for (std::size_t index = 0; index < requiredFovs.size(); ++index) {
			if (!projectionFovContains(candidateCalibration.fovs[index], requiredFovs[index])) {
				return ProjectionViewBuildResult::frozenFovExceeded;
			}
		}
	}

	RollFreeBasisState candidateBasisState = basisState;
	std::array<Pose, 2> rollFreePoses;
	if (!composeRollFreeEyePoses(
			toPose(centerLocation.pose), relativePoses,
			candidateBasisState, rollFreePoses)) {
		return ProjectionViewBuildResult::invalidPoseOrFov;
	}

	std::array<XrCompositionLayerProjectionView, 2> candidateViews{
			XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
			XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
	};
	for (std::size_t index = 0; index < relativeViews.size(); ++index) {
		candidateViews[index].pose = toXrPose(rollFreePoses[index]);
		candidateViews[index].fov = toXrFov(candidateCalibration.fovs[index]);
		candidateViews[index].subImage.swapchain = swapchains[index].swapchain;
		candidateViews[index].subImage.imageRect.extent = XrExtent2Di{
				static_cast<std::int32_t>(swapchains[index].width),
				static_cast<std::int32_t>(swapchains[index].height)};
	}
	basisState = candidateBasisState;
	calibration = candidateCalibration;
	projectionViews = candidateViews;
	sourceMappings = candidateCalibration.sourceMappings;
	return ProjectionViewBuildResult::success;
}

ExitCode runImmersiveCapture(const Options& options, const WindowCandidate& selected) {
	XrInstance instance = XR_NULL_HANDLE;
	XrSession session = XR_NULL_HANDLE;
	XrSpace localSpace = XR_NULL_HANDLE;
	XrSpace viewSpace = XR_NULL_HANDLE;
	D3DState d3d;
	WindowCaptureSource capture;
	HalfSbsRenderer renderer;
	std::array<SwapchainBundle, 2> eyeSwapchains;

	auto cleanup = [&]() -> bool {
		bool gpuDrained = true;
		capture.stop();
		if (d3d.context != nullptr) {
			gpuDrained = waitForGpuIdle(d3d.device.Get(), d3d.context.Get());
			d3d.context->ClearState();
			d3d.context->Flush();
		}
		renderer.reset();
		for (SwapchainBundle& swapchain : eyeSwapchains) {
			swapchain.reset();
		}
		if (viewSpace != XR_NULL_HANDLE) {
			xrDestroySpace(viewSpace);
			viewSpace = XR_NULL_HANDLE;
		}
		if (localSpace != XR_NULL_HANDLE) {
			xrDestroySpace(localSpace);
			localSpace = XR_NULL_HANDLE;
		}
		if (session != XR_NULL_HANDLE) {
			xrDestroySession(session);
			session = XR_NULL_HANDLE;
		}
		if (instance != XR_NULL_HANDLE) {
			xrDestroyInstance(instance);
			instance = XR_NULL_HANDLE;
		}
		return gpuDrained;
	};

	std::vector<XrExtensionProperties> extensions;
	if (!enumerateInstanceExtensions(extensions)
			|| !hasExtension(extensions, XR_KHR_D3D11_ENABLE_EXTENSION_NAME)) {
		std::cerr << "The active OpenXR runtime does not provide D3D11 support.\n";
		cleanup();
		return ExitCode::openXrRuntime;
	}

	XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	copyText("MCXRInput", instanceInfo.applicationInfo.applicationName);
	instanceInfo.applicationInfo.applicationVersion = 1;
	copyText("MCXRInput immersive capture probe", instanceInfo.applicationInfo.engineName);
	instanceInfo.applicationInfo.engineVersion = 1;
	instanceInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	const char* enabledExtensions[]{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
	instanceInfo.enabledExtensionCount = 1;
	instanceInfo.enabledExtensionNames = enabledExtensions;
	XrResult result = xrCreateInstance(&instanceInfo, &instance);
	if (XR_FAILED(result)) {
		printFailure("creating immersive-capture OpenXR instance", result);
		cleanup();
		return ExitCode::openXrRuntime;
	}

	XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
	systemInfo.formFactor = formFactor;
	XrSystemId systemId = XR_NULL_SYSTEM_ID;
	result = xrGetSystem(instance, &systemInfo, &systemId);
	if (XR_FAILED(result)) {
		printFailure("requesting an HMD for immersive capture", result);
		cleanup();
		return ExitCode::noHeadset;
	}
	XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
	result = xrGetSystemProperties(instance, systemId, &systemProperties);
	if (XR_FAILED(result) || systemProperties.graphicsProperties.maxLayerCount < 1) {
		if (XR_FAILED(result)) {
			printFailure("querying immersive projection layer limits", result);
		} else {
			std::cerr << "Immersive capture requires one OpenXR composition layer.\n";
		}
		cleanup();
		return ExitCode::openXrSession;
	}
	std::vector<XrViewConfigurationView> viewConfigs;
	if (!enumerateViewConfigurationViews(instance, systemId, viewConfigs)
			|| viewConfigs.size() != 2) {
		std::cerr << "Immersive capture requires exactly two PRIMARY_STEREO views.\n";
		cleanup();
		return ExitCode::openXrSession;
	}

	PFN_xrGetD3D11GraphicsRequirementsKHR getRequirements = nullptr;
	if (!loadD3D11RequirementsFunction(instance, getRequirements)) {
		cleanup();
		return ExitCode::openXrSession;
	}
	XrGraphicsRequirementsD3D11KHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
	result = getRequirements(instance, systemId, &graphicsRequirements);
	if (XR_FAILED(result) || !createD3D11Device(graphicsRequirements, d3d)) {
		if (XR_FAILED(result)) {
			printFailure("querying immersive-capture D3D11 requirements", result);
		}
		cleanup();
		return ExitCode::openXrSession;
	}

	if (!capture.start(selected.window, d3d.device.Get())) {
		std::wcerr << capture.lastError() << L'\n';
		cleanup();
		return ExitCode::capture;
	}
	if (const auto initialFailure = waitForInitialCapture(capture, d3d.context.Get())) {
		cleanup();
		return *initialFailure;
	}
	const WindowCaptureFrame& initialFrame = capture.latestFrame();
	if (!initialFrame.texture || initialFrame.combinedWidth < 2 || initialFrame.height == 0) {
		cleanup();
		return ExitCode::invalidStereo;
	}
	const std::uint32_t initialCaptureWidth = initialFrame.combinedWidth;
	const std::uint32_t initialCaptureHeight = initialFrame.height;

	XrGraphicsBindingD3D11KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
	graphicsBinding.device = d3d.device.Get();
	XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionInfo.next = &graphicsBinding;
	sessionInfo.systemId = systemId;
	result = xrCreateSession(instance, &sessionInfo, &session);
	if (XR_FAILED(result)) {
		printFailure("creating immersive-capture OpenXR session", result);
		cleanup();
		return ExitCode::openXrSession;
	}
	if (!createReferenceSpace(session, XR_REFERENCE_SPACE_TYPE_LOCAL, localSpace)
			|| !createReferenceSpace(session, XR_REFERENCE_SPACE_TYPE_VIEW, viewSpace)) {
		cleanup();
		return ExitCode::openXrSession;
	}
	XrEnvironmentBlendMode blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	std::int64_t swapchainFormat = 0;
	if (!chooseEnvironmentBlendMode(instance, systemId, blendMode)
			|| !chooseDisplayFormat(session, swapchainFormat)) {
		cleanup();
		return ExitCode::rendering;
	}

	for (std::size_t index = 0; index < eyeSwapchains.size(); ++index) {
		const std::uint32_t width = std::min({
				viewConfigs[index].recommendedImageRectWidth,
				viewConfigs[index].maxImageRectWidth,
				systemProperties.graphicsProperties.maxSwapchainImageWidth});
		const std::uint32_t height = std::min({
				viewConfigs[index].recommendedImageRectHeight,
				viewConfigs[index].maxImageRectHeight,
				systemProperties.graphicsProperties.maxSwapchainImageHeight});
		if (width == 0 || height == 0
				|| !createColorSwapchain(
					session, d3d.device.Get(),
					ColorSwapchainDescription{
							width, height, swapchainFormat, 1, 1,
							XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT},
					eyeSwapchains[index])) {
			cleanup();
			return ExitCode::rendering;
		}
	}
	if (!renderer.initialize(d3d.device.Get())) {
		cleanup();
		return ExitCode::rendering;
	}

	std::cout << std::fixed << std::setprecision(3)
			  << "Initial half-SBS capture: " << initialCaptureWidth << 'x'
			  << initialCaptureHeight << '\n'
			  << "Projection targets: left " << eyeSwapchains[0].width << 'x'
			  << eyeSwapchains[0].height << ", right " << eyeSwapchains[1].width
			  << 'x' << eyeSwapchains[1].height << '\n'
			  << "Fit: " << (options.fit == HalfSbsFitMode::cover
					  ? "cover (tangent-correct FOV crop)" : "stretch (complete source)")
			  << "; source vertical FOV: " << options.sourceVerticalFovDegrees << " degrees\n"
			  << "Fixed roll coverage: +/-" << options.rollCoverageDegrees << " degrees\n"
			  << "Frozen projection guard: " << projectionCalibrationGuardDegrees
			  << " degrees per edge\n"
			  << "Eye routing: " << (options.eyeOrder == EyeOrder::leftRight
					  ? "source L->left, R->right" : "source R->left, L->right")
			  << "\nThis is a head-following 3DoF projection; translation provides no scene parallax.\n"
			  << "This probe owns OpenXR focus; do not run MCXRInputOpenXRBridge.exe beside it.\n"
			  << "Press Ctrl+C to stop early.\n";

	bool sessionRunning = false;
	bool shouldExit = false;
	bool runtimeLoss = false;
	bool exitRequested = false;
	bool stopInitiated = false;
	bool userCancelled = false;
	bool cancelledBeforeValidation = false;
	bool shutdownFailed = false;
	bool invalidCaptureReported = false;
	bool calibrationPrinted = false;
	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
	std::optional<ExitCode> terminalFailure;
	RollFreeBasisState basisState;
	FrozenProjectionCalibration projectionCalibration;
	ProjectionFitDiagnostics projectionFitDiagnostics;
	std::uint64_t submittedFrames = 0;
	std::uint64_t staleFrames = 0;
	std::uint64_t invalidPoseFrames = 0;
	const auto loopStarted = std::chrono::steady_clock::now();
	auto lastStatus = loopStarted;
	std::chrono::steady_clock::time_point firstSubmittedAt{};
	std::chrono::steady_clock::time_point lastSubmittedAt{};
	std::chrono::steady_clock::time_point captureUnavailableSince{};
	std::chrono::steady_clock::time_point stopStartedAt{};
	std::chrono::steady_clock::time_point nextExitAttempt{};

	while (!shouldExit) {
		if (!pollEvents(instance, session, sessionRunning, sessionState, shouldExit, runtimeLoss)) {
			terminalFailure = ExitCode::openXrSession;
			break;
		}
		if (shouldExit) {
			break;
		}

		if (!terminalFailure) {
			const WindowCaptureUpdate update = capture.poll(d3d.context.Get());
			switch (update) {
			case WindowCaptureUpdate::resized:
				std::cout << "Capture resized; waiting for a fresh exact-sized frame.\n";
				break;
			case WindowCaptureUpdate::minimized:
				std::cout << "Capture blanked while the Minecraft window is minimized.\n";
				break;
			case WindowCaptureUpdate::restored:
				std::cout << "Minecraft window restored; waiting for a fresh frame.\n";
				break;
			case WindowCaptureUpdate::windowClosed:
				std::cerr << "The selected Minecraft window closed.\n";
				terminalFailure = ExitCode::windowSelection;
				break;
			case WindowCaptureUpdate::invalidStereoFrame:
				if (!invalidCaptureReported) {
					std::wcerr << capture.lastError() << L" Blanking until a valid frame arrives.\n";
					invalidCaptureReported = true;
				}
				break;
			case WindowCaptureUpdate::failure:
				std::wcerr << capture.lastError() << L'\n';
				terminalFailure = invalidCaptureReported
						? ExitCode::invalidStereo : ExitCode::capture;
				break;
			default:
				break;
			}
			if (update == WindowCaptureUpdate::frameReady) {
				invalidCaptureReported = false;
			}
		}

		const auto now = std::chrono::steady_clock::now();
		const bool captureFreshNow = capture.hasFreshFrame(maximumCaptureAge);
		if (!stopInitiated && firstSubmittedAt != std::chrono::steady_clock::time_point{}) {
			if (captureFreshNow) {
				captureUnavailableSince = {};
			} else if (captureUnavailableSince == std::chrono::steady_clock::time_point{}) {
				captureUnavailableSince = now;
			} else if (now - captureUnavailableSince >= maximumLiveStarvation
					&& !terminalFailure) {
				std::cerr << "Live Minecraft capture remained unavailable for five seconds.\n";
				terminalFailure = invalidCaptureReported
						? ExitCode::invalidStereo : ExitCode::capture;
			}
		}
		const bool durationReached = firstSubmittedAt != std::chrono::steady_clock::time_point{}
				&& now - firstSubmittedAt >= std::chrono::seconds{options.seconds};
		const bool recentlySubmitted = lastSubmittedAt != std::chrono::steady_clock::time_point{}
				&& now - lastSubmittedAt <= maximumCaptureAge;
		const bool durationComplete = durationReached && captureFreshNow && recentlySubmitted;
		if (!stopInitiated && durationReached && !durationComplete
				&& now - firstSubmittedAt >= std::chrono::seconds{options.seconds}
						+ maximumLiveStarvation && !terminalFailure) {
			std::cerr << "Immersive output did not recover near the end of its bounded run.\n";
			terminalFailure = captureFreshNow ? ExitCode::rendering : ExitCode::capture;
		}
		if ((stopRequested.load() || durationComplete || terminalFailure.has_value())
				&& !stopInitiated) {
			stopInitiated = true;
			userCancelled = stopRequested.load();
			cancelledBeforeValidation = userCancelled
					&& firstSubmittedAt == std::chrono::steady_clock::time_point{};
			stopStartedAt = now;
			nextExitAttempt = now;
		}
		if (stopInitiated && !sessionRunning) {
			shouldExit = true;
		} else if (stopInitiated && now - stopStartedAt >= shutdownGrace) {
			std::cerr << "OpenXR immersive-capture session did not stop within five seconds.\n";
			shutdownFailed = true;
			shouldExit = true;
		} else if (stopInitiated && !exitRequested) {
			if (sessionState == XR_SESSION_STATE_FOCUSED && now >= nextExitAttempt) {
				const XrResult exitResult = xrRequestExitSession(session);
				if (exitResult == XR_SUCCESS) {
					exitRequested = true;
				} else if (exitResult == XR_SESSION_NOT_FOCUSED) {
					std::cerr << "OpenXR focus changed during shutdown; destroying the session "
							  << "without a normal STOPPING transition.\n";
					shutdownFailed = true;
					shouldExit = true;
				} else {
					printFailure("requesting immersive-capture session exit", exitResult);
					shutdownFailed = true;
					shouldExit = true;
				}
			} else if (sessionState != XR_SESSION_STATE_FOCUSED) {
				std::cerr << "OpenXR session is not focused; destroying it without a normal "
						  << "STOPPING transition.\n";
				shutdownFailed = true;
				shouldExit = true;
			}
		}
		if (shouldExit) {
			continue;
		}
		if (firstSubmittedAt == std::chrono::steady_clock::time_point{}
				&& now - loopStarted >= firstDisplayTimeout) {
			std::cerr << "No fresh captured stereo frame was projected within 30 seconds.\n";
			terminalFailure = ExitCode::rendering;
			stopInitiated = true;
			stopStartedAt = now;
			nextExitAttempt = now;
			continue;
		}
		if (!sessionRunning) {
			std::this_thread::sleep_for(std::chrono::milliseconds{10});
			continue;
		}

		FrameScope frame;
		if (!frame.begin(session, blendMode)) {
			terminalFailure = ExitCode::openXrSession;
			break;
		}
		bool layerReady = false;
		bool renderFailed = false;
		std::array<XrCompositionLayerProjectionView, 2> projectionViews{
				XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
				XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
		std::array<SourceUvTransform, 2> sourceMappings{};
		const bool captureFresh = capture.hasFreshFrame(maximumCaptureAge);
		if (!stopInitiated && frame.state().shouldRender == XR_TRUE && captureFresh) {
			const WindowCaptureFrame& captured = capture.latestFrame();
			const float sourceAspect = static_cast<float>(captured.combinedWidth)
					/ static_cast<float>(captured.height);
			const ProjectionViewBuildResult viewResult = makeProjectionViews(
					session, viewSpace, localSpace, frame.state().predictedDisplayTime,
					options.rollCoverageDegrees, options.fit, sourceAspect,
					options.sourceVerticalFovDegrees, basisState, projectionCalibration,
					projectionFitDiagnostics,
					eyeSwapchains,
					projectionViews, sourceMappings);
			if (viewResult == ProjectionViewBuildResult::success) {
				if (!calibrationPrinted) {
					constexpr float radiansToDegrees = 57.29577951308232F;
					for (std::size_t index = 0; index < projectionCalibration.fovs.size(); ++index) {
						const ProjectionFov fov = projectionCalibration.fovs[index];
						const SourceUvTransform uv = projectionCalibration.sourceMappings[index];
						std::cout << (index == 0 ? "Frozen left" : "Frozen right")
								  << " projection FOV degrees [L R U D]=["
								  << fov.angleLeft * radiansToDegrees << ' '
								  << fov.angleRight * radiansToDegrees << ' '
								  << fov.angleUp * radiansToDegrees << ' '
								  << fov.angleDown * radiansToDegrees << "] UV [scaleX scaleY offsetX offsetY]=["
								  << uv.scaleX << ' ' << uv.scaleY << ' '
								  << uv.offsetX << ' ' << uv.offsetY << "]\n";
					}
					calibrationPrinted = true;
				}
				SwapchainImageLease physicalLeft;
				SwapchainImageLease physicalRight;
				if (physicalLeft.acquire(eyeSwapchains[0])
						&& physicalRight.acquire(eyeSwapchains[1])) {
					HalfSbsRenderOptions renderOptions;
					SwapchainImageLease* sourceLeftTarget = &physicalLeft;
					SwapchainImageLease* sourceRightTarget = &physicalRight;
					if (options.eyeOrder == EyeOrder::leftRight) {
						renderOptions.left = {
								options.fit, sourceMappings[0], options.fit == HalfSbsFitMode::cover};
						renderOptions.right = {
								options.fit, sourceMappings[1], options.fit == HalfSbsFitMode::cover};
					} else {
						sourceLeftTarget = &physicalRight;
						sourceRightTarget = &physicalLeft;
						renderOptions.left = {
								options.fit, sourceMappings[1], options.fit == HalfSbsFitMode::cover};
						renderOptions.right = {
								options.fit, sourceMappings[0], options.fit == HalfSbsFitMode::cover};
					}
					const bool rendered = renderer.render(
							d3d.context.Get(), captured.texture.Get(), captured.combinedWidth,
							captured.height, *sourceLeftTarget, *sourceRightTarget,
							renderOptions);
					const bool rightReleased = physicalRight.release();
					const bool leftReleased = physicalLeft.release();
					layerReady = rendered && rightReleased && leftReleased;
					renderFailed = !layerReady;
				} else {
					renderFailed = true;
				}
			} else if (viewResult == ProjectionViewBuildResult::eyePlaneCrossing) {
				std::cerr << "The requested roll coverage and runtime eye cant make a "
						  << "projection ray reach the eye plane. Reduce "
							 "--roll-coverage-deg; changing image fit cannot correct this.\n";
				renderFailed = true;
			} else if (viewResult == ProjectionViewBuildResult::insufficientSourceFov) {
				std::cerr << "The configured " << options.sourceVerticalFovDegrees
						  << "-degree source vertical FOV cannot cover SteamVR's expanded "
						  << "projection frustum.\n";
				if (projectionFitDiagnostics.valid) {
					// Round requirements upward and supported coverage downward so every
					// printed threshold remains conservative when copied back into the CLI.
					constexpr float diagnosticStep = 0.001F;
					const float requiredLeft = std::ceil(projectionFitDiagnostics
							.requiredSourceVerticalFovDegrees[0] / diagnosticStep)
							* diagnosticStep;
					const float requiredRight = std::ceil(projectionFitDiagnostics
							.requiredSourceVerticalFovDegrees[1] / diagnosticStep)
							* diagnosticStep;
					const MaximumRollCoverage leftLimit =
							projectionFitDiagnostics.maximumRollCoverages[0];
					const MaximumRollCoverage rightLimit =
							projectionFitDiagnostics.maximumRollCoverages[1];
					std::cerr << std::fixed << std::setprecision(3)
							  << "  Left eye requires at least " << requiredLeft
							  << " degrees source VFOV for +/-"
							  << options.rollCoverageDegrees << " degrees roll.\n"
							  << "  Right eye requires at least " << requiredRight
							  << " degrees source VFOV for +/-"
							  << options.rollCoverageDegrees << " degrees roll.\n";
					if (leftLimit.supportsZeroCoverage
							&& rightLimit.supportsZeroCoverage) {
						const float bothEyesLimit = std::floor(
								std::min(leftLimit.degrees, rightLimit.degrees)
								/ diagnosticStep) * diagnosticStep;
						std::cerr << "  At " << options.sourceVerticalFovDegrees
								  << " degrees source VFOV, both eyes support at most +/-"
								  << bothEyesLimit << " degrees fixed roll.\n"
								  << "Retry with --roll-coverage-deg no greater than that "
									 "conservatively rounded value. The probe will not auto-clamp.\n";
					} else {
						std::cerr << "  At " << options.sourceVerticalFovDegrees
								  << " degrees source VFOV, at least one eye cannot fit even "
									 "with zero fixed-roll coverage.\n";
					}
					std::cerr << std::defaultfloat << std::setprecision(6);
				}
				std::cerr << "Reduce --roll-coverage-deg or use the explicitly distorted "
							 "--fit stretch comparison.\n";
				renderFailed = true;
			} else if (viewResult == ProjectionViewBuildResult::frozenFovExceeded) {
				std::cerr << "SteamVR's required view FOV grew outside the projection "
						  << "calibration frozen at first display; refusing a zoom change.\n";
				renderFailed = true;
			} else if (viewResult == ProjectionViewBuildResult::sourceAspectChanged) {
				std::cerr << "The decoded source aspect changed after projection calibration; "
						  << "refusing to change the frozen FOV mapping.\n";
				renderFailed = true;
			} else {
				++invalidPoseFrames;
			}
		} else if (!stopInitiated && frame.state().shouldRender == XR_TRUE && !captureFresh) {
			++staleFrames;
		}

		XrCompositionLayerProjection projectionLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
		projectionLayer.space = localSpace;
		projectionLayer.viewCount = static_cast<std::uint32_t>(projectionViews.size());
		projectionLayer.views = projectionViews.data();
		const XrCompositionLayerBaseHeader* layers[]{
				reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projectionLayer)};
		if (!frame.end(layerReady ? layers : nullptr, layerReady ? 1U : 0U)) {
			terminalFailure = ExitCode::openXrSession;
			break;
		}
		if (renderFailed) {
			terminalFailure = ExitCode::rendering;
			continue;
		}
		if (layerReady) {
			++submittedFrames;
			lastSubmittedAt = std::chrono::steady_clock::now();
			if (firstSubmittedAt == std::chrono::steady_clock::time_point{}) {
				firstSubmittedAt = lastSubmittedAt;
				std::cout << "First live immersive projection frame submitted. Display timer started.\n";
			}
		}
		if (now - lastStatus >= statusInterval) {
			const WindowCaptureStats& stats = capture.stats();
			std::cout << "XR frames=" << submittedFrames
					  << " capture=" << stats.usableFrames << '/' << stats.receivedFrames
					  << " stale=" << staleFrames << " invalid-pose=" << invalidPoseFrames
					  << " resizes=" << stats.resizes << '\n';
			lastStatus = now;
		}
	}

	const WindowCaptureStats finalStats = capture.stats();
	const bool cleanupSucceeded = cleanup();
	std::cout << "Capture summary: received=" << finalStats.receivedFrames
			  << " usable=" << finalStats.usableFrames
			  << " discarded=" << finalStats.discardedFrames
			  << " resizes=" << finalStats.resizes << '\n';
	if (terminalFailure) {
		return *terminalFailure;
	}
	if (!cleanupSucceeded) {
		std::cerr << "Immersive-capture GPU teardown did not complete safely.\n";
		return ExitCode::rendering;
	}
	if (runtimeLoss || shutdownFailed || !stopInitiated) {
		std::cerr << "Immersive-capture probe ended before a normal bounded shutdown.\n";
		return ExitCode::openXrSession;
	}
	if (cancelledBeforeValidation) {
		std::cout << "Immersive-capture probe was cancelled before display validation.\n";
		return ExitCode::cancelledBeforeValidation;
	}
	if (submittedFrames == 0) {
		std::cerr << "No live captured stereo frame was successfully projected.\n";
		return ExitCode::rendering;
	}
	std::cout << "Immersive-capture probe completed after " << submittedFrames
			  << " submitted frame(s).\n";
	return ExitCode::success;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
	std::wcout << L"MCXRInput live OpenXR immersive-capture probe\n";
	Options options;
	if (!parseOptions(argc, argv, options)) {
		printUsage();
		return static_cast<int>(ExitCode::usage);
	}
	if (options.help) {
		printUsage();
		return static_cast<int>(ExitCode::success);
	}
	if (options.listWindows) {
		auto windows = enumerateWindows();
		if (options.executable) {
			windows = filterWindowsByExecutable(windows, *options.executable);
		}
		std::wcout << L"Matching visible windows: " << windows.size() << L'\n';
		for (const WindowCandidate& window : windows) {
			printWindow(window, std::wcout);
		}
		return static_cast<int>(ExitCode::success);
	}

	const WindowSelectionOptions selection{options.executable, options.window};
	const auto selected = selectWindow(selection, std::wcerr);
	if (!selected) {
		return static_cast<int>(ExitCode::windowSelection);
	}
	std::wcout << L"Selected window:\n";
	printWindow(*selected, std::wcout);
	SetConsoleCtrlHandler(handleConsoleControl, TRUE);

	try {
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
		return static_cast<int>(runImmersiveCapture(options, *selected));
	} catch (const winrt::hresult_error& error) {
		std::wcerr << L"Initializing immersive capture failed (HRESULT 0x" << std::hex
				   << static_cast<std::uint32_t>(error.code().value) << std::dec
				   << L"): " << error.message().c_str() << L'\n';
		return static_cast<int>(ExitCode::capture);
	} catch (const std::exception& error) {
		std::cerr << "Immersive capture initialization failed: " << error.what() << '\n';
		return static_cast<int>(ExitCode::capture);
	}
}
