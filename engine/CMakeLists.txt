add_library(openscanner_engine STATIC
    src/obscan_loader.cpp
    src/obscan_mesh_bridge.cpp
    src/worker_process.cpp
    src/ws_broadcast_server.cpp
)

target_include_directories(openscanner_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

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
