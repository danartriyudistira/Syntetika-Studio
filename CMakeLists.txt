cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0091 NEW)

# Global build parameters. Optional features are configurable via additional
# SYNTETIKA_* variables defined in their respective subdirs.
option(SYNTETIKA_NIGHTLY "Automated build from github" OFF)
option(SYNTETIKA_PORTABLE "Create a self-contained build that ships all dependencies" OFF)
set(SYNTETIKA_JUCE_LOCATION "${CMAKE_SOURCE_DIR}/libs/JUCE" CACHE STRING "Path to JUCE library source tree")
set(SYNTETIKA_PYTHON_ROOT "" CACHE STRING "Python search path. Override as needed.")
set(SYNTETIKA_VST2_SDK_LOCATION "" CACHE STRING "Steinberg VST2 SDK directory for non-FOSS builds")
set(SYNTETIKA_ASIO_SDK_LOCATION "" CACHE STRING "Steinberg ASIO SDK directory for non-FOSS builds")
set(SYNTETIKA_SPACEMOUSE_SDK_LOCATION "" CACHE STRING "3Dxware SpaceMouse SDK directory for non-FOSS builds")
option(SYNTETIKA_SYSTEM_JUCE "Use system installation of juce" OFF)
option(SYNTETIKA_DEVENDORED_SYSTEM_JUCE "Use system libraries when using system installation of juce" OFF)
option(SYNTETIKA_SYSTEM_PYBIND11 "Use a system installation of pybind11" OFF)
option(SYNTETIKA_SYSTEM_JSONCPP "Use system-wide installation of jsoncpp" OFF)
option(SYNTETIKA_SYSTEM_TUNING_LIBRARY "Use system installation of tuning-library" OFF)
option(SYNTETIKA_SYSTEM_ABLETONLINK "Use system installation of abletonlink" OFF)
option(SYNTETIKA_USE_ASAN "Build with ASAN" OFF)

# Global CMake options
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # clangd/LSP support
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT Syntetika)
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.13 CACHE STRING "Minimum macOS version")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif ()

project(Syntetika VERSION 0.0.0 LANGUAGES C CXX ASM)

message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
math(EXPR SYNTETIKA_BITNESS "${CMAKE_SIZEOF_VOID_P} * 8" OUTPUT_FORMAT DECIMAL)
message(STATUS "Targeting ${SYNTETIKA_BITNESS} bit configuration")

# Global compile options
add_compile_options(
    # Build with Multiple Processes on Visual Studio
    $<$<CXX_COMPILER_ID:MSVC>:/MP>
    # Set source and executable charsets to UTF-8. Required for building on CJK Windows.
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    # Enable C++ exceptions (required by JUCE)
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
    # Enable RTTI (required by JUCE)
    $<$<CXX_COMPILER_ID:MSVC>:/GR>
    )

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(WIN32 AND ${SYNTETIKA_BITNESS} EQUAL 32)
    add_link_options(
        # Windows 32-bit: Extended address space
        $<$<CXX_COMPILER_ID:MSVC>:/LARGEADDRESSAWARE>
        $<$<BOOL:${MINGW}>:LINKER:--large-address-aware>
        )
endif()

if (SYNTETIKA_USE_ASAN)
    message( STATUS "BUILDING with ASAN" )
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(SYNTETIKA_NIGHTLY)
    message(STATUS "Nightly build")
endif()

if(SYNTETIKA_PORTABLE)
    message(STATUS "Portable build enabled")
    if(MINGW)
        message(WARNING "Portable MinGW builds don't ship the runtime DLLs \
        (libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll). Make sure \
        to copy them to the .exe's directory before distributing this build.")
    endif()
endif()

if(SYNTETIKA_PYTHON_ROOT)
    set(Python_ROOT "${SYNTETIKA_PYTHON_ROOT}")
    message(STATUS "Overriding Python_ROOT to ${Python_ROOT}")
endif()
find_package (Python 3.6 COMPONENTS Interpreter Development REQUIRED)
message(STATUS "Python version: ${Python_VERSION}")
message(STATUS "Python executable: ${Python_EXECUTABLE}")
message(STATUS "Python library: ${Python_LIBRARIES}")

if(NOT SYNTETIKA_SYSTEM_JUCE)
  message(STATUS "Using JUCE from ${SYNTETIKA_JUCE_LOCATION}")
  add_subdirectory(${SYNTETIKA_JUCE_LOCATION} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
  message(STATUS "Using system provided JUCE")
  find_package(JUCE COMPONENTS REQUIRED)
  if(SYNTETIKA_DEVENDORED_SYSTEM_JUCE)
    message(STATUS "Using devendored system provided JUCE")
    include(FindPkgConfig)
    pkg_search_module(flac REQUIRED flac)
    pkg_search_module(ogg REQUIRED ogg)
    pkg_search_module(vorbis REQUIRED vorbis vorbisenc vorbisfile)
  endif()
endif()

if(SYNTETIKA_SYSTEM_JSONCPP)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(JSONCPP REQUIRED jsoncpp)
    include_directories(${JSONCPP_INCLUDE_DIRS})
endif()

if (SYNTETIKA_VST2_SDK_LOCATION)
    juce_set_vst2_sdk_path(${SYNTETIKA_VST2_SDK_LOCATION})
    message(STATUS "Enabling VST2 - caution, non FOSS build")
endif()

add_subdirectory(libs)
add_subdirectory(Source)
