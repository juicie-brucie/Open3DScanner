# Row #123: the frame-bin extractor's CI test (hardware-free, Python).
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME frame_bins_test
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_frame_bins.py)
