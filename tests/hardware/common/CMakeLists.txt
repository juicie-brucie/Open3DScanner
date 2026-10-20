# Shared by the hardware tests: the precondition probe that tells "this run
# cannot happen" (skip) from "this run failed" (failure).
add_library(hw_precondition STATIC hw_precondition.cpp)

target_include_directories(hw_precondition PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(APPLE)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation REQUIRED)
    find_library(IOKIT_LIBRARY IOKit REQUIRED)
    target_link_libraries(hw_precondition PUBLIC ${CORE_FOUNDATION_LIBRARY} ${IOKIT_LIBRARY})
endif()
