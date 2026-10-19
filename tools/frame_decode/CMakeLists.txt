# Row #125: the band renderer's offline regression (hardware-free, Python).
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME frame_decode_test
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_frame_decode.py)
