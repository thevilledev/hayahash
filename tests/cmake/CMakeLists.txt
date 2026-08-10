# Consumer fixture for the installed CMake package. Builds against an
# installed hayahash rather than the source tree, so it fails if the
# exported target does not carry a usable include directory.
#
#   cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/tmp/p
#   DESTDIR=/tmp/stage cmake --install build
#   cmake -S tests/cmake -B /tmp/cbld -DCMAKE_PREFIX_PATH=/tmp/stage/tmp/p
#   cmake --build /tmp/cbld && /tmp/cbld/consumer

cmake_minimum_required(VERSION 3.14)

project(hayahash-cmake-consumer LANGUAGES C)

# Deliberately a two-component request: hayahashConfigVersion.cmake is
# written SameMinorVersion while digests are unfrozen, so this also
# pins that a 0.5 consumer resolves against a 0.5 install.
find_package(hayahash 0.5 REQUIRED)

add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE hayahash::hayahash)
