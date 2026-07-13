cmake_minimum_required(VERSION 3.25)

project(MCXRInputOpenXRProbe VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
	message(FATAL_ERROR "The MCXRInput OpenXR bridge currently supports Windows only.")
endif()

include(FetchContent)

# Pin the official Khronos loader so builds do not depend on SteamVR's private files.
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_LOADER ON CACHE BOOL "" FORCE)
FetchContent_Declare(
	OpenXR
	GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
	GIT_TAG release-1.1.59
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(OpenXR)

include(CTest)

function(configure_mcxr_cpp_target target_name)
	target_compile_features(${target_name} PRIVATE cxx_std_20)

	if(MSVC)
		target_compile_options(${target_name} PRIVATE /W4 /permissive- /EHsc)
	endif()
endfunction()

function(configure_mcxr_native_target target_name)
	configure_mcxr_cpp_target(${target_name})
	target_link_libraries(${target_name} PRIVATE OpenXR::openxr_loader)
endfunction()

# Dependency-free transform used to keep the virtual screen gravity-level while
# following the headset's physical gaze. Keeping it separate makes the comfort
# behavior testable without an OpenXR runtime or headset.
add_library(mcxr_screen_pose_math STATIC
	src/screen_pose_math.cpp
)
target_include_directories(mcxr_screen_pose_math PUBLIC include)
configure_mcxr_cpp_target(mcxr_screen_pose_math)

# Dependency-free stereo-pose, projection-FOV, and source-crop calculations.
# Keeping OpenXR types out makes immersive presentation behavior unit-testable.
add_library(mcxr_projection_math STATIC
	src/projection_math.cpp
)
target_include_directories(mcxr_projection_math PUBLIC include)
configure_mcxr_cpp_target(mcxr_projection_math)
target_link_libraries(mcxr_projection_math PUBLIC mcxr_screen_pose_math)

if(BUILD_TESTING)
	add_executable(MCXRInputScreenPoseMathTests
		tests/screen_pose_math_tests.cpp
	)
	configure_mcxr_cpp_target(MCXRInputScreenPoseMathTests)
	target_link_libraries(MCXRInputScreenPoseMathTests PRIVATE mcxr_screen_pose_math)
	add_test(NAME MCXRInputScreenPoseMath COMMAND MCXRInputScreenPoseMathTests)

	add_executable(MCXRInputProjectionMathTests
		tests/projection_math_tests.cpp
	)
	configure_mcxr_cpp_target(MCXRInputProjectionMathTests)
	target_link_libraries(MCXRInputProjectionMathTests PRIVATE mcxr_projection_math)
	add_test(NAME MCXRInputProjectionMath COMMAND MCXRInputProjectionMathTests)
endif()

# Shared by the production bridge and focused D3D11 display diagnostics. The
# passive runtime/pose/input probes remain independent of this lifecycle layer.
add_library(mcxr_openxr_d3d11 STATIC
	src/openxr_d3d11.cpp
)
target_include_directories(mcxr_openxr_d3d11 PUBLIC include)
configure_mcxr_native_target(mcxr_openxr_d3d11)
target_link_libraries(mcxr_openxr_d3d11 PRIVATE d3d11 dxgi)

# Protocol-v2 serialization and the production fail-closed publication gate.
# Keeping it free of Winsock makes neutralization behavior directly testable.
add_library(mcxr_bridge_output STATIC
	src/bridge_output.cpp
)
target_include_directories(mcxr_bridge_output PUBLIC include)
configure_mcxr_native_target(mcxr_bridge_output)

if(BUILD_TESTING)
	add_executable(MCXRInputBridgeOutputTests
		tests/bridge_output_tests.cpp
	)
	configure_mcxr_native_target(MCXRInputBridgeOutputTests)
	target_link_libraries(MCXRInputBridgeOutputTests PRIVATE mcxr_bridge_output)
	add_test(NAME MCXRInputBridgeOutput COMMAND MCXRInputBridgeOutputTests)
endif()

# Shared adapter for the exact roll-free/canted-eye projection calibration
# proven by the bounded hardware probe. It owns no OpenXR or D3D11 resources;
# callers keep their instance, session, spaces, and swapchains.
add_library(mcxr_immersive_projection STATIC
	src/immersive_projection.cpp
)
target_include_directories(mcxr_immersive_projection PUBLIC include)
configure_mcxr_native_target(mcxr_immersive_projection)
target_link_libraries(mcxr_immersive_projection PUBLIC
	mcxr_openxr_d3d11
	mcxr_projection_math
)

if(BUILD_TESTING)
	add_executable(MCXRInputImmersiveProjectionTests
		tests/immersive_projection_tests.cpp
	)
	configure_mcxr_native_target(MCXRInputImmersiveProjectionTests)
	target_link_libraries(MCXRInputImmersiveProjectionTests PRIVATE
		mcxr_immersive_projection
	)
	add_test(NAME MCXRInputImmersiveProjection COMMAND MCXRInputImmersiveProjectionTests)
endif()

# GPU-only anamorphic half-SBS decoder shared by the capture/display probes.
# It samples one captured BGRA texture into two caller-owned OpenXR images.
add_library(mcxr_half_sbs_renderer STATIC
	src/half_sbs_renderer.cpp
)
target_include_directories(mcxr_half_sbs_renderer PUBLIC include)
configure_mcxr_native_target(mcxr_half_sbs_renderer)
target_link_libraries(mcxr_half_sbs_renderer PRIVATE
	mcxr_openxr_d3d11
	mcxr_projection_math
	d3d11
	d3dcompiler
)

# Reusable Windows Graphics Capture source. It wraps a caller-owned D3D11
# device, so capture and OpenXR rendering stay on the runtime-required adapter.
add_library(mcxr_window_capture STATIC
	src/window_capture.cpp
)
target_include_directories(mcxr_window_capture PUBLIC include)
configure_mcxr_cpp_target(mcxr_window_capture)
target_link_libraries(mcxr_window_capture PRIVATE
	d3d11
	dxgi
	dwmapi
	ole32
	user32
	windowsapp
)

if(BUILD_TESTING)
	add_executable(MCXRInputWindowCaptureTests
		tests/window_capture_tests.cpp
	)
	configure_mcxr_cpp_target(MCXRInputWindowCaptureTests)
	target_link_libraries(MCXRInputWindowCaptureTests PRIVATE mcxr_window_capture)
	add_test(NAME MCXRInputWindowCapture COMMAND MCXRInputWindowCaptureTests)
endif()

# Unicode-safe, strict production bridge CLI parsing. Window enumeration and
# OpenXR startup remain outside this option layer.
add_library(mcxr_bridge_options STATIC
	src/bridge_options.cpp
)
target_include_directories(mcxr_bridge_options PUBLIC include)
configure_mcxr_native_target(mcxr_bridge_options)
target_link_libraries(mcxr_bridge_options PUBLIC mcxr_window_capture)

if(BUILD_TESTING)
	add_executable(MCXRInputBridgeOptionsTests
		tests/bridge_options_tests.cpp
	)
	configure_mcxr_native_target(MCXRInputBridgeOptionsTests)
	target_link_libraries(MCXRInputBridgeOptionsTests PRIVATE mcxr_bridge_options)
	add_test(NAME MCXRInputBridgeOptions COMMAND MCXRInputBridgeOptionsTests)
endif()

add_executable(MCXRInputOpenXRProbe src/main.cpp)
configure_mcxr_native_target(MCXRInputOpenXRProbe)

add_executable(MCXRInputOpenXRPoseProbe src/pose_probe.cpp)
configure_mcxr_native_target(MCXRInputOpenXRPoseProbe)

add_executable(MCXRInputOpenXRD3D11InputProbe src/d3d11_input_probe.cpp)
configure_mcxr_native_target(MCXRInputOpenXRD3D11InputProbe)
target_link_libraries(MCXRInputOpenXRD3D11InputProbe PRIVATE d3d11 dxgi)

add_executable(MCXRInputOpenXRBridge src/openxr_bridge.cpp)
configure_mcxr_native_target(MCXRInputOpenXRBridge)
target_link_libraries(MCXRInputOpenXRBridge PRIVATE
	mcxr_bridge_options
	mcxr_bridge_output
	mcxr_half_sbs_renderer
	mcxr_immersive_projection
	mcxr_openxr_d3d11
	mcxr_window_capture
	ws2_32
)

# Synthetic-only compositor diagnostic: two eye-specific quad layers on a
# gravity-level screen. It deliberately has no capture, UDP, or action input.
add_executable(MCXRInputOpenXRStereoScreenProbe src/stereo_screen_probe.cpp)
configure_mcxr_native_target(MCXRInputOpenXRStereoScreenProbe)
target_link_libraries(MCXRInputOpenXRStereoScreenProbe PRIVATE
	mcxr_openxr_d3d11
	mcxr_screen_pose_math
)

# Bounded integration checkpoint: live Windows half-SBS capture rendered on the
# proven eye-specific, gravity-level OpenXR quads. No UDP or action input.
add_executable(MCXRInputOpenXRCaptureScreenProbe src/capture_screen_probe.cpp)
configure_mcxr_native_target(MCXRInputOpenXRCaptureScreenProbe)
target_link_libraries(MCXRInputOpenXRCaptureScreenProbe PRIVATE
	mcxr_openxr_d3d11
	mcxr_screen_pose_math
	mcxr_half_sbs_renderer
	mcxr_window_capture
)

# Bounded full-FOV integration diagnostic. It reuses the proven WGC/GPU decode
# path but submits one core projection layer and deliberately has no UDP/actions.
add_executable(MCXRInputOpenXRImmersiveCaptureProbe src/immersive_capture_probe.cpp)
configure_mcxr_native_target(MCXRInputOpenXRImmersiveCaptureProbe)
target_link_libraries(MCXRInputOpenXRImmersiveCaptureProbe PRIVATE
	mcxr_openxr_d3d11
	mcxr_projection_math
	mcxr_half_sbs_renderer
	mcxr_window_capture
)

# Standalone window-capture diagnostic. It deliberately has no OpenXR or
# gameplay-input dependency, so capture can be proven before headset rendering.
add_executable(MCXRInputCaptureProbe src/capture_probe.cpp)
configure_mcxr_cpp_target(MCXRInputCaptureProbe)
target_link_libraries(MCXRInputCaptureProbe PRIVATE
	d3d11
	dxgi
	dwmapi
	ole32
	user32
	windowsapp
	windowscodecs
)
