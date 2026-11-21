cmake_minimum_required(VERSION 3.25)

project(MusicsheetDaw
    VERSION 0.1.0
    DESCRIPTION "Notation-first DAW"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Read and emit sources as UTF-8 regardless of the system code page (e.g. 950).
if(MSVC)
    add_compile_options(/utf-8)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# melatonin inspector: on by default in Debug, off otherwise. Can be forced either way.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_msd_inspector_default ON)
else()
    set(_msd_inspector_default OFF)
endif()
option(MSD_ENABLE_INSPECTOR "Link melatonin inspector (Ctrl+I toggles it)" ${_msd_inspector_default})

option(MSD_BUILD_TESTS "Build the unit tests" ON)

include(cmake/Dependencies.cmake)

add_subdirectory(src/core)
add_subdirectory(src/layout)
add_subdirectory(src/app)

if(MSD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
