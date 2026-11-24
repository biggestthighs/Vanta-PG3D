cmake_minimum_required(VERSION 3.20)
project(Vanta VERSION 0.1.20 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "x64 only")
endif ()

include(FetchContent)

FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui GIT_TAG v1.91.5-docking)
FetchContent_GetProperties(imgui)
if (NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif ()
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
# must match the DLLs' static CRT or imgui.lib fails to link (__imp_strncpy etc.)
set_property(TARGET imgui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

FetchContent_Declare(minhook GIT_REPOSITORY https://github.com/TsudaKageyu/minhook GIT_TAG v1.3.3)
FetchContent_MakeAvailable(minhook)
# v1.3.3 has no CMakeLists, so build it here (x64 only)
add_library(minhook STATIC
  ${minhook_SOURCE_DIR}/src/buffer.c
  ${minhook_SOURCE_DIR}/src/hook.c
  ${minhook_SOURCE_DIR}/src/trampoline.c
  ${minhook_SOURCE_DIR}/src/hde/hde64.c)
target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)
set_property(TARGET minhook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json GIT_TAG v3.11.3)
FetchContent_MakeAvailable(json)
# sources include "json.hpp" directly
target_include_directories(nlohmann_json INTERFACE ${json_SOURCE_DIR}/single_include/nlohmann)

set(CORE_SRC
  src/core/common.cpp
  src/core/config.cpp
  src/sdk/il2cpp.cpp
  src/sdk/offsets.cpp
  src/sdk/game.cpp)

add_library(pg3d_core SHARED
  ${CORE_SRC}
  src/core/render.cpp
  src/core/menu.cpp
  src/core/atmosphere.cpp
  src/features/aimbot.cpp
  src/features/triggerbot.cpp
  src/features/visuals.cpp
  src/features/world.cpp
  src/features/misc.cpp
  src/features/hitsound.cpp
  src/features/farm.cpp
  src/features/custommodel.cpp
  src/features/probe.cpp
  src/features/rage.cpp
  src/main.cpp)
target_link_libraries(pg3d_core PRIVATE imgui minhook nlohmann_json::nlohmann_json d3d11 dxgi d3dcompiler)
target_compile_definitions(pg3d_core PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX CORE_BUILD)
set_target_properties(pg3d_core PROPERTIES PREFIX "" OUTPUT_NAME "pg3d_core")
add_custom_command(TARGET pg3d_core POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets/models" "$<TARGET_FILE_DIR:pg3d_core>/models"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets/sounds" "$<TARGET_FILE_DIR:pg3d_core>/sounds")

# Optional native regression check; exercises the loader's actual GC handle paths.
add_executable(pg3d_model_handle_test EXCLUDE_FROM_ALL tools/test_custommodel_handles.cpp)
target_include_directories(pg3d_model_handle_test PRIVATE ${imgui_SOURCE_DIR})

add_executable(pg3d_farm_test EXCLUDE_FROM_ALL tools/test_farm.cpp)

add_library(pg3d_stats_probe SHARED EXCLUDE_FROM_ALL tools/stats_probe.cpp src/core/common.cpp src/sdk/il2cpp.cpp)

add_library(pg3d_dumper SHARED ${CORE_SRC} dumper/dumper.cpp)
target_link_libraries(pg3d_dumper PRIVATE imgui minhook nlohmann_json::nlohmann_json d3d11 dxgi)
target_compile_definitions(pg3d_dumper PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
set_target_properties(pg3d_dumper PROPERTIES PREFIX "" OUTPUT_NAME "pg3d_dumper")

add_executable(pg3d_loader loader/main.cpp)
target_compile_definitions(pg3d_loader PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
set_target_properties(pg3d_loader PROPERTIES OUTPUT_NAME "pg3d_loader")

if (MSVC)
  foreach (t pg3d_core pg3d_dumper pg3d_loader)
    target_compile_options(${t} PRIVATE /W3 /permissive- /MP)
  endforeach ()
  foreach (t pg3d_core pg3d_dumper)
    set_property(TARGET ${t} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endforeach ()
endif ()

set_target_properties(pg3d_core pg3d_dumper PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Release
  RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Debug)

# Portable release launcher; /MT avoids a separate Visual C++ runtime install.
add_executable(Vanta WIN32 loader/vanta.cpp)
target_compile_definitions(Vanta PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 VANTA_VERSION=L\"${PROJECT_VERSION}\")
target_link_libraries(Vanta PRIVATE user32 gdi32 shell32)
set_property(TARGET Vanta PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_target_properties(Vanta PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
enable_testing()
add_test(NAME farm_regression COMMAND pg3d_farm_test)
add_test(NAME model_handles COMMAND pg3d_model_handle_test)

add_executable(pg3d_config_test EXCLUDE_FROM_ALL tools/test_config.cpp src/core/config.cpp)
target_include_directories(pg3d_config_test PRIVATE ${imgui_SOURCE_DIR})
target_link_libraries(pg3d_config_test PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME config_profiles COMMAND pg3d_config_test)

# Optional read-only weapon timing metadata diagnostic.
add_library(pg3d_rapid_probe SHARED EXCLUDE_FROM_ALL tools/rapid_probe.cpp src/core/common.cpp src/sdk/il2cpp.cpp)
