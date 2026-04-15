set(PLATFORM "Android" CACHE STRING "Force Android" FORCE)

cmake_minimum_required(VERSION 3.22)
project(rts C)

set(CMAKE_C_STANDARD 11)

set(PLATFORM "Android" CACHE STRING "Platform force" FORCE)
set(CUSTOM_ANDROID_API 24 CACHE STRING "API version")
set(CMAKE_SYSTEM_VERSION 24)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.0
)
FetchContent_MakeAvailable(raylib)

# Absolute path to our game source tree
set(RTS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src")

file(GLOB_RECURSE SRC_FILES
    "${RTS_SRC_DIR}/*.c"
)

add_library(rts SHARED ${SRC_FILES})

target_include_directories(rts PRIVATE
    "${RTS_SRC_DIR}"
    "${RTS_SRC_DIR}/core"
    "${RTS_SRC_DIR}/core/unit"
    "${RTS_SRC_DIR}/core/enet"
    "${RTS_SRC_DIR}/ui"
    "${RTS_SRC_DIR}/ui/renderer"
    "${RTS_SRC_DIR}/ui/hud"
    "${RTS_SRC_DIR}/ui/input"
    "${RTS_SRC_DIR}/ai"
)

target_compile_definitions(rts PRIVATE
    _GNU_SOURCE          # expose POSIX networking for ENet
    PLATFORM_ANDROID=1   # explicit platform flag for our ifdefs
)


# ──────────────────────────────────────────────────────────────
# CRITICAL: Force the linker to export ANativeActivity_onCreate.
#
# This is the JNI entry point that the Android Java runtime calls
# to start our NativeActivity.  Because no C code in our project
# *calls* this function (only the Java layer does), the linker
# treats it as dead code and drops it — making the app crash
# immediately on launch with a black screen and no error.
#
# -u <sym>  =  "undefined reference": treat sym as an unresolved
#   reference that must be satisfied by the archives, even if no
#   object file explicitly references it.  This pulls the
#   ANativeActivity_onCreate object from libraylib.a (where
#   android_native_app_glue was merged) into librts.so.
# ──────────────────────────────────────────────────────────────
target_link_options(rts PRIVATE -u ANativeActivity_onCreate)

target_link_libraries(rts raylib log android EGL GLESv2)
