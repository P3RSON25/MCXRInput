#include <openxr/openxr.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

enum class ExitCode : int {
	success = 0,
	runtimeUnavailable = 10,
	runtimeNotReady = 11,
	instanceCreationFailed = 20,
	headsetUnavailable = 30,
	systemQueryFailed = 31,
};

std::string_view resultName(XrResult result) {
	switch (result) {
	case XR_SUCCESS:
		return "XR_SUCCESS";
	case XR_ERROR_RUNTIME_UNAVAILABLE:
		return "XR_ERROR_RUNTIME_UNAVAILABLE";
	case XR_ERROR_RUNTIME_FAILURE:
		return "XR_ERROR_RUNTIME_FAILURE";
	case XR_ERROR_INITIALIZATION_FAILED:
		return "XR_ERROR_INITIALIZATION_FAILED";
	case XR_ERROR_API_VERSION_UNSUPPORTED:
		return "XR_ERROR_API_VERSION_UNSUPPORTED";
	case XR_ERROR_FORM_FACTOR_UNAVAILABLE:
		return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
	case XR_ERROR_INSTANCE_LOST:
		return "XR_ERROR_INSTANCE_LOST";
	default:
		return "unknown OpenXR result";
	}
}

void printFailure(std::string_view operation, XrResult result) {
	std::cerr << "MCXRInput OpenXR probe: " << operation << " failed ("
			  << resultName(result) << ", " << result << ").\n";
}

XrResult enumerateLayers(std::vector<XrApiLayerProperties>& layers) {
	std::uint32_t count = 0;
	XrResult result = xrEnumerateApiLayerProperties(0, &count, nullptr);
	if (XR_FAILED(result)) {
		return result;
	}

	layers.assign(count, XrApiLayerProperties{XR_TYPE_API_LAYER_PROPERTIES});
	return xrEnumerateApiLayerProperties(count, &count, layers.data());
}

XrResult enumerateExtensions(std::vector<XrExtensionProperties>& extensions) {
	std::uint32_t count = 0;
	XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
	if (XR_FAILED(result)) {
		return result;
	}

	extensions.assign(count, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
	return xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data());
}

void printVersion(XrVersion version) {
	std::cout << XR_VERSION_MAJOR(version) << '.'
			  << XR_VERSION_MINOR(version) << '.'
			  << XR_VERSION_PATCH(version);
}

} // namespace

int main() {
	std::cout << "MCXRInput OpenXR runtime probe\n";

	std::vector<XrApiLayerProperties> layers;
	XrResult result = enumerateLayers(layers);
	if (XR_FAILED(result)) {
		printFailure("loading the active OpenXR runtime", result);
		std::cerr << "Check that SteamVR is installed, running, and selected as the OpenXR runtime.\n";
		return static_cast<int>(ExitCode::runtimeUnavailable);
	}

	std::vector<XrExtensionProperties> extensions;
	result = enumerateExtensions(extensions);
	if (XR_FAILED(result)) {
		printFailure("enumerating OpenXR extensions", result);
		return static_cast<int>(ExitCode::runtimeUnavailable);
	}

	std::cout << "API layers (" << layers.size() << "):\n";
	for (const auto& layer : layers) {
		std::cout << "  " << layer.layerName << " - " << layer.description << '\n';
	}
	std::cout << "Instance extensions (" << extensions.size() << "):\n";
	for (const auto& extension : extensions) {
		std::cout << "  " << extension.extensionName << " v" << extension.extensionVersion << '\n';
	}

	XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	constexpr std::string_view applicationName = "MCXRInput";
	std::copy(applicationName.begin(), applicationName.end(), createInfo.applicationInfo.applicationName);
	createInfo.applicationInfo.applicationVersion = 1;
	constexpr std::string_view engineName = "MCXRInput native bridge";
	std::copy(engineName.begin(), engineName.end(), createInfo.applicationInfo.engineName);
	createInfo.applicationInfo.engineVersion = 1;
	// Use the 1.0 baseline so the probe works with runtimes that have not yet
	// adopted OpenXR 1.1. Newer headers remain source-compatible with this API.
	createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);

	XrInstance instance = XR_NULL_HANDLE;
	result = xrCreateInstance(&createInfo, &instance);
	if (XR_FAILED(result)) {
		printFailure("creating an OpenXR instance", result);
		if (result == XR_ERROR_RUNTIME_FAILURE || result == XR_ERROR_INITIALIZATION_FAILED) {
			std::cerr << "SteamVR is selected but not ready. Start SteamVR and confirm it sees the headset.\n";
			return static_cast<int>(ExitCode::runtimeNotReady);
		}
		return static_cast<int>(ExitCode::instanceCreationFailed);
	}

	auto destroyInstance = [&instance]() {
		if (instance != XR_NULL_HANDLE) {
			xrDestroyInstance(instance);
			instance = XR_NULL_HANDLE;
		}
	};

	XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
	result = xrGetInstanceProperties(instance, &instanceProperties);
	if (XR_FAILED(result)) {
		printFailure("querying runtime properties", result);
		destroyInstance();
		return static_cast<int>(ExitCode::systemQueryFailed);
	}

	std::cout << "Runtime: " << instanceProperties.runtimeName << ' ';
	printVersion(instanceProperties.runtimeVersion);
	std::cout << '\n';

	XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrSystemId systemId = XR_NULL_SYSTEM_ID;
	result = xrGetSystem(instance, &systemInfo, &systemId);
	if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
		std::cerr << "MCXRInput OpenXR probe: no head-mounted display is currently available.\n"
				  << "Connect Quest Link/Air Link and confirm SteamVR can see the headset.\n";
		destroyInstance();
		return static_cast<int>(ExitCode::headsetUnavailable);
	}
	if (XR_FAILED(result)) {
		printFailure("requesting the head-mounted display", result);
		destroyInstance();
		return static_cast<int>(ExitCode::systemQueryFailed);
	}

	XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
	result = xrGetSystemProperties(instance, systemId, &properties);
	if (XR_FAILED(result)) {
		printFailure("querying headset properties", result);
		destroyInstance();
		return static_cast<int>(ExitCode::systemQueryFailed);
	}

	std::cout << "Headset: " << properties.systemName << '\n'
			  << "Vendor ID: " << properties.vendorId << '\n'
			  << "Orientation tracking: "
			  << (properties.trackingProperties.orientationTracking == XR_TRUE ? "supported" : "not supported") << '\n'
			  << "Position tracking: "
			  << (properties.trackingProperties.positionTracking == XR_TRUE ? "supported" : "not supported") << '\n'
			  << "Maximum swapchain size: "
			  << properties.graphicsProperties.maxSwapchainImageWidth << 'x'
			  << properties.graphicsProperties.maxSwapchainImageHeight << '\n'
			  << "Maximum layer count: " << properties.graphicsProperties.maxLayerCount << '\n';

	destroyInstance();
	std::cout << "OpenXR runtime and headset discovery succeeded.\n";
	return static_cast<int>(ExitCode::success);
}
