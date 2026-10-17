add_executable(device_message_listener_test main.cpp)
add_test(NAME device_message_listener_test COMMAND device_message_listener_test)
target_link_libraries(device_message_listener_test PRIVATE openscanner_engine)

if(APPLE)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation REQUIRED)
    target_link_libraries(device_message_listener_test PRIVATE ${CORE_FOUNDATION_LIBRARY})

    add_dependencies(device_message_listener_test vulkan_disable_interpose)
    target_compile_definitions(device_message_listener_test PRIVATE
        VULKAN_DISABLE_INTERPOSE_DYLIB="$<TARGET_FILE:vulkan_disable_interpose>"
    )
endif()