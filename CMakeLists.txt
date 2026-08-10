# CMake package for the header-only C reference.
#
#   cmake -S . -B build
#   cmake --install build --prefix /usr/local
#
# Installs exactly what `make install` installs, to the same paths, and
# adds a CMake package config on top so consumers can use either:
#
#   find_package(hayahash 0.5 REQUIRED)
#   target_link_libraries(app PRIVATE hayahash::hayahash)
#
# Copying hayahash.h into a project stays fully supported; this target
# is for distro, system, and CMake-consumer packaging.

cmake_minimum_required(VERSION 3.14)

# VERSION at the repository root is the single source of truth, shared
# with the pkg-config file and every port manifest. Strip it the same
# way the Makefile does (`tr -d ' \n'`) so the two agree exactly.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" HAYAHASH_VERSION_RAW)
string(STRIP "${HAYAHASH_VERSION_RAW}" HAYAHASH_VERSION)

project(hayahash
    VERSION "${HAYAHASH_VERSION}"
    DESCRIPTION "Fast portable 64- and 128-bit non-cryptographic hash functions"
    HOMEPAGE_URL "https://github.com/thevilledev/hayahash"
    LANGUAGES C)

# PROJECT_IS_TOP_LEVEL is CMake 3.21; derive it so the floor stays 3.14.
if(NOT DEFINED HAYAHASH_IS_TOP_LEVEL)
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        set(HAYAHASH_IS_TOP_LEVEL ON)
    else()
        set(HAYAHASH_IS_TOP_LEVEL OFF)
    endif()
endif()

option(HAYAHASH_INSTALL "Generate the hayahash install rules"
    "${HAYAHASH_IS_TOP_LEVEL}")

add_library(hayahash INTERFACE)
add_library(hayahash::hayahash ALIAS hayahash)

target_include_directories(hayahash INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:include>")

if(NOT HAYAHASH_INSTALL)
    return()
endif()

include(CMakePackageConfigHelpers)

# Deliberately "lib" rather than GNUInstallDirs' CMAKE_INSTALL_LIBDIR.
# The Makefile hardcodes $(PREFIX)/lib, and hayahash.pc.in hardcodes
# libdir=${exec_prefix}/lib, so GNUInstallDirs would put the .pc file in
# lib64 on Fedora and friends while the .pc kept claiming lib. Matching
# the Makefile keeps the two install paths interchangeable.
set(HAYAHASH_LIBDIR "lib")

# hayahash.pc.in uses @prefix@ and @VERSION@, and the Makefile
# substitutes the un-staged PREFIX (DESTDIR is applied to the install
# location only, never to the file contents). @ONLY reproduces the
# Makefile's sed output byte for byte.
set(prefix "${CMAKE_INSTALL_PREFIX}")
set(VERSION "${HAYAHASH_VERSION}")
configure_file(hayahash.pc.in "${CMAKE_CURRENT_BINARY_DIR}/hayahash.pc" @ONLY)

install(FILES hayahash.h TYPE INCLUDE)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hayahash.pc"
    DESTINATION "${HAYAHASH_LIBDIR}/pkgconfig")

set(HAYAHASH_CMAKEDIR "${HAYAHASH_LIBDIR}/cmake/hayahash")

install(TARGETS hayahash EXPORT hayahashTargets)
install(EXPORT hayahashTargets
    FILE hayahashTargets.cmake
    NAMESPACE hayahash::
    DESTINATION "${HAYAHASH_CMAKEDIR}")

# SameMinorVersion, not SameMajorVersion: while hayahash is pre-1.0 the
# digest may change between minors (it did at 0.3.0 and again at 0.5.0),
# so a 0.5 consumer must not silently accept a 0.6 install. Revisit when
# docs/stability.md freezes digests at 1.0.
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/hayahashConfigVersion.cmake"
    VERSION "${HAYAHASH_VERSION}"
    COMPATIBILITY SameMinorVersion
    ARCH_INDEPENDENT)

configure_package_config_file(
    cmake/hayahashConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/hayahashConfig.cmake"
    INSTALL_DESTINATION "${HAYAHASH_CMAKEDIR}")

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/hayahashConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/hayahashConfigVersion.cmake"
    DESTINATION "${HAYAHASH_CMAKEDIR}")
