# A test double for the OBScan SDK — see stub_obscan.cpp. It exists so the
# device-free evidence for row #130 can reach the "genuine failure" path
# (exit 1) without the vendor library; no hardware run uses it.
add_library(obscan_stub SHARED stub_obscan.cpp)

target_link_libraries(obscan_stub PRIVATE openscanner_engine)
