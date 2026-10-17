cmake_minimum_required(VERSION 3.20)
project(OpenScanner LANGUAGES CXX C)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

add_subdirectory(third_party)

add_subdirectory(mesh)
add_subdirectory(engine)
add_subdirectory(tools/smoke_test)
add_subdirectory(tools/scan_server)
add_subdirectory(tools/mesh_tools_smoke_test)
add_subdirectory(tools/live_capture_test)
add_subdirectory(tools/capture_pipeline_smoke_test)
add_subdirectory(tests)
