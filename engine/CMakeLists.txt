add_library(openscanner_engine STATIC
    src/obscan_loader.cpp
    src/obscan_mesh_bridge.cpp
    src/worker_process.cpp
    src/ws_broadcast_server.cpp
    src/recon/laser_line.cpp
    src/recon/triangulate.cpp
    src/recon/cloud_compare.cpp
)

# Row #200: the laser-line extraction seed. Device-free; the recorded-frame
# pass is opt-in (`--report`), so ctest runs the synthetic ground truth only.
add_executable(laser_line_test src/recon/laser_line_test.cpp)
target_link_libraries(laser_line_test PRIVATE openscanner_engine)
target_include_directories(laser_line_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME laser_line_test COMMAND laser_line_test)

# WO-2: triangulation and the A/B cloud-compare harness. Both are device-free;
# the synthetic ground-truth tests run under ctest, and the quantitative vendor
# A/B is gated on a paired recording (see tools/point_cloud).
add_executable(triangulate_test src/recon/triangulate_test.cpp)
target_link_libraries(triangulate_test PRIVATE openscanner_engine)
target_include_directories(triangulate_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME triangulate_test COMMAND triangulate_test)

add_executable(cloud_compare_test src/recon/cloud_compare_test.cpp)
target_link_libraries(cloud_compare_test PRIVATE openscanner_engine)
target_include_directories(cloud_compare_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME cloud_compare_test COMMAND cloud_compare_test)

target_include_directories(openscanner_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(openscanner_engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

find_package(Threads REQUIRED)
target_link_libraries(openscanner_engine PUBLIC Threads::Threads openscanner_mesh)

if(WIN32)
    target_link_libraries(openscanner_engine PUBLIC ws2_32)
else()
    target_link_libraries(openscanner_engine PUBLIC ${CMAKE_DL_LIBS})
endif()

if(APPLE)
    # Standalone dylib, loaded only into the live_capture_test worker
    # subprocess via DYLD_INSERT_LIBRARIES (see
    # tools/live_capture_test/main.cpp) — never linked into our own
    # binaries. -undefined dynamic_lookup is required: the Vulkan
    # symbols it interposes are deliberately not linked against
    # anything here, see vulkan_disable_interpose.c for why.
    # SHARED (not MODULE): DYLD_INSERT_LIBRARIES requires a real dylib
    # (MH_DYLIB); MODULE produces a loadable bundle (MH_BUNDLE) on macOS,
    # which dlopen() can use but the dyld insertion mechanism cannot.
    add_library(vulkan_disable_interpose SHARED src/vulkan_disable_interpose.c)
    set_target_properties(vulkan_disable_interpose PROPERTIES
        LINK_FLAGS "-undefined dynamic_lookup"
    )
endif()
