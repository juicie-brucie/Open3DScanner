if(NOT APPLE)
    return()
endif()

if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")
endif()

set(APP_NAME "OpenScanner")

add_executable(OpenScanner MACOSX_BUNDLE main.mm)
add_dependencies(OpenScanner scan_server)
set_target_properties(OpenScanner PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "${APP_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.openscanner.app"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
    MACOSX_BUNDLE_ICON_FILE "OpenScanner.icns"
    OUTPUT_NAME "${APP_NAME}"
)

configure_file(Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)
set_target_properties(OpenScanner PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")

# Icon: generated at build time from the committed generator (no asset catalog).
set(ICON_PATH "${CMAKE_CURRENT_BINARY_DIR}/OpenScanner.icns")
add_custom_command(
    OUTPUT "${ICON_PATH}"
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/make_icon.py" "${ICON_PATH}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/make_icon.py"
    COMMENT "Generating OpenScanner.icns"
    VERBATIM
)
add_custom_target(OpenScannerIcon DEPENDS "${ICON_PATH}")
add_dependencies(OpenScanner OpenScannerIcon)

# Copy the icon into the .app's Resources at build time.
add_custom_command(TARGET OpenScanner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ICON_PATH}"
            "$<TARGET_BUNDLE_CONTENT_DIR:OpenScanner>/Resources/OpenScanner.icns"
    COMMENT "Installing OpenScanner.icns into the bundle"
)

target_compile_options(OpenScanner PRIVATE "-fobjc-arc")
target_link_libraries(OpenScanner PRIVATE "-framework Cocoa" "-framework WebKit")

# Bundle our own capture worker (not the vendor dylib) so the app owns it.
# scan_server goes in Contents/MacOS (so its exe-relative ui/ lookup finds
# Contents/Resources/ui); the UI directory is copied alongside.
#
# Row #107: the install runs from the shared script, and scan_server also calls
# it as its own POST_BUILD (tools/scan_server/CMakeLists.txt) so a worker-only
# rebuild refreshes the bundle instead of leaving the old copy behind. This
# call populates the bundle on a clean/app build.
add_custom_command(TARGET OpenScanner POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            "-DAPP_CONTENTS=$<TARGET_BUNDLE_CONTENT_DIR:OpenScanner>"
            "-DWORKER=$<TARGET_FILE:scan_server>"
            "-DUI_DIR=${CMAKE_SOURCE_DIR}/ui"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/install_bundle.cmake"
    COMMENT "Installing scan_server and ui/ into the bundle"
)
