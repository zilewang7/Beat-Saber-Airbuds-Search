include_guard()

if (NOT DEFINED CMAKE_ANDROID_NDK)
    if (EXISTS $ENV{ANDROID_NDK_HOME})
        set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK_HOME})
    elseif (EXISTS $ENV{ANDROID_NDK_LATEST_HOME})
        set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK_LATEST_HOME})
    endif ()
endif ()

if (NOT DEFINED CMAKE_ANDROID_NDK)
    message(Big time error buddy, no NDK)
endif ()

string(REPLACE "\\" "/" CMAKE_ANDROID_NDK ${CMAKE_ANDROID_NDK})

message(STATUS "Using Android NDK: \"${CMAKE_ANDROID_NDK}\"")

# check if contains space
if (CMAKE_ANDROID_NDK MATCHES " ")
    message(FATAL_ERROR "CMAKE_ANDROID_NDK contains a space! Please remove it!")
endif ()


# Quest is armv8-64
# Uses Android 12-14 now
set(ANDROID_PLATFORM 28)
set(ANDROID_ABI arm64-v8a)
set(ANDROID_STL c++_static)
set(ANDROID_USE_LEGACY_TOOLCHAIN_FILE OFF)

# Set Android toolchain
if (CMAKE_TOOLCHAIN_FILE)
    message(WARNING "CMAKE_TOOLCHAIN_FILE is already defined. Overwriting it!")
endif ()
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake)

# Set triplet for vcpkg
set(VCPKG_TARGET_TRIPLET arm64-android)
