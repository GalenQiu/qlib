cmake_minimum_required(VERSION 3.16)

project(qlib
    LANGUAGES C
    VERSION 0.1.0
    DESCRIPTION "A general purpose industrial C11 library: data structures, algorithms, network utilities and common tools."
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
# Tests default to ON only when qlib is the top level project, so that a
# consumer using add_subdirectory() does not silently build our test suite.
# PROJECT_IS_TOP_LEVEL needs CMake 3.21; this comparison works on 3.16.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(QLIB_TESTS_DEFAULT ON)
else()
    set(QLIB_TESTS_DEFAULT OFF)
endif()

option(QLIB_BUILD_TESTS "Build qlib unit tests"       ${QLIB_TESTS_DEFAULT})
option(QLIB_BUILD_BENCH "Build qlib benchmark"        OFF)
option(QLIB_BUILD_FUZZ  "Build libFuzzer targets (Clang only)" OFF)
# Placeholder module: defaulted OFF until there is something to build.
# The network module is optional so that a build can drop it. It has no
# link dependency, but "modular and optional components" is the promise.
option(QLIB_BUILD_NET   "Build qlib network module"   ON)

# Threads are optional because they are a link dependency (Threads::Threads).
# Nothing else in the library needs them; the modules that do will say so.
option(QLIB_BUILD_THREADS "Build the qlib threading module" OFF)
option(QLIB_SHARED      "Build shared library"        OFF)
option(QLIB_WERROR      "Treat compiler warnings as errors" OFF)
option(QLIB_INSTALL     "Generate install and package config rules" ON)
option(QLIB_SANITIZE    "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(QLIB_BUILD_DOCS  "Generate the API reference with Doxygen" OFF)

# C11 with no compiler extensions, as CONTRIBUTING rule 1 requires.
#
# These are the directory-level defaults for the C_STANDARD, C_STANDARD_REQUIRED
# and C_EXTENSIONS target properties, so they cover the library, the tests and
# the fuzz targets. target_compile_features() alone is NOT enough: it expresses
# a minimum, so a compiler whose default dialect already satisfies c_std_11 gets
# no -std= flag at all and builds as, say, C23. C_STANDARD_REQUIRED plus
# C_EXTENSIONS OFF is what actually pins the dialect.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# The C11 requirement also travels with the target, so consumers inherit it.
set(QLIB_C_STANDARD c_std_11)

# ---------------------------------------------------------------------------
# Sanitizers
#
# Applied with directory scope rather than through a target interface, so the
# flags reach the tests too and never leak into the installed/exported target.
# ---------------------------------------------------------------------------
if(QLIB_SANITIZE)
    if(MSVC)
        # MSVC ships AddressSanitizer but has no UndefinedBehaviorSanitizer.
        add_compile_options(/fsanitize=address)
        add_link_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    endif()
    message(STATUS "qlib: sanitizers enabled (QLIB_SANITIZE=ON)")
endif()

# ---------------------------------------------------------------------------
# Warning flags, shared by every target we build ourselves.
# Kept PRIVATE to the project: consumers of qlib never see these flags.
# ---------------------------------------------------------------------------
add_library(qlib_warnings INTERFACE)

if(MSVC)
    target_compile_options(qlib_warnings INTERFACE /W4)
else()
    target_compile_options(qlib_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wcast-qual
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wundef
        -Wwrite-strings
    )
endif()

if(QLIB_WERROR)
    if(MSVC)
        target_compile_options(qlib_warnings INTERFACE /WX)
    else()
        target_compile_options(qlib_warnings INTERFACE -Werror)
    endif()
endif()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
# M2 source files: ql_common (errors/version), ql_mem (allocator),
# ql_vector (dynamic array). ql_net is optional and appended below.
set(QLIB_SRC
    src/ql_common.c
    src/ql_hash.c
    src/ql_list.c
    src/ql_mem.c
    src/ql_str.c
    src/ql_vector.c
)

if(QLIB_BUILD_NET)
    list(APPEND QLIB_SRC src/ql_net.c)
endif()

if(QLIB_BUILD_THREADS)
    list(APPEND QLIB_SRC src/ql_thread.c)
endif()

if(QLIB_SHARED)
    add_library(qlib SHARED ${QLIB_SRC})
else()
    add_library(qlib STATIC ${QLIB_SRC})
endif()

# Consumers should always write qlib::qlib.
add_library(qlib::qlib ALIAS qlib)

target_compile_features(qlib PUBLIC ${QLIB_C_STANDARD})

# BUILD_INTERFACE keeps this build-only helper out of the exported target set.
target_link_libraries(qlib PRIVATE $<BUILD_INTERFACE:qlib_warnings>)

# Public headers: the source tree while building, the install prefix once
# installed. Never leak the build machine's absolute paths into the export.
target_include_directories(qlib PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Cross platform symbol export. Generates <build>/include/qlib/qlib_export.h.
generate_export_header(qlib
    BASE_NAME QLIB
    EXPORT_MACRO_NAME QLIB_EXPORT
    EXPORT_FILE_NAME "${PROJECT_BINARY_DIR}/include/qlib/qlib_export.h"
)

# A static build must not expand QLIB_EXPORT to dllimport on Windows.
if(NOT QLIB_SHARED)
    target_compile_definitions(qlib PUBLIC QLIB_STATIC_DEFINE)
endif()

# An optional module has to be announced to consumers, not just compiled in:
# the umbrella header <qlib/qlib.h> includes ql_net.h only when this is defined,
# so a build without the module still has a working umbrella header. PUBLIC, so
# it reaches anything linking qlib::qlib, and mirrored into qlib.pc below for
# consumers that do not use CMake.
if(QLIB_BUILD_THREADS)
    target_compile_definitions(qlib PUBLIC QLIB_HAS_THREADS)
    set(QLIB_PC_THREAD_DEFINES " -DQLIB_HAS_THREADS")
    find_package(Threads REQUIRED)
    # PUBLIC: a consumer of the static library needs the same flags.
    target_link_libraries(qlib PUBLIC Threads::Threads)
    if(NOT WIN32)
        set(QLIB_PC_THREAD_LIBS " -pthread")
    else()
        set(QLIB_PC_THREAD_LIBS "")
    endif()
else()
    set(QLIB_PC_THREAD_DEFINES "")
    set(QLIB_PC_THREAD_LIBS "")
endif()

if(QLIB_BUILD_NET)
    target_compile_definitions(qlib PUBLIC QLIB_HAS_NET)
    set(QLIB_PC_DEFINES " -DQLIB_HAS_NET")

    # The TCP half of the network module is the library's first link
    # dependency: Winsock lives in ws2_32. PUBLIC rather than PRIVATE because
    # a consumer of the static library has to link it too.
    if(WIN32)
        target_link_libraries(qlib PUBLIC ws2_32)
        set(QLIB_PC_LIBS " -lws2_32")
    else()
        set(QLIB_PC_LIBS "")
    endif()
else()
    set(QLIB_PC_DEFINES "")
    set(QLIB_PC_LIBS "")
endif()

# Gather the optional defines and libraries for qlib.pc.
set(QLIB_PC_DEFINES "${QLIB_PC_DEFINES}${QLIB_PC_THREAD_DEFINES}")
set(QLIB_PC_LIBS "${QLIB_PC_THREAD_LIBS}${QLIB_PC_LIBS}")

set_target_properties(qlib PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}"
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}"
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}"
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(QLIB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Fuzz targets (libFuzzer needs Clang; fuzz/CMakeLists.txt reports otherwise)
if(QLIB_BUILD_FUZZ)
    add_subdirectory(fuzz)
endif()

# Benchmarks
if(QLIB_BUILD_BENCH)
    add_subdirectory(bench)
endif()


# ---------------------------------------------------------------------------
# Documentation
#
# A hand maintained Doxyfile.in rather than doxygen_add_docs(): it keeps the
# configuration readable and lets docs/README.md document a manual Doxygen run
# that does not go through CMake at all.
# ---------------------------------------------------------------------------
if(QLIB_BUILD_DOCS)
    find_package(Doxygen REQUIRED OPTIONAL_COMPONENTS dot)

    set(DOXYGEN_HAVE_DOT NO)
    if(DOXYGEN_DOT_FOUND)
        set(DOXYGEN_HAVE_DOT YES)
    endif()

    configure_file(
        "${PROJECT_SOURCE_DIR}/docs/Doxyfile.in"
        "${PROJECT_BINARY_DIR}/Doxyfile"
        @ONLY
    )

    add_custom_target(docs
        COMMAND "${DOXYGEN_EXECUTABLE}" "${PROJECT_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
        COMMENT "Generating the qlib API reference in docs/html"
        VERBATIM
    )
endif()

# ---------------------------------------------------------------------------
# Install / export
# ---------------------------------------------------------------------------
if(QLIB_INSTALL)
    set(QLIB_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/qlib"
        CACHE STRING "Installation directory for the qlib CMake package files")

    # No INCLUDES DESTINATION here: target_include_directories() already
    # contributes $<INSTALL_INTERFACE:...>, and adding both duplicates the
    # entry in the exported INTERFACE_INCLUDE_DIRECTORIES.
    install(TARGETS qlib
        EXPORT qlibTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )

    # An optional module's header is installed only when the module is built, so
    # a disabled build cannot hand out a header whose symbols do not exist.
    set(QLIB_OPTIONAL_HEADER_RULES "")
    if(NOT QLIB_BUILD_NET)
        list(APPEND QLIB_OPTIONAL_HEADER_RULES PATTERN "ql_net.h" EXCLUDE)
    endif()
    if(NOT QLIB_BUILD_THREADS)
        list(APPEND QLIB_OPTIONAL_HEADER_RULES PATTERN "ql_thread.h" EXCLUDE)
    endif()

    install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/qlib"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        FILES_MATCHING PATTERN "*.h" ${QLIB_OPTIONAL_HEADER_RULES}
    )

    # The generated export header lives in the build tree, not the source tree.
    install(FILES "${PROJECT_BINARY_DIR}/include/qlib/qlib_export.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/qlib"
    )

    install(EXPORT qlibTargets
        FILE qlibTargets.cmake
        NAMESPACE qlib::
        DESTINATION "${QLIB_INSTALL_CMAKEDIR}"
    )

    write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/qlibConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/cmake/qlibConfig.cmake.in"
        "${PROJECT_BINARY_DIR}/qlibConfig.cmake"
        INSTALL_DESTINATION "${QLIB_INSTALL_CMAKEDIR}"
    )

    install(FILES
        "${PROJECT_BINARY_DIR}/qlibConfig.cmake"
        "${PROJECT_BINARY_DIR}/qlibConfigVersion.cmake"
        DESTINATION "${QLIB_INSTALL_CMAKEDIR}"
    )

    # pkg-config, for non-CMake consumers.
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/qlib.pc.in"
        "${PROJECT_BINARY_DIR}/qlib.pc"
        @ONLY
    )
    install(FILES "${PROJECT_BINARY_DIR}/qlib.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
    )

    # Uninstall target: `cmake --build . --target uninstall`
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
        "${PROJECT_BINARY_DIR}/cmake_uninstall.cmake"
        @ONLY
    )
    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P "${PROJECT_BINARY_DIR}/cmake_uninstall.cmake"
        COMMENT "Removing qlib installation"
    )
endif()
