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
add_subdirectory(lib/godot-cpp EXCLUDE_FROM_ALL)

set(LUAU_BUILD_CLI OFF CACHE BOOL "")
set(LUAU_BUILD_TESTS OFF CACHE BOOL "")
add_subdirectory(lib/luau EXCLUDE_FROM_ALL)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Engine method hashes and signatures, generated as data from extension_api.json
set(API_JSON "${CMAKE_CURRENT_SOURCE_DIR}/lib/godot-cpp/gdextension/extension_api.json")
set(API_DATA "${CMAKE_CURRENT_BINARY_DIR}/gen/api_data.inc")
add_custom_command(
    OUTPUT "${API_DATA}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_api_data.py" "${API_JSON}" "${API_DATA}"
    DEPENDS "${API_JSON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_api_data.py"
)
file(GLOB SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(godot_luau SHARED ${SOURCES} "${API_DATA}")
target_include_directories(godot_luau PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/gen")
target_link_libraries(godot_luau PRIVATE godot::cpp Luau.VM Luau.Compiler)
set_target_properties(godot_luau PROPERTIES
    OUTPUT_NAME "godot_luau"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/addons/godot_luau/bin"
)
