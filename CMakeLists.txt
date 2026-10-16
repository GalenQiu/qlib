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
option(QLIB_BUILD_TESTS "Build qlib unit tests"       ON)
option(QLIB_BUILD_BENCH "Build qlib benchmark"        OFF)
option(QLIB_BUILD_FUZZ  "Build libFuzzer targets"     OFF)
option(QLIB_BUILD_NET   "Build qlib network module"   ON)
option(QLIB_SHARED      "Build shared library"        OFF)
option(QLIB_WERROR      "Treat compiler warnings as errors" OFF)
option(QLIB_INSTALL     "Generate install and package config rules" ON)

# The C11 requirement travels with the target, so consumers inherit it.
set(QLIB_C_STANDARD c_std_11)

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
# M0 source files: only ql_common
set(QLIB_SRC
    src/ql_common.c
)

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

# Benchmark placeholder
if(QLIB_BUILD_BENCH)
    # add_subdirectory(bench)
endif()

# Fuzz placeholder
if(QLIB_BUILD_FUZZ)
    # add_subdirectory(fuzz)
endif()

# Network module placeholder
if(QLIB_BUILD_NET)
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

    install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/qlib"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        FILES_MATCHING PATTERN "*.h"
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
