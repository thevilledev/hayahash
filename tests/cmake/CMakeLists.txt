# Consumer fixture for the installed CMake package. Builds against an
# installed hayahash rather than the source tree, so it fails if the
# exported target does not carry a usable include directory.
#
#   cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/tmp/p
#   DESTDIR=/tmp/stage cmake --install build
#   cmake -S tests/cmake -B /tmp/cbld -DCMAKE_PREFIX_PATH=/tmp/stage/tmp/p
#   cmake --build /tmp/cbld && /tmp/cbld/consumer

cmake_minimum_required(VERSION 3.14)

# Follow the release version so a minor bump cannot leave the fixture behind.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../../VERSION" HAYAHASH_TEST_VERSION)
string(STRIP "${HAYAHASH_TEST_VERSION}" HAYAHASH_TEST_VERSION)

project(hayahash-cmake-consumer VERSION "${HAYAHASH_TEST_VERSION}" LANGUAGES C)

# Deliberately a two-component request: hayahashConfigVersion.cmake is
# written SameMinorVersion while digests are unfrozen, so this also
# checks that the current major/minor request resolves against the install.
find_package(hayahash "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}" REQUIRED)

add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE hayahash::hayahash)
