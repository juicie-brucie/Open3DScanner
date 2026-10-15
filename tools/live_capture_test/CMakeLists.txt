add_executable(live_capture_test main.cpp)
target_link_libraries(live_capture_test PRIVATE openscanner_engine)

if(APPLE)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation REQUIRED)
    target_link_libraries(live_capture_test PRIVATE ${CORE_FOUNDATION_LIBRARY})
endif()
