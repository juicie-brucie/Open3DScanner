add_executable(scan_server main.cpp)
target_link_libraries(scan_server PRIVATE openscanner_engine)

if(APPLE)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation REQUIRED)
    target_link_libraries(scan_server PRIVATE ${CORE_FOUNDATION_LIBRARY})

    # --live mode runs a real OBScan session; same crash/hang isolation
    # story as live_capture_test (see vulkan_disable_interpose.c /
    # worker_process.cpp).
    add_dependencies(scan_server vulkan_disable_interpose)
    target_compile_definitions(scan_server PRIVATE
        VULKAN_DISABLE_INTERPOSE_DYLIB="$<TARGET_FILE:vulkan_disable_interpose>"
    )
endif()
