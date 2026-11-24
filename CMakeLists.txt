# AMD FSR 4 as a Hailstone upscaler add-on (README.md).
cmake_minimum_required(VERSION 3.24)
project(hailstone-fsr4 VERSION 0.2.0 LANGUAGES CXX)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(CTest)
include(ExternalProject)
if(POLICY CMP0225)
  cmake_policy(SET CMP0225 NEW)
endif()

set(HAILSTONE_CLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../hailstone-client" CACHE PATH
    "a Hailstone client checkout: sdk/hailstone_upscaler.h is taken from it")
set(FSR4_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime" CACHE PATH
    "the pinned ebuwiz/fsr4-vk checkout")
# Verified FSR 4.1.2 upscaler assets prepared by tools/prepare_assets.py.
set(FSR4_ASSET_ROOT "${FSR4_RUNTIME_DIR}/assets/pack-unified")
set(FSR4_BUILD_JOBS 4 CACHE STRING "parallel runtime build jobs")

if(NOT EXISTS "${HAILSTONE_CLIENT_DIR}/sdk/hailstone_upscaler.h")
  message(FATAL_ERROR "no sdk/hailstone_upscaler.h in ${HAILSTONE_CLIENT_DIR} (set HAILSTONE_CLIENT_DIR)")
endif()
if(NOT EXISTS "${FSR4_ASSET_ROOT}/manifest.json")
  message(FATAL_ERROR "no FSR 4 assets in ${FSR4_ASSET_ROOT}: run tools/prepare_assets.py --dll /path/to/upscaler-dx12.dll")
endif()
if(NOT EXISTS "${FSR4_RUNTIME_DIR}/third_party/Vulkan-Headers/include/vulkan/vulkan.h")
  message(FATAL_ERROR "runtime submodules are missing: run git submodule update --init --recursive")
endif()

# Build the runtime as its own CMake project. Its build files use CMAKE_SOURCE_DIR
# and expect a standalone tree, so add_subdirectory() would point them here.
# Only the upscaler target is built; the separate Frame Generation provider is
# never produced or copied into the package.
set(FSR4_RUNTIME_BUILD "${CMAKE_BINARY_DIR}/runtime")
ExternalProject_Add(fsr4_runtime
  SOURCE_DIR "${FSR4_RUNTIME_DIR}"
  BINARY_DIR "${FSR4_RUNTIME_BUILD}"
  CMAKE_GENERATOR "${CMAKE_GENERATOR}"
  CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=Release
    -DFSR4_BUILD_UPSCALER=ON
    -DFSR4_CAPABILITY_PROBE=ON
    -DFFG4_EMBED_ASSETS=OFF
    "-DFSR4_ASSET_ROOT:PATH=${FSR4_ASSET_ROOT}"
  BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target amd_fidelityfx_upscaler_vk
  INSTALL_COMMAND ""
  EXCLUDE_FROM_ALL YES)

add_library(fsr4 MODULE src/fsr4_upscaler.cpp)
set_target_properties(fsr4 PROPERTIES PREFIX "" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_VISIBILITY_PRESET hidden)
target_include_directories(fsr4 PRIVATE
  "${HAILSTONE_CLIENT_DIR}/sdk"
  "${FSR4_RUNTIME_DIR}/third_party/fidelityfx-sdk/Kits/FidelityFX/api/include"
  "${FSR4_RUNTIME_DIR}/third_party/fidelityfx-sdk/Kits/FidelityFX/upscalers/include"
  "${FSR4_RUNTIME_DIR}/third_party/Vulkan-Headers/include")
target_link_libraries(fsr4 PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(fsr4 fsr4_runtime)
if(MSVC)
  target_compile_options(fsr4 PRIVATE /W4 /permissive-)
  target_compile_definitions(fsr4 PRIVATE NOMINMAX)
else()
  target_compile_options(fsr4 PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

set(FSR4_RUNTIME_LIBRARY "${FSR4_RUNTIME_BUILD}/amd_fidelityfx_upscaler_vk.so")
set(FSR4_DIST "${CMAKE_BINARY_DIR}/dist")
add_custom_command(TARGET fsr4 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${FSR4_DIST}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${FSR4_DIST}/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:fsr4>" "${FSR4_DIST}/fsr4.so"
  COMMAND ${CMAKE_COMMAND} -E copy "${FSR4_RUNTIME_LIBRARY}" "${FSR4_DIST}/amd_fidelityfx_upscaler_vk.so"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${FSR4_DIST}/licenses/adapter-MIT.txt"
  COMMAND ${CMAKE_COMMAND} -E copy "${HAILSTONE_CLIENT_DIR}/sdk/LICENSE" "${FSR4_DIST}/licenses/hailstone-SDK-MIT.txt"
  COMMAND ${CMAKE_COMMAND} -E copy "${FSR4_RUNTIME_DIR}/LICENSE" "${FSR4_DIST}/licenses/runtime-GPL-3.0.txt"
  COMMAND ${CMAKE_COMMAND} -E copy "${FSR4_RUNTIME_DIR}/THIRD_PARTY_NOTICES.md" "${FSR4_DIST}/THIRD_PARTY_NOTICES.md"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${FSR4_RUNTIME_DIR}/LICENSES" "${FSR4_DIST}/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${FSR4_DIST}/README.md"
  VERBATIM)

add_custom_target(fsr4-runtime-tests
  COMMAND "${CMAKE_COMMAND}" --build "${FSR4_RUNTIME_BUILD}" --parallel "${FSR4_BUILD_JOBS}"
          --target upscaler-debugcheck-test upscaler-query-test cap-probe descriptor-smoke
  DEPENDS fsr4_runtime
  VERBATIM)

add_custom_target(fsr4-check
  COMMAND "${Python3_EXECUTABLE}" "${FSR4_RUNTIME_DIR}/tools/validate/shaders.py" "${FSR4_ASSET_ROOT}"
  COMMAND "${Python3_EXECUTABLE}" "${FSR4_RUNTIME_DIR}/tools/validate/elf-assets.py"
          "${FSR4_RUNTIME_LIBRARY}" "${FSR4_ASSET_ROOT}"
          "${FSR4_RUNTIME_BUILD}/generated/elf-asset-report.json"
          --storage-index "${FSR4_RUNTIME_BUILD}/generated/embedded-storage.json"
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${FSR4_RUNTIME_BUILD}" --output-on-failure
          -R "upscaler-debugcheck|cap-probe-synthetic-gates|descriptor-smoke-transports-byte-identical|upscaler-query-math"
  DEPENDS fsr4 fsr4-runtime-tests
  WORKING_DIRECTORY "${FSR4_RUNTIME_DIR}"
  VERBATIM)
