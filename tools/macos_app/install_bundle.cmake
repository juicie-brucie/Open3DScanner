# Refresh OpenScanner.app from a freshly-built worker.
#
# Row #107: the old install was a POST_BUILD on the OpenScanner executable, so
# a worker-only rebuild left Contents/MacOS/scan_server stale — CMake saw the
# app up to date and never re-ran the copy. This script is invoked as a
# POST_BUILD on *both* scan_server (so any worker rebuild refreshes the bundle)
# and OpenScanner (so a clean/app build populates it).
#
# A clean tree has no bundle yet; the guard makes that a no-op and the
# OpenScanner POST_BUILD installs later.
#
# Inputs: -DAPP_CONTENTS=<bundle>/Contents -DWORKER=<built scan_server>
#         -DUI_DIR=<source ui/>

if(NOT EXISTS "${APP_CONTENTS}")
    return()
endif()

set(MACOS_DIR "${APP_CONTENTS}/MacOS")
set(RESOURCES_DIR "${APP_CONTENTS}/Resources")
file(MAKE_DIRECTORY "${MACOS_DIR}" "${RESOURCES_DIR}")

# The worker belongs in Contents/MacOS: scan_server's own exe-relative ui/
# lookup resolves ../Resources/ui, and the app resolves Contents/MacOS/scan_server.
# Contents/Resources/scan_server was an older layout and is never resolved by
# the app, so a stale copy there is deleted rather than refreshed.
if(EXISTS "${RESOURCES_DIR}/scan_server")
    file(REMOVE "${RESOURCES_DIR}/scan_server")
    message(STATUS "Removed the obsolete Contents/Resources/scan_server copy")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${WORKER}" "${MACOS_DIR}/scan_server"
    RESULT_VARIABLE copy_rc)
if(NOT copy_rc EQUAL 0)
    message(FATAL_ERROR "copying scan_server into the bundle failed (${copy_rc})")
endif()

# ui/ is static per build; copying it keeps an app-only or worker-only build's
# bundle self-consistent.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${UI_DIR}" "${RESOURCES_DIR}/ui"
    RESULT_VARIABLE ui_rc)
if(NOT ui_rc EQUAL 0)
    message(FATAL_ERROR "copying ui/ into the bundle failed (${ui_rc})")
endif()

# The copy invalidates the nested signature, and the nested change invalidates
# the outer bundle's: re-sign both. Best-effort — a machine without codesign or
# an unsigned bundle must not fail the worker build.
find_program(CODESIGN_EXECUTABLE codesign)
if(CODESIGN_EXECUTABLE)
    get_filename_component(APP_BUNDLE "${APP_CONTENTS}/.." ABSOLUTE)
    execute_process(COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - "${MACOS_DIR}/scan_server"
                    RESULT_VARIABLE ignored)
    execute_process(COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - "${APP_BUNDLE}"
                    RESULT_VARIABLE ignored)
endif()
