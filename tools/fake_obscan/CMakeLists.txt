# Test-only stand-in for the vendor's liborbbec_scan — see fake_obscan.cpp.
# Built as a plain shared library and passed to live_capture_test as its
# library path; never installed, never loaded by anything else.
add_library(fake_obscan SHARED fake_obscan.cpp)
target_include_directories(fake_obscan PRIVATE ${PROJECT_SOURCE_DIR}/engine/include)
find_package(Threads REQUIRED)
target_link_libraries(fake_obscan PRIVATE Threads::Threads)
set_target_properties(fake_obscan PROPERTIES CXX_VISIBILITY_PRESET hidden)
