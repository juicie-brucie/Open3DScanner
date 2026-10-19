add_executable(scan_server main.cpp)
target_link_libraries(scan_server PRIVATE openscanner_engine)

# Hardware-free smoke test for the tracking-verdict name rendering (row #91).
add_executable(tracking_status_test tracking_status_test.cpp)
target_link_libraries(tracking_status_test PRIVATE openscanner_engine)
add_test(NAME tracking_status_test COMMAND tracking_status_test)

# Hardware-free smoke test for the per-call bound protocol (row #89).
add_executable(bounded_watchdog_test bounded_watchdog_test.cpp)
target_link_libraries(bounded_watchdog_test PRIVATE openscanner_engine)
add_test(NAME bounded_watchdog_test COMMAND bounded_watchdog_test)

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
