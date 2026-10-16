add_executable(live_capture_test main.cpp)
target_link_libraries(live_capture_test PRIVATE openscanner_engine)

if(APPLE)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation REQUIRED)
    target_link_libraries(live_capture_test PRIVATE ${CORE_FOUNDATION_LIBRARY})

    # See vulkan_disable_interpose.c / worker_process.cpp: the worker
    # loads this via DYLD_INSERT_LIBRARIES, built fresh here rather than
    # relying on a path baked in elsewhere.
    add_dependencies(live_capture_test vulkan_disable_interpose)
    target_compile_definitions(live_capture_test PRIVATE
        VULKAN_DISABLE_INTERPOSE_DYLIB="$<TARGET_FILE:vulkan_disable_interpose>"
    )
endif()
