cmake_minimum_required(VERSION 3.22)
project(godot_luau LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Dependencies: built once, never touched by edits to src/
set(GODOTCPP_TARGET editor CACHE STRING "")
set(GODOTCPP_BUILD_PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/build_profile.json" CACHE PATH "")
# On MSVC, godot-cpp disables exceptions with a public _HAS_EXCEPTIONS=0, which
# changes std::exception's layout in our sources but not in Luau's libraries:
# Luau's ParseError (a std::exception) then reads wrongly and crashes. Keep
# them alike (docs/adr/0054). Elsewhere it's a private -fno-exceptions.
if(MSVC)
    set(GODOTCPP_DISABLE_EXCEPTIONS OFF CACHE BOOL "")
endif()
add_subdirectory(lib/godot-cpp EXCLUDE_FROM_ALL)

set(LUAU_BUILD_CLI OFF CACHE BOOL "")
set(LUAU_BUILD_TESTS OFF CACHE BOOL "")
# Vector2 and Vector3 are separate Luau types (the Godot fork, lib/luau/GODOT.md)
set(LUAU_VECTOR_KINDS ON CACHE BOOL "")
add_subdirectory(lib/luau EXCLUDE_FROM_ALL)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Engine method hashes and signatures, generated as data from extension_api.json
set(API_JSON "${CMAKE_CURRENT_SOURCE_DIR}/lib/godot-cpp/gdextension/extension_api.json")
set(API_DATA "${CMAKE_CURRENT_BINARY_DIR}/gen/api_data.inc")
add_custom_command(
    OUTPUT "${API_DATA}"
        "${CMAKE_CURRENT_BINARY_DIR}/gen/utility_data.inc"
        "${CMAKE_CURRENT_BINARY_DIR}/gen/global_enum_data.inc"
        "${CMAKE_CURRENT_BINARY_DIR}/gen/builtin_data.inc"
        "${CMAKE_CURRENT_BINARY_DIR}/gen/builtin_method_data.inc"
        "${CMAKE_CURRENT_BINARY_DIR}/gen/operator_data.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_api_data.py" "${API_JSON}" "${API_DATA}"
    DEPENDS "${API_JSON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_api_data.py"
)
file(GLOB SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(godot_luau SHARED ${SOURCES} "${API_DATA}")
target_include_directories(godot_luau PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/gen")

# Luau definitions of Godot's API for luau-lsp, next to the library (docs/adr/0048)
set(DEFINITIONS "${CMAKE_CURRENT_SOURCE_DIR}/addons/godot_luau/types/godot.d.luau")
add_custom_command(
    OUTPUT "${DEFINITIONS}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_definitions.py" "${API_JSON}" "${DEFINITIONS}"
    DEPENDS "${API_JSON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_definitions.py"
    COMMENT "Generating godot.d.luau"
)
add_custom_target(godot_luau_definitions ALL DEPENDS "${DEFINITIONS}")
target_link_libraries(godot_luau PRIVATE godot::cpp Luau.VM Luau.Compiler)

# Native code generation for script functions (docs/adr/0009)
option(GODOT_LUAU_CODEGEN "Compile script functions to native code with Luau.CodeGen (off: see docs/adr/0009)" OFF)
if(GODOT_LUAU_CODEGEN)
    target_link_libraries(godot_luau PRIVATE Luau.CodeGen)
    target_compile_definitions(godot_luau PRIVATE GODOT_LUAU_CODEGEN=1)
endif()
# One library per platform in the addon's bin/, named as godot_luau.gdextension
# lists them ($<1:…> keeps multi-config generators from adding a subfolder)
if(WIN32)
    set(GODOT_LUAU_NAME "godot_luau.windows.x86_64")
elseif(APPLE)
    set(GODOT_LUAU_NAME "godot_luau")  # universal with -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        set(GODOT_LUAU_NAME "godot_luau.linux.arm64")
    else()
        set(GODOT_LUAU_NAME "godot_luau.linux.x86_64")
    endif()
endif()
set_target_properties(godot_luau PROPERTIES
    OUTPUT_NAME "${GODOT_LUAU_NAME}"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/addons/godot_luau/bin>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/addons/godot_luau/bin>"
)
