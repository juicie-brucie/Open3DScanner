# Vendored third-party geometry libraries for the mesh-editing toolbox.
# See /THIRD_PARTY_LICENSES.md and /docs/architecture/mesh-editing-stack.md
# for why these two (and nothing else) are vendored here.
#
# Both are configured conservatively: every optional module that could pull in
# a GPL/LGPL dependency, a proprietary dependency, or extra build time we don't
# need is forced off before add_subdirectory() runs.

# --- libigl (MPL-2.0 core) ------------------------------------------------
# HARD RULE: never flip LIBIGL_COPYLEFT_* or LIBIGL_RESTRICTED_* on. The
# copyleft modules are GPL-3 (CGAL-backed); the restricted ones pull in
# proprietary/non-commercial-licensed dependencies (MATLAB, MOSEK, the
# "Triangle" library). Only igl::core (MPL-2.0, always built) is used.
set(LIBIGL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBIGL_BUILD_TUTORIALS OFF CACHE BOOL "" FORCE)
set(LIBIGL_INSTALL OFF CACHE BOOL "" FORCE)
set(LIBIGL_COPYLEFT_CORE OFF CACHE BOOL "" FORCE)
set(LIBIGL_COPYLEFT_CGAL OFF CACHE BOOL "" FORCE)
set(LIBIGL_COPYLEFT_COMISO OFF CACHE BOOL "" FORCE)
set(LIBIGL_COPYLEFT_TETGEN OFF CACHE BOOL "" FORCE)
set(LIBIGL_RESTRICTED_MATLAB OFF CACHE BOOL "" FORCE)
set(LIBIGL_RESTRICTED_MOSEK OFF CACHE BOOL "" FORCE)
set(LIBIGL_RESTRICTED_TRIANGLE OFF CACHE BOOL "" FORCE)
set(LIBIGL_EMBREE OFF CACHE BOOL "" FORCE)
set(LIBIGL_GLFW OFF CACHE BOOL "" FORCE)
set(LIBIGL_IMGUI OFF CACHE BOOL "" FORCE)
set(LIBIGL_OPENGL OFF CACHE BOOL "" FORCE)
set(LIBIGL_STB OFF CACHE BOOL "" FORCE)
set(LIBIGL_PREDICATES OFF CACHE BOOL "" FORCE)
set(LIBIGL_SPECTRA OFF CACHE BOOL "" FORCE)
set(LIBIGL_XML OFF CACHE BOOL "" FORCE)
set(LIBIGL_CYCODEBASE OFF CACHE BOOL "" FORCE)
add_subdirectory(libigl)

if(TARGET igl_copyleft_core OR TARGET igl_copyleft_cgal OR TARGET igl_copyleft_comiso OR TARGET igl_copyleft_tetgen)
    message(FATAL_ERROR
        "A GPL-licensed igl::copyleft::* module got enabled. This must stay "
        "off to keep OpenScanner's dependency tree MIT-compatible — check "
        "third_party/CMakeLists.txt.")
endif()

# --- Manifold (Apache-2.0) -------------------------------------------------
# Only the core 3D boolean/CSG target is needed here. Cross-section (2D),
# parallel backend (TBB), pybind, and the test suite are all off to avoid
# pulling in extra dependencies (Clipper2, oneTBB, GoogleTest) we don't use
# yet; each can be turned on later if a feature actually needs it.
set(MANIFOLD_TEST OFF CACHE BOOL "" FORCE)
set(MANIFOLD_CBIND OFF CACHE BOOL "" FORCE)
set(MANIFOLD_CROSS_SECTION OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PAR OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PYBIND OFF CACHE BOOL "" FORCE)
set(ASSIMP_ENABLE OFF CACHE BOOL "" FORCE)

# Build manifold as a static lib regardless of the parent project's default,
# without changing BUILD_SHARED_LIBS for anything else added after this.
set(_openscanner_saved_build_shared_libs ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(manifold)
set(BUILD_SHARED_LIBS ${_openscanner_saved_build_shared_libs})
unset(_openscanner_saved_build_shared_libs)
