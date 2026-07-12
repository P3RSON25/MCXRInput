#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace capture = winrt::Windows::Graphics::Capture;
namespace directx = winrt::Windows::Graphics::DirectX;
namespace direct3d = winrt::Windows::Graphics::DirectX::Direct3D11;

namespace {

constexpr int defaultCaptureSeconds = 10;
constexpr int maximumCaptureSeconds = 300;
constexpr auto maximumSnapshotAge = std::chrono::seconds{1};
constexpr std::wstring_view defaultSnapshotName = L"MCXRInputCapture.png";

std::atomic_bool stopRequested{false};

enum class ExitCode : int {
	success = 0,
	usage = 1,
	windowSelection = 2,
	initialization = 3,
	captureFailed = 4,
	invalidStereoFrame = 5,
	snapshotFailed = 6,
};

struct Options {
	bool help{false};
	bool listWindows{false};
	std::optional<std::filesystem::path> executable;
	std::optional<HWND> window;
	std::filesystem::path snapshot{std::wstring{defaultSnapshotName}};
	int seconds{defaultCaptureSeconds};
	bool snapshotSpecified{false};
	bool secondsSpecified{false};
};

struct WindowCandidate {
	HWND window{nullptr};
	DWORD processId{0};
	std::wstring title;
	std::filesystem::path executable;
	int clientWidth{0};
	int clientHeight{0};
	bool minimized{false};
};

struct D3DResources {
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	direct3d::IDirect3DDevice winrtDevice{nullptr};
	D3D_FEATURE_LEVEL featureLevel{};
};

struct CaptureSignals {
	std::mutex mutex;
	std::condition_variable condition;
	bool frameArrived{false};
	std::atomic_bool windowClosed{false};
};

struct CaptureStats {
	std::uint64_t receivedFrames{0};
	std::uint64_t usableFrames{0};
	std::uint64_t discardedFrames{0};
	std::uint64_t resizes{0};
	std::optional<std::chrono::steady_clock::time_point> firstUsableFrame;
	std::optional<std::chrono::steady_clock::time_point> lastUsableFrame;
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

void printUsage() {
	std::wcout
			<< L"Usage:\n"
			<< L"  MCXRInputCaptureProbe.exe --list-windows [--executable <absolute-path>]\n"
			<< L"  MCXRInputCaptureProbe.exe (--executable <absolute-path> | --window <0xHWND>)\n"
			<< L"      [--snapshot <png-path>] [--seconds <1..300>]\n\n"
			<< L"Captures one Windows window into a D3D11 texture and saves the newest\n"
			<< L"valid half-SBS frame. This diagnostic does not start OpenXR or read game memory.\n";
}

bool parsePositiveInt(std::wstring_view text, int minimum, int maximum, int& value) {
	if (text.empty()) {
		return false;
	}
	std::string narrow;
	narrow.reserve(text.size());
	for (wchar_t character : text) {
		if (character < L'0' || character > L'9') {
			return false;
		}
		narrow.push_back(static_cast<char>(character));
	}
	int parsed = 0;
	const auto result = std::from_chars(narrow.data(), narrow.data() + narrow.size(), parsed);
	if (result.ec != std::errc{} || result.ptr != narrow.data() + narrow.size()
			|| parsed < minimum || parsed > maximum) {
		return false;
	}
	value = parsed;
	return true;
}

bool parseWindowHandle(std::wstring_view text, HWND& window) {
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

bool hasPngExtension(const std::filesystem::path& path) {
	const std::wstring extension = path.extension().wstring();
	return _wcsicmp(extension.c_str(), L".png") == 0;
}

bool parseOptions(int argc, wchar_t** argv, Options& options) {
	bool executableSeen = false;
	bool windowSeen = false;
	bool listSeen = false;
	for (int index = 1; index < argc; ++index) {
		const std::wstring_view argument{argv[index]};
		if (argument == L"--help" || argument == L"-h") {
			if (options.help) {
				std::wcerr << L"Duplicate --help option.\n";
				return false;
			}
			options.help = true;
			continue;
		}
		if (argument == L"--list-windows") {
			if (listSeen) {
				std::wcerr << L"Duplicate --list-windows option.\n";
				return false;
			}
			listSeen = true;
			options.listWindows = true;
			continue;
		}
		if (argument == L"--executable") {
			if (executableSeen || index + 1 >= argc) {
				std::wcerr << L"Expected one --executable followed by an absolute path.\n";
				return false;
			}
			executableSeen = true;
			options.executable = std::filesystem::path{argv[++index]};
			continue;
		}
		if (argument == L"--window") {
			if (windowSeen || index + 1 >= argc) {
				std::wcerr << L"Expected one --window followed by a hexadecimal 0x handle.\n";
				return false;
			}
			HWND parsed = nullptr;
			if (!parseWindowHandle(argv[++index], parsed)) {
				std::wcerr << L"Expected --window to use hexadecimal form, for example 0x123ABC.\n";
				return false;
			}
			windowSeen = true;
			options.window = parsed;
			continue;
		}
		if (argument == L"--snapshot") {
			if (options.snapshotSpecified || index + 1 >= argc) {
				std::wcerr << L"Expected one --snapshot followed by a PNG path.\n";
				return false;
			}
			options.snapshotSpecified = true;
			options.snapshot = std::filesystem::path{argv[++index]};
			continue;
		}
		if (argument == L"--seconds") {
			if (options.secondsSpecified || index + 1 >= argc
					|| !parsePositiveInt(argv[index + 1], 1, maximumCaptureSeconds, options.seconds)) {
				std::wcerr << L"Expected one --seconds value from 1 to 300.\n";
				return false;
			}
			options.secondsSpecified = true;
			++index;
			continue;
		}

		std::wcerr << L"Unknown argument: " << argument << L'\n';
		return false;
	}

	if (options.help) {
		if (argc != 2) {
			std::wcerr << L"--help cannot be combined with other options.\n";
			return false;
		}
		return true;
	}
	if (options.listWindows) {
		if (options.window || options.snapshotSpecified || options.secondsSpecified) {
			std::wcerr << L"--list-windows accepts only the optional --executable filter.\n";
			return false;
		}
	} else if (options.executable.has_value() == options.window.has_value()) {
		std::wcerr << L"Capture requires exactly one of --executable or --window.\n";
		return false;
	}

	if (options.executable) {
		if (options.executable->empty() || !options.executable->is_absolute()) {
			std::wcerr << L"--executable must be an absolute path.\n";
			return false;
		}
		*options.executable = options.executable->lexically_normal();
	}
	if (!hasPngExtension(options.snapshot)) {
		std::wcerr << L"--snapshot must name a .png file.\n";
		return false;
	}
	return true;
}

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

BOOL CALLBACK enumerateWindowCallback(HWND window, LPARAM parameter) {
	auto& candidates = *reinterpret_cast<std::vector<WindowCandidate>*>(parameter);
	if (auto candidate = describeWindow(window)) {
		candidates.push_back(std::move(*candidate));
	}
	return TRUE;
}

std::vector<WindowCandidate> enumerateWindows() {
	std::vector<WindowCandidate> candidates;
	EnumWindows(enumerateWindowCallback, reinterpret_cast<LPARAM>(&candidates));
	return candidates;
}

void printWindow(const WindowCandidate& candidate) {
	std::wcout << L"  HWND=0x" << std::hex
			   << reinterpret_cast<std::uintptr_t>(candidate.window) << std::dec
			   << L" PID=" << candidate.processId
			   << L" client=" << candidate.clientWidth << L'x' << candidate.clientHeight
			   << (candidate.minimized ? L" minimized" : L"")
			   << L"\n    title: " << candidate.title
			   << L"\n    executable: "
			   << (candidate.executable.empty() ? L"<unavailable>" : candidate.executable.wstring())
			   << L'\n';
}

std::vector<WindowCandidate> filterByExecutable(
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

std::optional<WindowCandidate> selectWindow(const Options& options) {
	if (options.window) {
		auto candidate = describeWindow(*options.window);
		if (!candidate) {
			std::wcerr << L"MCXRInput capture probe: the requested HWND is not a visible, capturable window.\n";
		}
		return candidate;
	}

	const auto matches = filterByExecutable(enumerateWindows(), *options.executable);
	if (matches.size() == 1) {
		return matches.front();
	}
	if (matches.empty()) {
		std::wcerr << L"MCXRInput capture probe: no visible window belongs to\n  "
				   << options.executable->wstring() << L"\nStart the borderless Minecraft instance and try again.\n";
		return std::nullopt;
	}

	std::wcerr << L"MCXRInput capture probe: multiple windows use that executable; choose one with --window.\n";
	for (const WindowCandidate& candidate : matches) {
		printWindow(candidate);
	}
	return std::nullopt;
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

void printHresult(std::wstring_view operation, HRESULT result) {
	std::wcerr << L"MCXRInput capture probe: " << operation << L" failed (HRESULT 0x"
			   << std::hex << static_cast<std::uint32_t>(result) << std::dec
			   << L", " << hresultText(result) << L").\n";
}

bool createD3D11Device(D3DResources& d3d) {
	const std::array<D3D_FEATURE_LEVEL, 7> featureLevels{
			D3D_FEATURE_LEVEL_12_1,
			D3D_FEATURE_LEVEL_12_0,
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
			D3D_FEATURE_LEVEL_9_3,
	};
	const HRESULT result = D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT,
			featureLevels.data(),
			static_cast<UINT>(featureLevels.size()),
			D3D11_SDK_VERSION,
			&d3d.device,
			&d3d.featureLevel,
			&d3d.context);
	if (FAILED(result)) {
		printHresult(L"creating the D3D11 capture device", result);
		return false;
	}

	winrt::com_ptr<IDXGIDevice> dxgiDevice;
	winrt::check_hresult(d3d.device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put())));
	winrt::com_ptr<IInspectable> inspectable;
	winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
	d3d.winrtDevice = inspectable.as<direct3d::IDirect3DDevice>();

	ComPtr<IDXGIAdapter> adapter;
	if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
		DXGI_ADAPTER_DESC description{};
		if (SUCCEEDED(adapter->GetDesc(&description))) {
			std::wcout << L"D3D11 capture adapter: " << description.Description << L'\n';
		}
	}
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

bool copyFrameTexture(
		const capture::Direct3D11CaptureFrame& frame,
		ID3D11Device* device,
		ID3D11DeviceContext* context,
		ComPtr<ID3D11Texture2D>& destination) {
	const auto size = frame.ContentSize();
	if (size.Width <= 0 || size.Height <= 0) {
		return false;
	}

	auto access = frame.Surface().as<
			::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
	ComPtr<ID3D11Texture2D> source;
	winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&source)));

	D3D11_TEXTURE2D_DESC sourceDescription{};
	source->GetDesc(&sourceDescription);
	if (sourceDescription.Format != DXGI_FORMAT_B8G8R8A8_UNORM
			|| sourceDescription.SampleDesc.Count != 1
			|| sourceDescription.Width < static_cast<UINT>(size.Width)
			|| sourceDescription.Height < static_cast<UINT>(size.Height)) {
		std::wcerr << L"MCXRInput capture probe: received an unsupported capture texture.\n";
		return false;
	}

	D3D11_TEXTURE2D_DESC destinationDescription{};
	if (destination) {
		destination->GetDesc(&destinationDescription);
	}
	if (!destination
			|| destinationDescription.Width != static_cast<UINT>(size.Width)
			|| destinationDescription.Height != static_cast<UINT>(size.Height)) {
		destination.Reset();
		destinationDescription = sourceDescription;
		destinationDescription.Width = static_cast<UINT>(size.Width);
		destinationDescription.Height = static_cast<UINT>(size.Height);
		destinationDescription.MipLevels = 1;
		destinationDescription.ArraySize = 1;
		destinationDescription.Usage = D3D11_USAGE_DEFAULT;
		destinationDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		destinationDescription.CPUAccessFlags = 0;
		destinationDescription.MiscFlags = 0;
		const HRESULT createResult = device->CreateTexture2D(
				&destinationDescription, nullptr, &destination);
		if (FAILED(createResult)) {
			printHresult(L"creating the owned capture texture", createResult);
			return false;
		}
	}

	const D3D11_BOX sourceBox{
			0,
			0,
			0,
			static_cast<UINT>(size.Width),
			static_cast<UINT>(size.Height),
			1,
	};
	context->CopySubresourceRegion(destination.Get(), 0, 0, 0, 0, source.Get(), 0, &sourceBox);
	return true;
}

bool saveTextureToPng(
		ID3D11Device* device,
		ID3D11DeviceContext* context,
		ID3D11Texture2D* source,
		const std::filesystem::path& destination) {
	if (device == nullptr || context == nullptr || source == nullptr) {
		return false;
	}

	D3D11_TEXTURE2D_DESC sourceDescription{};
	source->GetDesc(&sourceDescription);
	if (sourceDescription.Width == 0 || sourceDescription.Height == 0
			|| sourceDescription.Format != DXGI_FORMAT_B8G8R8A8_UNORM
			|| sourceDescription.SampleDesc.Count != 1) {
		std::wcerr << L"MCXRInput capture probe: snapshot texture is not BGRA8 single-sample.\n";
		return false;
	}

	D3D11_TEXTURE2D_DESC stagingDescription = sourceDescription;
	stagingDescription.MipLevels = 1;
	stagingDescription.ArraySize = 1;
	stagingDescription.Usage = D3D11_USAGE_STAGING;
	stagingDescription.BindFlags = 0;
	stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDescription.MiscFlags = 0;
	ComPtr<ID3D11Texture2D> staging;
	HRESULT result = device->CreateTexture2D(&stagingDescription, nullptr, &staging);
	if (FAILED(result)) {
		printHresult(L"creating the PNG readback texture", result);
		return false;
	}

	context->CopyResource(staging.Get(), source);
	D3D11_MAPPED_SUBRESOURCE mapped{};
	result = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
	if (FAILED(result)) {
		printHresult(L"mapping the PNG readback texture", result);
		return false;
	}
	struct UnmapGuard {
		ID3D11DeviceContext* context;
		ID3D11Texture2D* texture;
		bool mapped{true};

		~UnmapGuard() {
			reset();
		}

		void reset() noexcept {
			if (mapped) {
				context->Unmap(texture, 0);
				mapped = false;
			}
		}
	} unmapGuard{context, staging.Get()};

	const std::uint64_t minimumStride = static_cast<std::uint64_t>(sourceDescription.Width) * 4;
	const std::uint64_t bufferSize = static_cast<std::uint64_t>(mapped.RowPitch)
			* sourceDescription.Height;
	if (mapped.pData == nullptr || mapped.RowPitch < minimumStride
			|| bufferSize > std::numeric_limits<UINT>::max()) {
		std::wcerr << L"MCXRInput capture probe: PNG readback layout is invalid.\n";
		return false;
	}

	std::filesystem::path temporary = destination;
	temporary += L".tmp";
	DeleteFileW(temporary.c_str());
	bool encoded = false;
	{
		ComPtr<IWICImagingFactory> factory;
		result = CoCreateInstance(
				CLSID_WICImagingFactory2,
				nullptr,
				CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(&factory));
		if (FAILED(result)) {
			result = CoCreateInstance(
					CLSID_WICImagingFactory,
					nullptr,
					CLSCTX_INPROC_SERVER,
					IID_PPV_ARGS(&factory));
		}

		ComPtr<IWICStream> stream;
		ComPtr<IWICBitmapEncoder> encoder;
		ComPtr<IWICBitmapFrameEncode> frame;
		ComPtr<IPropertyBag2> options;
		if (SUCCEEDED(result)) {
			result = factory->CreateStream(&stream);
		}
		if (SUCCEEDED(result)) {
			result = stream->InitializeFromFilename(temporary.c_str(), GENERIC_WRITE);
		}
		if (SUCCEEDED(result)) {
			result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
		}
		if (SUCCEEDED(result)) {
			result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
		}
		if (SUCCEEDED(result)) {
			result = encoder->CreateNewFrame(&frame, &options);
		}
		if (SUCCEEDED(result)) {
			result = frame->Initialize(options.Get());
		}
		if (SUCCEEDED(result)) {
			result = frame->SetSize(sourceDescription.Width, sourceDescription.Height);
		}
		WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
		if (SUCCEEDED(result)) {
			result = frame->SetPixelFormat(&pixelFormat);
		}
		if (SUCCEEDED(result) && !IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppBGRA)) {
			result = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
		}
		if (SUCCEEDED(result)) {
			result = frame->WritePixels(
					sourceDescription.Height,
					mapped.RowPitch,
					static_cast<UINT>(bufferSize),
					static_cast<BYTE*>(mapped.pData));
		}
		unmapGuard.reset();
		if (SUCCEEDED(result)) {
			result = frame->Commit();
		}
		if (SUCCEEDED(result)) {
			result = encoder->Commit();
		}
		encoded = SUCCEEDED(result);
	}

	if (!encoded) {
		printHresult(L"encoding the PNG snapshot", result);
		DeleteFileW(temporary.c_str());
		return false;
	}
	if (!MoveFileExW(
			temporary.c_str(), destination.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		printHresult(L"publishing the PNG snapshot", HRESULT_FROM_WIN32(GetLastError()));
		DeleteFileW(temporary.c_str());
		return false;
	}
	return true;
}

void printStereoDimensions(const winrt::Windows::Graphics::SizeInt32& size) {
	std::wcout << L"Combined " << size.Width << L'x' << size.Height
			   << L"; left " << size.Width / 2 << L'x' << size.Height
			   << L"; right " << size.Width / 2 << L'x' << size.Height << L'\n';
}

ExitCode runCapture(const Options& options, const WindowCandidate& selected) {
	if (selected.minimized || selected.clientWidth <= 0 || selected.clientHeight <= 0) {
		std::wcerr << L"MCXRInput capture probe: restore the Minecraft window before capture.\n";
		return ExitCode::captureFailed;
	}
	if (!capture::GraphicsCaptureSession::IsSupported()) {
		std::wcerr << L"MCXRInput capture probe: Windows Graphics Capture is not supported.\n";
		return ExitCode::initialization;
	}

	D3DResources d3d;
	if (!createD3D11Device(d3d)) {
		return ExitCode::initialization;
	}

	capture::GraphicsCaptureItem item{nullptr};
	try {
		item = createCaptureItem(selected.window);
	} catch (const winrt::hresult_error& error) {
		printHresult(L"creating the window capture item", error.code());
		return ExitCode::initialization;
	}
	const auto initialSize = item.Size();
	if (initialSize.Width <= 0 || initialSize.Height <= 0) {
		std::wcerr << L"MCXRInput capture probe: the selected window has no capturable area.\n";
		return ExitCode::captureFailed;
	}

	auto signals = std::make_shared<CaptureSignals>();
	const auto pixelFormat = directx::DirectXPixelFormat::B8G8R8A8UIntNormalized;
	capture::Direct3D11CaptureFramePool framePool{nullptr};
	capture::GraphicsCaptureSession session{nullptr};
	winrt::Windows::Graphics::SizeInt32 poolSize = initialSize;
	try {
		framePool = capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
				d3d.winrtDevice, pixelFormat, 2, poolSize);
		session = framePool.CreateCaptureSession(item);
		session.IsCursorCaptureEnabled(true);
	} catch (const winrt::hresult_error& error) {
		printHresult(L"creating the capture frame pool", error.code());
		return ExitCode::initialization;
	}

	const auto frameToken = framePool.FrameArrived([signals](auto&&, auto&&) {
		{
			std::scoped_lock lock{signals->mutex};
			signals->frameArrived = true;
		}
		signals->condition.notify_one();
	});
	const auto closedToken = item.Closed([signals](auto&&, auto&&) {
		signals->windowClosed.store(true);
		signals->condition.notify_one();
	});

	try {
		session.StartCapture();
	} catch (const winrt::hresult_error& error) {
		framePool.FrameArrived(frameToken);
		item.Closed(closedToken);
		printHresult(L"starting window capture", error.code());
		return ExitCode::initialization;
	}

	std::wcout << L"Capturing for " << options.seconds << L" second(s). Press Ctrl+C to finish early.\n";
	CaptureStats stats;
	ComPtr<ID3D11Texture2D> latestTexture;
	winrt::Windows::Graphics::SizeInt32 latestSize{};
	bool latestStereoValid = false;
	bool minimized = false;
	bool oddWidthObserved = false;
	bool fatalCaptureError = false;
	const auto started = std::chrono::steady_clock::now();
	const auto deadline = started + std::chrono::seconds{options.seconds};

	while (!stopRequested.load() && !signals->windowClosed.load()
			&& std::chrono::steady_clock::now() < deadline) {
		{
			std::unique_lock lock{signals->mutex};
			const auto nextWake = std::min(
					deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds{100});
			signals->condition.wait_until(lock, nextWake, [&] {
				return signals->frameArrived || signals->windowClosed.load() || stopRequested.load();
			});
			signals->frameArrived = false;
		}

		const bool nowMinimized = IsIconic(selected.window) != FALSE;
		bool justRestored = false;
		if (nowMinimized != minimized) {
			minimized = nowMinimized;
			if (minimized) {
				std::wcout << L"Capture paused while the window is minimized; stale frames were discarded.\n";
				latestTexture.Reset();
				latestStereoValid = false;
			} else {
				std::wcout << L"Window restored; waiting for a fresh frame.\n";
				justRestored = true;
			}
		}
		if (minimized || justRestored) {
			try {
				for (;;) {
					auto stale = framePool.TryGetNextFrame();
					if (!stale) {
						break;
					}
					++stats.receivedFrames;
					++stats.discardedFrames;
					stale.Close();
				}
			} catch (const winrt::hresult_error& error) {
				if (error.code() != RO_E_CLOSED) {
					printHresult(L"discarding minimized-window frames", error.code());
					fatalCaptureError = true;
					break;
				}
			}
			continue;
		}
		if (signals->windowClosed.load()) {
			continue;
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
			if (error.code() != RO_E_CLOSED) {
				printHresult(L"draining captured frames", error.code());
				fatalCaptureError = true;
			}
			break;
		}
		if (!newest) {
			continue;
		}

		const auto frameSize = newest.ContentSize();
		if (frameSize.Width <= 0 || frameSize.Height <= 0) {
			newest.Close();
			++stats.discardedFrames;
			continue;
		}
		if (frameSize.Width != poolSize.Width || frameSize.Height != poolSize.Height) {
			newest.Close();
			++stats.discardedFrames;
			latestTexture.Reset();
			latestStereoValid = false;
			try {
				framePool.Recreate(d3d.winrtDevice, pixelFormat, 2, frameSize);
				poolSize = frameSize;
				++stats.resizes;
				std::wcout << L"Capture resized to " << poolSize.Width << L'x' << poolSize.Height
						   << L"; waiting for an exact-sized frame.\n";
			} catch (const winrt::hresult_error& error) {
				printHresult(L"resizing the capture frame pool", error.code());
				fatalCaptureError = true;
				break;
			}
			continue;
		}

		if ((frameSize.Width & 1) != 0) {
			if (!oddWidthObserved) {
				std::wcerr << L"MCXRInput capture probe: width " << frameSize.Width
						   << L" is odd and cannot be split into equal half-SBS eyes.\n";
			}
			oddWidthObserved = true;
			latestTexture.Reset();
			latestStereoValid = false;
			newest.Close();
			++stats.discardedFrames;
			continue;
		}

		try {
			if (!copyFrameTexture(newest, d3d.device.Get(), d3d.context.Get(), latestTexture)) {
				newest.Close();
				++stats.discardedFrames;
				fatalCaptureError = true;
				break;
			}
		} catch (const winrt::hresult_error& error) {
			newest.Close();
			printHresult(L"copying the captured frame", error.code());
			fatalCaptureError = true;
			break;
		}
		newest.Close();
		latestStereoValid = true;
		if (latestSize.Width != frameSize.Width || latestSize.Height != frameSize.Height) {
			latestSize = frameSize;
			printStereoDimensions(latestSize);
		}
		++stats.usableFrames;
		const auto now = std::chrono::steady_clock::now();
		if (!stats.firstUsableFrame) {
			stats.firstUsableFrame = now;
		}
		stats.lastUsableFrame = now;
	}

	framePool.FrameArrived(frameToken);
	item.Closed(closedToken);
	session.Close();
	framePool.Close();

	const auto finished = std::chrono::steady_clock::now();
	const double elapsedSeconds = std::chrono::duration<double>(finished - started).count();
	const double averageFps = elapsedSeconds > 0.0
			? static_cast<double>(stats.usableFrames) / elapsedSeconds
			: 0.0;
	std::wcout << std::fixed << std::setprecision(1)
			   << L"Capture summary: elapsed=" << elapsedSeconds << L"s"
			   << L" received=" << stats.receivedFrames
			   << L" usable=" << stats.usableFrames
			   << L" discarded=" << stats.discardedFrames
			   << L" resizes=" << stats.resizes
			   << L" average=" << averageFps << L" fps\n";

	if (signals->windowClosed.load() || !IsWindow(selected.window)) {
		std::wcerr << L"MCXRInput capture probe: the selected window closed during capture.\n";
		return ExitCode::windowSelection;
	}
	if (fatalCaptureError) {
		return ExitCode::captureFailed;
	}
	if (IsIconic(selected.window)) {
		std::wcerr << L"MCXRInput capture probe: the selected window is minimized; no stale snapshot was saved.\n";
		return ExitCode::captureFailed;
	}
	if (!latestStereoValid || !latestTexture || !stats.lastUsableFrame) {
		return oddWidthObserved ? ExitCode::invalidStereoFrame : ExitCode::captureFailed;
	}
	if (finished - *stats.lastUsableFrame > maximumSnapshotAge) {
		std::wcerr << L"MCXRInput capture probe: the newest frame is stale; no snapshot was saved.\n";
		return ExitCode::captureFailed;
	}

	const std::filesystem::path absoluteSnapshot = std::filesystem::absolute(options.snapshot);
	if (!saveTextureToPng(
			d3d.device.Get(), d3d.context.Get(), latestTexture.Get(), absoluteSnapshot)) {
		return ExitCode::snapshotFailed;
	}
	std::wcout << L"Saved newest half-SBS frame to " << absoluteSnapshot.wstring() << L'\n';
	return ExitCode::success;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
	std::wcout << L"MCXRInput Minecraft window capture probe\n"
			   << L"Window capture diagnostic only; OpenXR and gameplay input are not started.\n";

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
			windows = filterByExecutable(windows, *options.executable);
		}
		std::wcout << L"Matching visible windows: " << windows.size() << L'\n';
		for (const WindowCandidate& candidate : windows) {
			printWindow(candidate);
		}
		return static_cast<int>(ExitCode::success);
	}

	const auto selected = selectWindow(options);
	if (!selected) {
		return static_cast<int>(ExitCode::windowSelection);
	}
	std::wcout << L"Selected window:\n";
	printWindow(*selected);

	SetConsoleCtrlHandler(handleConsoleControl, TRUE);
	try {
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
		return static_cast<int>(runCapture(options, *selected));
	} catch (const winrt::hresult_error& error) {
		printHresult(L"initializing Windows capture", error.code());
		return static_cast<int>(ExitCode::initialization);
	} catch (const std::exception& error) {
		std::cerr << "MCXRInput capture probe: " << error.what() << '\n';
		return static_cast<int>(ExitCode::initialization);
	}
}
