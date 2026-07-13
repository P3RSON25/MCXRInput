#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>

#include <d3d11.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mcxrinput::native {

struct WindowCandidate {
	HWND window{nullptr};
	DWORD processId{0};
	std::wstring title;
	std::filesystem::path executable;
	int clientWidth{0};
	int clientHeight{0};
	bool minimized{false};
};

struct WindowSelectionOptions {
	std::optional<std::filesystem::path> executable;
	std::optional<HWND> window;
};

/** Parses the strict hexadecimal form used by the capture probe, such as 0x123ABC. */
bool parseWindowHandle(std::wstring_view text, HWND& window) noexcept;

/** Describes only a visible, uncloaked, titled top-level window. */
std::optional<WindowCandidate> describeWindow(HWND window);
std::vector<WindowCandidate> enumerateWindows();
std::vector<WindowCandidate> filterWindowsByExecutable(
		const std::vector<WindowCandidate>& candidates,
		const std::filesystem::path& executable);
void printWindow(const WindowCandidate& candidate, std::wostream& output);

/**
 * Applies the capture probe's deterministic selection policy. Exactly one of
 * executable and window must be present. Executable selection succeeds only
 * when exactly one visible window matches the full normalized path.
 */
std::optional<WindowCandidate> selectWindow(
		const WindowSelectionOptions& options, std::wostream& errors);

enum class WindowCaptureUpdate {
	none,
	frameReady,
	resized,
	minimized,
	restored,
	windowClosed,
	invalidStereoFrame,
	failure,
};

struct WindowCaptureFrame {
	Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
	std::uint32_t combinedWidth{0};
	std::uint32_t height{0};
	std::uint64_t generation{0};
	// WGC's QPC-based SystemRelativeTime mapped into steady_clock's epoch.
	// This is the compositor capture time, not the later time poll() dequeued it.
	std::chrono::steady_clock::time_point receivedAt{};
};

struct WindowCaptureStats {
	std::uint64_t receivedFrames{0};
	std::uint64_t usableFrames{0};
	std::uint64_t discardedFrames{0};
	std::uint64_t resizes{0};
};

/**
 * Owns a Windows Graphics Capture session backed by an existing D3D11 device.
 * start(), poll(), and stop() must run on the OpenXR/render thread. Free-threaded
 * capture callbacks only publish atomic wake-up signals and never use the D3D11
 * immediate context.
 */
class WindowCaptureSource {
public:
	WindowCaptureSource();
	~WindowCaptureSource();

	WindowCaptureSource(const WindowCaptureSource&) = delete;
	WindowCaptureSource& operator=(const WindowCaptureSource&) = delete;
	WindowCaptureSource(WindowCaptureSource&&) = delete;
	WindowCaptureSource& operator=(WindowCaptureSource&&) = delete;

	/**
	 * Wraps the supplied device for Windows Graphics Capture. The caller must
	 * initialize a multithreaded Windows Runtime apartment before this call and
	 * keep the device alive until stop() returns.
	 */
	bool start(HWND window, ID3D11Device* device);

	/**
	 * Drains queued frames and copies the newest one into an owned BGRA8 texture.
	 * The context must be the immediate context belonging to the start() device.
	 */
	WindowCaptureUpdate poll(ID3D11DeviceContext* context);
	void stop() noexcept;

	[[nodiscard]] bool active() const noexcept;
	[[nodiscard]] const WindowCaptureFrame& latestFrame() const noexcept;
	[[nodiscard]] bool hasFreshFrame(
			std::chrono::steady_clock::duration maximumAge) const noexcept;
	[[nodiscard]] const WindowCaptureStats& stats() const noexcept;
	[[nodiscard]] std::wstring_view lastError() const noexcept;

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace mcxrinput::native
