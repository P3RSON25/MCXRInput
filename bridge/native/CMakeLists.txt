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

function(configure_mcxr_native_target target_name)
	target_compile_features(${target_name} PRIVATE cxx_std_20)
	target_link_libraries(${target_name} PRIVATE OpenXR::openxr_loader)

	if(MSVC)
		target_compile_options(${target_name} PRIVATE /W4 /permissive- /EHsc)
	endif()
endfunction()

add_executable(MCXRInputOpenXRProbe src/main.cpp)
configure_mcxr_native_target(MCXRInputOpenXRProbe)

add_executable(MCXRInputOpenXRPoseProbe src/pose_probe.cpp)
configure_mcxr_native_target(MCXRInputOpenXRPoseProbe)

add_executable(MCXRInputOpenXRD3D11InputProbe src/d3d11_input_probe.cpp)
configure_mcxr_native_target(MCXRInputOpenXRD3D11InputProbe)
target_link_libraries(MCXRInputOpenXRD3D11InputProbe PRIVATE d3d11 dxgi)

add_executable(MCXRInputOpenXRBridge src/openxr_bridge.cpp)
configure_mcxr_native_target(MCXRInputOpenXRBridge)
target_link_libraries(MCXRInputOpenXRBridge PRIVATE d3d11 dxgi ws2_32)
