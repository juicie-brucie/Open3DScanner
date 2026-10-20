# Row #159: the confidence/retention experiment's offline harness.
#
# The harness is device-free and dependency-free: it reads a cloud, scores it
# against a ground truth, estimates a per-point confidence, sweeps a retention
# threshold and reports the accuracy of what was kept next to the accuracy of
# an equal-sized random keep.
#
# The self-test is pinned under `ctest -LE hardware`: on a synthetic scene with
# known truth, the confidence curve must beat random retention - and a shuffled
# confidence field must not.

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_library(confidence_sweep_core STATIC
    harness.cpp
    io.cpp
    plot.cpp
)
target_include_directories(confidence_sweep_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(confidence_sweep_core PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)

add_executable(confidence_sweep main.cpp)
target_link_libraries(confidence_sweep PRIVATE confidence_sweep_core)

# The harness's self-test: the synthetic scene, the estimator, the sweep, the
# random control, the negative control and the PLY round trip.
add_executable(confidence_sweep_test sweep_test.cpp)
target_link_libraries(confidence_sweep_test PRIVATE confidence_sweep_core)
target_include_directories(confidence_sweep_test PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
add_test(NAME confidence_sweep_test COMMAND confidence_sweep_test)

# The same acceptance through the command line, so the tool's own exit code
# (3 when the curve does not beat random) is pinned as well.
add_test(NAME confidence_sweep_cli_test
         COMMAND confidence_sweep --synthetic --expect-beats-random --quiet)
set_tests_properties(confidence_sweep_cli_test PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# The run's artefacts: the CSV and the SVG, checked for being written and for
# being identical between two runs (the harness is deterministic).
add_test(NAME confidence_sweep_artifacts_test
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_artifacts.py
                 $<TARGET_FILE:confidence_sweep>)
set_tests_properties(confidence_sweep_artifacts_test PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
