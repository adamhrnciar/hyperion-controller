# SPDX-License-Identifier: BSD-3-Clause
# Locate the Raspberry Pi Pico SDK. Same behaviour as the pico_sdk_import.cmake
# shipped in <PICO_SDK_PATH>/external/ (BSD-3-Clause, Raspberry Pi Ltd):
#   - PICO_SDK_PATH (CMake or environment) points at an existing checkout, or
#   - PICO_SDK_FETCH_FROM_GIT=ON downloads it (optionally PICO_SDK_FETCH_FROM_GIT_TAG).

if (DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif ()
if (DEFINED ENV{PICO_SDK_FETCH_FROM_GIT} AND (NOT PICO_SDK_FETCH_FROM_GIT))
    set(PICO_SDK_FETCH_FROM_GIT $ENV{PICO_SDK_FETCH_FROM_GIT})
endif ()
if (DEFINED ENV{PICO_SDK_FETCH_FROM_GIT_TAG} AND (NOT PICO_SDK_FETCH_FROM_GIT_TAG))
    set(PICO_SDK_FETCH_FROM_GIT_TAG $ENV{PICO_SDK_FETCH_FROM_GIT_TAG})
endif ()

set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Raspberry Pi Pico SDK")
set(PICO_SDK_FETCH_FROM_GIT "${PICO_SDK_FETCH_FROM_GIT}" CACHE BOOL "Download the Pico SDK from git if PICO_SDK_PATH is not set")
if (NOT PICO_SDK_FETCH_FROM_GIT_TAG)
    set(PICO_SDK_FETCH_FROM_GIT_TAG "master")
endif ()

if (NOT PICO_SDK_PATH)
    if (PICO_SDK_FETCH_FROM_GIT)
        include(FetchContent)
        FetchContent_Declare(
                pico_sdk
                GIT_REPOSITORY https://github.com/raspberrypi/pico-sdk
                GIT_TAG ${PICO_SDK_FETCH_FROM_GIT_TAG}
                GIT_SUBMODULES_RECURSE FALSE
        )
        if (NOT pico_sdk)
            message("Downloading Raspberry Pi Pico SDK")
            FetchContent_Populate(pico_sdk)
            set(PICO_SDK_PATH ${pico_sdk_SOURCE_DIR})
        endif ()
    else ()
        message(FATAL_ERROR
                "Pico SDK not found. Set PICO_SDK_PATH, or pass -DPICO_SDK_FETCH_FROM_GIT=ON.")
    endif ()
endif ()

get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
if (NOT EXISTS ${PICO_SDK_PATH})
    message(FATAL_ERROR "Directory '${PICO_SDK_PATH}' not found")
endif ()

set(PICO_SDK_INIT_CMAKE_FILE ${PICO_SDK_PATH}/pico_sdk_init.cmake)
if (NOT EXISTS ${PICO_SDK_INIT_CMAKE_FILE})
    message(FATAL_ERROR "Directory '${PICO_SDK_PATH}' does not appear to contain the Raspberry Pi Pico SDK")
endif ()

set(PICO_SDK_PATH ${PICO_SDK_PATH} CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)

include(${PICO_SDK_INIT_CMAKE_FILE})
