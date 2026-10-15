# Mesh-editing toolbox (boolean ops via Manifold, decimation/smoothing via
# libigl). Kept as its own library, separate from engine/ (the OBScan device
# binding layer), so the two can be developed independently.
add_library(openscanner_mesh STATIC
    src/mesh_tools.cpp
)

target_include_directories(openscanner_mesh PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(openscanner_mesh PUBLIC
    igl::core
    manifold
)
