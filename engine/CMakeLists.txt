add_library(openscanner_engine STATIC
    src/obscan_loader.cpp
    src/obscan_mesh_bridge.cpp
    src/worker_process.cpp
    src/ws_broadcast_server.cpp
)

target_include_directories(openscanner_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)
target_link_libraries(openscanner_engine PUBLIC Threads::Threads openscanner_mesh)

if(WIN32)
    target_link_libraries(openscanner_engine PUBLIC ws2_32)
else()
    target_link_libraries(openscanner_engine PUBLIC ${CMAKE_DL_LIBS})
endif()
