# Android: build the loader's native side by including the top-level CMakeLists.
#
# The shared libraries the APK needs (libmain.so, libSDL2.so) are produced by the
# top-level project's `if(ANDROID)` branch, which also flips SDL2 to a shared build.
# The static core (libzengine_core.a) is linked into libmain.so by the top-level
# project; Gradle then packages every shared library under this module's build/
# directory into the APK's jniLibs.
cmake_minimum_required(VERSION 3.22)
project(zgirlsmobile_android LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17 CACHE STRING "" FORCE)
set(ZDROID_BUILD_TESTS  OFF  CACHE BOOL "" FORCE)
set(ZDROID_BUILD_VIEWER ON  CACHE BOOL "" FORCE)
set(ZDROID_FIGHT_MODE   ON  CACHE BOOL "" FORCE)   # ZGirlsMobile: the fight mode is always on in the APK
# ZDROID_SDL2_SOURCE_DIR is passed in by Gradle (see app/build.gradle).

# Bring in the top-level project: it defines the engine core static library and, when
# ANDROID is set by the toolchain, the `main` shared library that SDLActivity loads.
get_filename_component(ZDROID_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_subdirectory(${ZDROID_ROOT} ${CMAKE_BINARY_DIR}/_core EXCLUDE_FROM_ALL)
