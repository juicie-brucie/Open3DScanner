# Device-free live-surface demo: a growing synthetic sphere through the real
# LiveReconPipeline, streamed as OMSH mesh frames over WsBroadcastServer to the
# WebGL viewer in web/. A harness to see the live surface forming, not product.
add_executable(surface_demo main.cpp)
target_link_libraries(surface_demo PRIVATE openscanner_engine)
target_include_directories(surface_demo PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
