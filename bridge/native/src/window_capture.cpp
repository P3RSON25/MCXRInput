#include <mcxrinput/window_capture.hpp>

#include <dwmapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <iomanip>
#include <limits>
#include <ostream>
#include <utility>

namespace capture = winrt::Windows::Graphics::Capture;
namespace directx = winrt::Windows::Graphics::DirectX;
namespace direct3d = winrt::Windows::Graphics::DirectX::Direct3D11;

namespace mcxrinput::native {

namespace {

std::wstring queryWindowTitle(HWND window) {
	const int length = GetWindowTextLengthW(window);
	if (length <= 0) {
		return {};
	}
	std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
	const int copied = GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
	if (copied <= 0) {
		return {};
	}
	title.resize(static_cast<std::size_t>(copied));
	return title;
}

std::filesystem::path queryProcessExecutable(DWORD processId) {
	const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
	if (process == nullptr) {
		return {};
	}
	std::wstring path(32768, L'\0');
	DWORD length = static_cast<DWORD>(path.size());
	const BOOL success = QueryFullProcessImageNameW(process, 0, path.data(), &length);
	CloseHandle(process);
	if (!success || length == 0) {
		return {};
	}
	path.resize(length);
	return std::filesystem::path{path}.lexically_normal();
}

bool pathsEqual(const std::filesystem::path& left, const std::filesystem::path& right) {
	std::wstring leftText = left.lexically_normal().wstring();
	std::wstring rightText = right.lexically_normal().wstring();
	if (leftText.starts_with(L"\\\\?\\")) {
		leftText.erase(0, 4);
	}
	if (rightText.starts_with(L"\\\\?\\")) {
		rightText.erase(0, 4);
	}
	return CompareStringOrdinal(
			leftText.c_str(), static_cast<int>(leftText.size()),
			rightText.c_str(), static_cast<int>(rightText.size()), TRUE) == CSTR_EQUAL;
}

BOOL CALLBACK enumerateWindowCallback(HWND window, LPARAM parameter) {
	auto& candidates = *reinterpret_cast<std::vector<WindowCandidate>*>(parameter);
	if (auto candidate = describeWindow(window)) {
		candidates.push_back(std::move(*candidate));
	}
	return TRUE;
}

std::wstring hresultText(HRESULT result) {
	wchar_t* message = nullptr;
	const DWORD length = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, static_cast<DWORD>(result), 0,
			reinterpret_cast<wchar_t*>(&message), 0, nullptr);
	std::wstring text = length != 0 && message != nullptr
			? std::wstring{message, length}
			: L"unknown Windows error";
	if (message != nullptr) {
		LocalFree(message);
	}
	while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) {
		text.pop_back();
	}
	return text;
}

std::wstring makeHresultError(std::wstring_view operation, HRESULT result) {
	wchar_t code[16]{};
	swprintf_s(code, 16, L"0x%08X", static_cast<unsigned>(result));
	return std::wstring{operation} + L" failed (HRESULT " + code + L", "
			+ hresultText(result) + L").";
}

bool sameDevice(ID3D11Device* expected, ID3D11Device* actual) {
	if (expected == nullptr || actual == nullptr) {
		return false;
	}
	Microsoft::WRL::ComPtr<IUnknown> expectedIdentity;
	Microsoft::WRL::ComPtr<IUnknown> actualIdentity;
	return SUCCEEDED(expected->QueryInterface(IID_PPV_ARGS(&expectedIdentity)))
			&& SUCCEEDED(actual->QueryInterface(IID_PPV_ARGS(&actualIdentity)))
			&& expectedIdentity.Get() == actualIdentity.Get();
}

bool mapQpcTimeToSteady(
		const winrt::Windows::Foundation::TimeSpan& systemRelativeTime,
		std::chrono::steady_clock::time_point& mappedTime) noexcept {
	// Direct3D11CaptureFrame.SystemRelativeTime is QPC time expressed as a
	// WinRT TimeSpan (100-nanosecond ticks). Keep its epoch out of steady_clock:
	// calculate age in QPC space, then subtract that age from steady_clock::now().
	static const LONGLONG qpcFrequency = [] {
		LARGE_INTEGER frequency{};
		return QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0
				? frequency.QuadPart
				: LONGLONG{0};
	}();
	if (qpcFrequency <= 0 || systemRelativeTime.count() <= 0) {
		return false;
	}

	LARGE_INTEGER qpcNow{};
	if (!QueryPerformanceCounter(&qpcNow) || qpcNow.QuadPart < 0) {
		return false;
	}

	constexpr long double timeSpanTicksPerSecond = 10'000'000.0L;
	const long double frameSeconds =
			static_cast<long double>(systemRelativeTime.count()) / timeSpanTicksPerSecond;
	const long double nowSeconds =
			static_cast<long double>(qpcNow.QuadPart) / static_cast<long double>(qpcFrequency);
	long double ageSeconds = nowSeconds - frameSeconds;
	if (!std::isfinite(ageSeconds)) {
		return false;
	}

	// Independent counter reads can make a just-produced frame appear a few
	// microseconds in the future. Clamp small skew, but reject a mismatched epoch.
	constexpr long double toleratedFutureSkewSeconds = 0.250L;
	if (ageSeconds < -toleratedFutureSkewSeconds) {
		return false;
	}
	ageSeconds = std::max(0.0L, ageSeconds);

	using FloatingSeconds = std::chrono::duration<long double>;
	const long double maximumAgeSeconds = FloatingSeconds{
			std::chrono::steady_clock::duration::max()}.count();
	if (ageSeconds > maximumAgeSeconds) {
		return false;
	}

	const auto age = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			FloatingSeconds{ageSeconds});
	mappedTime = std::chrono::steady_clock::now() - age;
	return true;
}

capture::GraphicsCaptureItem createCaptureItem(HWND window) {
	auto interop = winrt::get_activation_factory<
			capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
	capture::GraphicsCaptureItem item{nullptr};
	winrt::check_hresult(interop->CreateForWindow(
			window,
			winrt::guid_of<capture::GraphicsCaptureItem>(),
			winrt::put_abi(item)));
	return item;
}

} // namespace

bool parseWindowHandle(std::wstring_view text, HWND& window) noexcept {
	if (text.size() <= 2 || text[0] != L'0' || (text[1] != L'x' && text[1] != L'X')) {
		return false;
	}
	std::uint64_t value = 0;
	for (wchar_t character : text.substr(2)) {
		unsigned digit = 0;
		if (character >= L'0' && character <= L'9') {
			digit = static_cast<unsigned>(character - L'0');
		} else if (character >= L'a' && character <= L'f') {
			digit = static_cast<unsigned>(character - L'a' + 10);
		} else if (character >= L'A' && character <= L'F') {
			digit = static_cast<unsigned>(character - L'A' + 10);
		} else {
			return false;
		}
		if (value > (std::numeric_limits<std::uintptr_t>::max() - digit) / 16) {
			return false;
		}
		value = value * 16 + digit;
	}
	if (value == 0) {
		return false;
	}
	window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(value));
	return true;
}

std::optional<WindowCandidate> describeWindow(HWND window) {
	if (!IsWindow(window) || GetAncestor(window, GA_ROOT) != window || !IsWindowVisible(window)) {
		return std::nullopt;
	}
	BOOL cloaked = FALSE;
	if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) {
		return std::nullopt;
	}

	WindowCandidate candidate;
	candidate.window = window;
	GetWindowThreadProcessId(window, &candidate.processId);
	candidate.title = queryWindowTitle(window);
	if (candidate.processId == 0 || candidate.title.empty()) {
		return std::nullopt;
	}
	candidate.executable = queryProcessExecutable(candidate.processId);
	RECT client{};
	if (GetClientRect(window, &client)) {
		candidate.clientWidth = std::max(0L, client.right - client.left);
		candidate.clientHeight = std::max(0L, client.bottom - client.top);
	}
	candidate.minimized = IsIconic(window) != FALSE;
	return candidate;
}

std::vector<WindowCandidate> enumerateWindows() {
	std::vector<WindowCandidate> candidates;
	EnumWindows(enumerateWindowCallback, reinterpret_cast<LPARAM>(&candidates));
	return candidates;
}

std::vector<WindowCandidate> filterWindowsByExecutable(
		const std::vector<WindowCandidate>& candidates,
		const std::filesystem::path& executable) {
	std::vector<WindowCandidate> matches;
	for (const WindowCandidate& candidate : candidates) {
		if (!candidate.executable.empty() && pathsEqual(candidate.executable, executable)) {
			matches.push_back(candidate);
		}
	}
	return matches;
}

void printWindow(const WindowCandidate& candidate, std::wostream& output) {
	output << L"  HWND=0x" << std::hex
		   << reinterpret_cast<std::uintptr_t>(candidate.window) << std::dec
		   << L" PID=" << candidate.processId
		   << L" client=" << candidate.clientWidth << L'x' << candidate.clientHeight
		   << (candidate.minimized ? L" minimized" : L"")
		   << L"\n    title: " << candidate.title
		   << L"\n    executable: "
		   << (candidate.executable.empty() ? L"<unavailable>" : candidate.executable.wstring())
		   << L'\n';
}

std::optional<WindowCandidate> selectWindow(
		const WindowSelectionOptions& options, std::wostream& errors) {
	if (options.executable.has_value() == options.window.has_value()) {
		errors << L"Window selection requires exactly one of an executable path or HWND.\n";
		return std::nullopt;
	}
	if (options.window) {
		auto candidate = describeWindow(*options.window);
		if (!candidate) {
			errors << L"The requested HWND is not a visible, capturable window.\n";
		}
		return candidate;
	}
	if (options.executable->empty() || !options.executable->is_absolute()) {
		errors << L"The executable selector must be an absolute path.\n";
		return std::nullopt;
	}

	const auto normalized = options.executable->lexically_normal();
	const auto matches = filterWindowsByExecutable(enumerateWindows(), normalized);
	if (matches.size() == 1) {
		return matches.front();
	}
	if (matches.empty()) {
		errors << L"No visible window belongs to\n  " << normalized.wstring()
			   << L"\nStart the borderless Minecraft instance and try again.\n";
		return std::nullopt;
	}

	errors << L"Multiple windows use that executable; choose one with --window.\n";
	for (const WindowCandidate& candidate : matches) {
		printWindow(candidate, errors);
	}
	return std::nullopt;
}

class WindowCaptureSource::Impl {
public:
	struct Signals {
		std::atomic_bool frameArrived{false};
		std::atomic_bool windowClosed{false};
	};

	~Impl() {
		stop();
	}

	bool start(HWND requestedWindow, ID3D11Device* requestedDevice) {
		stop();
		lastError.clear();
		stats = {};
		generation = 0;
		latest.generation = 0;

		if (requestedWindow == nullptr || !IsWindow(requestedWindow)) {
			lastError = L"Cannot capture an invalid window handle.";
			return false;
		}
		if (requestedDevice == nullptr) {
			lastError = L"Cannot capture without the OpenXR D3D11 device.";
			return false;
		}
		if (IsIconic(requestedWindow)) {
			lastError = L"Restore the selected window before starting capture.";
			return false;
		}

		try {
			if (!capture::GraphicsCaptureSession::IsSupported()) {
				lastError = L"Windows Graphics Capture is not supported on this system.";
				return false;
			}
			winrt::com_ptr<IDXGIDevice> dxgiDevice;
			winrt::check_hresult(requestedDevice->QueryInterface(IID_PPV_ARGS(dxgiDevice.put())));
			winrt::com_ptr<IInspectable> inspectable;
			winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(
					dxgiDevice.get(), inspectable.put()));
			winrtDevice = inspectable.as<direct3d::IDirect3DDevice>();

			item = createCaptureItem(requestedWindow);
			poolSize = item.Size();
			if (poolSize.Width <= 0 || poolSize.Height <= 0) {
				lastError = L"The selected window has no capturable area.";
				stop();
				return false;
			}

			framePool = capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
					winrtDevice, pixelFormat, 2, poolSize);
			session = framePool.CreateCaptureSession(item);
			session.IsCursorCaptureEnabled(true);
			signals = std::make_shared<Signals>();
			frameToken = framePool.FrameArrived([published = signals](auto&&, auto&&) {
				published->frameArrived.store(true, std::memory_order_release);
			});
			frameTokenAttached = true;
			closedToken = item.Closed([published = signals](auto&&, auto&&) {
				published->windowClosed.store(true, std::memory_order_release);
			});
			closedTokenAttached = true;
			session.StartCapture();
		} catch (const winrt::hresult_error& error) {
			lastError = makeHresultError(L"Starting Windows window capture", error.code());
			stopObjects();
			return false;
		}

		window = requestedWindow;
		device = requestedDevice;
		minimized = false;
		isActive = true;
		return true;
	}

	WindowCaptureUpdate poll(ID3D11DeviceContext* context) {
		if (!isActive) {
			lastError = L"Window capture is not active.";
			return WindowCaptureUpdate::failure;
		}
		if (context == nullptr) {
			return fail(L"Cannot poll capture without the OpenXR D3D11 immediate context.");
		}
		Microsoft::WRL::ComPtr<ID3D11Device> contextDevice;
		context->GetDevice(&contextDevice);
		if (!sameDevice(device.Get(), contextDevice.Get())) {
			return fail(L"The capture context does not belong to the OpenXR D3D11 device.");
		}

		if (signals->windowClosed.load(std::memory_order_acquire) || !IsWindow(window)) {
			invalidateLatest();
			stopObjects();
			return WindowCaptureUpdate::windowClosed;
		}

		const bool nowMinimized = IsIconic(window) != FALSE;
		if (nowMinimized) {
			drainAndDiscard();
			invalidateLatest();
			if (!minimized) {
				minimized = true;
				return WindowCaptureUpdate::minimized;
			}
			return WindowCaptureUpdate::none;
		}
		if (minimized) {
			minimized = false;
			drainAndDiscard();
			invalidateLatest();
			return WindowCaptureUpdate::restored;
		}

		if (!signals->frameArrived.exchange(false, std::memory_order_acq_rel)) {
			return WindowCaptureUpdate::none;
		}

		capture::Direct3D11CaptureFrame newest{nullptr};
		try {
			for (;;) {
				auto frame = framePool.TryGetNextFrame();
				if (!frame) {
					break;
				}
				++stats.receivedFrames;
				if (newest) {
					newest.Close();
					++stats.discardedFrames;
				}
				newest = std::move(frame);
			}
		} catch (const winrt::hresult_error& error) {
			if (error.code() == RO_E_CLOSED
					&& signals->windowClosed.load(std::memory_order_acquire)) {
				invalidateLatest();
				stopObjects();
				return WindowCaptureUpdate::windowClosed;
			}
			return fail(makeHresultError(L"Draining captured frames", error.code()));
		}
		if (!newest) {
			return WindowCaptureUpdate::none;
		}

		const auto size = newest.ContentSize();
		if (size.Width <= 0 || size.Height <= 0) {
			newest.Close();
			++stats.discardedFrames;
			invalidateLatest();
			lastError = L"The captured frame has no usable area.";
			return WindowCaptureUpdate::invalidStereoFrame;
		}

		if (size.Width != poolSize.Width || size.Height != poolSize.Height) {
			newest.Close();
			++stats.discardedFrames;
			invalidateLatest();
			try {
				framePool.Recreate(winrtDevice, pixelFormat, 2, size);
				poolSize = size;
				++stats.resizes;
			} catch (const winrt::hresult_error& error) {
				return fail(makeHresultError(L"Resizing the capture frame pool", error.code()));
			}
			if ((size.Width & 1) != 0) {
				lastError = L"The resized capture width is odd and cannot be split into equal stereo eyes.";
				return WindowCaptureUpdate::invalidStereoFrame;
			}
			return WindowCaptureUpdate::resized;
		}

		if ((size.Width & 1) != 0) {
			newest.Close();
			++stats.discardedFrames;
			invalidateLatest();
			lastError = L"The capture width is odd and cannot be split into equal stereo eyes.";
			return WindowCaptureUpdate::invalidStereoFrame;
		}

		std::chrono::steady_clock::time_point capturedAt{};
		try {
			if (!mapQpcTimeToSteady(newest.SystemRelativeTime(), capturedAt)) {
				newest.Close();
				++stats.discardedFrames;
				invalidateLatest();
				lastError = L"Windows Graphics Capture returned an invalid frame timestamp.";
				return WindowCaptureUpdate::invalidStereoFrame;
			}
			if (!copyNewest(newest, context, size)) {
				newest.Close();
				++stats.discardedFrames;
				return fail(lastError);
			}
		} catch (const winrt::hresult_error& error) {
			newest.Close();
			++stats.discardedFrames;
			return fail(makeHresultError(L"Accessing the captured D3D11 texture", error.code()));
		}
		newest.Close();

		latest.combinedWidth = static_cast<std::uint32_t>(size.Width);
		latest.height = static_cast<std::uint32_t>(size.Height);
		latest.generation = ++generation;
		latest.receivedAt = capturedAt;
		++stats.usableFrames;
		lastError.clear();
		return WindowCaptureUpdate::frameReady;
	}

	void stop() noexcept {
		invalidateLatest();
		stopObjects();
		window = nullptr;
		device.Reset();
		minimized = false;
	}

	bool fresh(std::chrono::steady_clock::duration maximumAge) const noexcept {
		const auto now = std::chrono::steady_clock::now();
		return latest.texture && maximumAge >= std::chrono::steady_clock::duration::zero()
				&& latest.receivedAt <= now
				&& now - latest.receivedAt <= maximumAge;
	}

	WindowCaptureUpdate fail(std::wstring message) {
		lastError = std::move(message);
		invalidateLatest();
		stopObjects();
		return WindowCaptureUpdate::failure;
	}

	void invalidateLatest() noexcept {
		latest.texture.Reset();
		latest.combinedWidth = 0;
		latest.height = 0;
		latest.receivedAt = {};
	}

	void drainAndDiscard() noexcept {
		if (!framePool) {
			return;
		}
		try {
			for (;;) {
				auto frame = framePool.TryGetNextFrame();
				if (!frame) {
					break;
				}
				++stats.receivedFrames;
				++stats.discardedFrames;
				frame.Close();
			}
		} catch (...) {
			// Teardown/minimize paths are fail-closed; no captured surface is retained.
		}
	}

	bool copyNewest(
			const capture::Direct3D11CaptureFrame& frame,
			ID3D11DeviceContext* context,
			const winrt::Windows::Graphics::SizeInt32& size) {
		auto access = frame.Surface().as<
				::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
		Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
		winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&source)));

		D3D11_TEXTURE2D_DESC sourceDescription{};
		source->GetDesc(&sourceDescription);
		if (sourceDescription.Format != DXGI_FORMAT_B8G8R8A8_UNORM
				|| sourceDescription.SampleDesc.Count != 1
				|| sourceDescription.Width < static_cast<UINT>(size.Width)
				|| sourceDescription.Height < static_cast<UINT>(size.Height)) {
			lastError = L"Windows Graphics Capture returned an unsupported D3D11 texture.";
			return false;
		}

		D3D11_TEXTURE2D_DESC ownedDescription{};
		if (latest.texture) {
			latest.texture->GetDesc(&ownedDescription);
		}
		if (!latest.texture
				|| ownedDescription.Width != static_cast<UINT>(size.Width)
				|| ownedDescription.Height != static_cast<UINT>(size.Height)) {
			latest.texture.Reset();
			ownedDescription = sourceDescription;
			ownedDescription.Width = static_cast<UINT>(size.Width);
			ownedDescription.Height = static_cast<UINT>(size.Height);
			ownedDescription.MipLevels = 1;
			ownedDescription.ArraySize = 1;
			// The WGC surface is BGRA8_UNORM but contains SDR presentation values.
			// Copy into the compatible typeless family so the renderer can create an
			// sRGB SRV and correctly decode those values before linear filtering.
			ownedDescription.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS;
			ownedDescription.SampleDesc = {1, 0};
			ownedDescription.Usage = D3D11_USAGE_DEFAULT;
			ownedDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			ownedDescription.CPUAccessFlags = 0;
			ownedDescription.MiscFlags = 0;
			const HRESULT result = device->CreateTexture2D(
					&ownedDescription, nullptr, &latest.texture);
			if (FAILED(result)) {
				lastError = makeHresultError(L"Creating the owned capture texture", result);
				return false;
			}
		}

		const D3D11_BOX sourceBox{
				0, 0, 0,
				static_cast<UINT>(size.Width), static_cast<UINT>(size.Height), 1};
		context->CopySubresourceRegion(
				latest.texture.Get(), 0, 0, 0, 0, source.Get(), 0, &sourceBox);
		return true;
	}

	void stopObjects() noexcept {
		isActive = false;
		try {
			if (frameTokenAttached && framePool) {
				framePool.FrameArrived(frameToken);
			}
		} catch (...) {
		}
		frameTokenAttached = false;
		try {
			if (closedTokenAttached && item) {
				item.Closed(closedToken);
			}
		} catch (...) {
		}
		closedTokenAttached = false;
		try {
			if (session) {
				session.Close();
			}
		} catch (...) {
		}
		try {
			if (framePool) {
				framePool.Close();
			}
		} catch (...) {
		}
		session = nullptr;
		framePool = nullptr;
		item = nullptr;
		winrtDevice = nullptr;
		signals.reset();
		poolSize = {};
	}

	HWND window{nullptr};
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	direct3d::IDirect3DDevice winrtDevice{nullptr};
	capture::GraphicsCaptureItem item{nullptr};
	capture::Direct3D11CaptureFramePool framePool{nullptr};
	capture::GraphicsCaptureSession session{nullptr};
	directx::DirectXPixelFormat pixelFormat{directx::DirectXPixelFormat::B8G8R8A8UIntNormalized};
	winrt::Windows::Graphics::SizeInt32 poolSize{};
	std::shared_ptr<Signals> signals;
	winrt::event_token frameToken{};
	winrt::event_token closedToken{};
	bool frameTokenAttached{false};
	bool closedTokenAttached{false};
	bool isActive{false};
	bool minimized{false};
	std::uint64_t generation{0};
	WindowCaptureFrame latest;
	WindowCaptureStats stats;
	std::wstring lastError;
};

WindowCaptureSource::WindowCaptureSource()
		: impl_{std::make_unique<Impl>()} {
}

WindowCaptureSource::~WindowCaptureSource() = default;

bool WindowCaptureSource::start(HWND window, ID3D11Device* device) {
	return impl_->start(window, device);
}

WindowCaptureUpdate WindowCaptureSource::poll(ID3D11DeviceContext* context) {
	return impl_->poll(context);
}

void WindowCaptureSource::stop() noexcept {
	impl_->stop();
}

bool WindowCaptureSource::active() const noexcept {
	return impl_->isActive;
}

const WindowCaptureFrame& WindowCaptureSource::latestFrame() const noexcept {
	return impl_->latest;
}

bool WindowCaptureSource::hasFreshFrame(
		std::chrono::steady_clock::duration maximumAge) const noexcept {
	return impl_->fresh(maximumAge);
}

const WindowCaptureStats& WindowCaptureSource::stats() const noexcept {
	return impl_->stats;
}

std::wstring_view WindowCaptureSource::lastError() const noexcept {
	return impl_->lastError;
}

} // namespace mcxrinput::native
