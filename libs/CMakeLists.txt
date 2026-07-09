add_subdirectory(tinyobjloader EXCLUDE_FROM_ALL)
add_subdirectory(Spout2 EXCLUDE_FROM_ALL)
add_subdirectory(exprtk EXCLUDE_FROM_ALL)
add_subdirectory(freeverb EXCLUDE_FROM_ALL)

# libkeyfinder: custom wrapper (bypass submodule CMakeLists.txt)
# Local fftadapter.cpp uses custom radix-2 FFT (no FFTW3 needed)
add_library(syntetika_keyfinder STATIC
  libkeyfinder/src/audiodata.cpp
  libkeyfinder/src/chromagram.cpp
  libkeyfinder/src/chromatransform.cpp
  libkeyfinder/src/chromatransformfactory.cpp
  libkeyfinder/src/fftadapter.cpp
  libkeyfinder/src/keyclassifier.cpp
  libkeyfinder/src/keyfinder.cpp
  libkeyfinder/src/lowpassfilter.cpp
  libkeyfinder/src/lowpassfilterfactory.cpp
  libkeyfinder/src/spectrumanalyser.cpp
  libkeyfinder/src/temporalwindowfactory.cpp
  libkeyfinder/src/toneprofiles.cpp
  libkeyfinder/src/windowfunctions.cpp
  libkeyfinder/src/workspace.cpp
  libkeyfinder/src/constants.cpp
)
target_include_directories(syntetika_keyfinder PUBLIC libkeyfinder/src)
add_library(syntetika::keyfinder ALIAS syntetika_keyfinder)

if(NOT SYNTETIKA_SYSTEM_JSONCPP)
  add_subdirectory(json EXCLUDE_FROM_ALL)
  message(STATUS "Using bundled jsoncpp")
endif()
add_subdirectory(leathers EXCLUDE_FROM_ALL)
add_subdirectory(nanosvg EXCLUDE_FROM_ALL)
add_subdirectory(nanovg EXCLUDE_FROM_ALL)
add_subdirectory(oddsound-mts EXCLUDE_FROM_ALL)
add_subdirectory(psmove EXCLUDE_FROM_ALL)
add_subdirectory(push2 EXCLUDE_FROM_ALL)
add_subdirectory(readerwriterqueue EXCLUDE_FROM_ALL)

set(PYBIND11_NOPYTHON TRUE)
if(NOT SYNTETIKA_SYSTEM_PYBIND11)
  message(STATUS "Using bundled pybind11")
  add_subdirectory(pybind11 EXCLUDE_FROM_ALL)
else()
  message(STATUS "Using system installed pybind11")
  include(FindPkgConfig)
  find_package(pybind11 COMPONENTS REQUIRED)
endif()
add_library(syntetika_pybind11_wrapper INTERFACE)
target_compile_definitions(syntetika_pybind11_wrapper INTERFACE PYBIND11_EXPORT=)
target_link_libraries(syntetika_pybind11_wrapper INTERFACE pybind11::pybind11)
add_library(syntetika::pybind11 ALIAS syntetika_pybind11_wrapper)

if(NOT SYNTETIKA_SYSTEM_TUNING_LIBRARY)
  add_subdirectory(tuning-library EXCLUDE_FROM_ALL)
  message(STATUS "Using bundled tuning-library")
else()
  find_file(TUNING_LIB_TUNINGS NAMES Tunings.h PATHS /usr/local/include /usr/include REQUIRED)
  find_file(TUNING_LIB_TUNINGS_IMPL NAMES TuningsImpl.h PATHS /usr/local/include /usr/include REQUIRED)
  message(STATUS "Using system provided tuning-library")
endif()
add_subdirectory(xwax EXCLUDE_FROM_ALL)

# foleys_video_engine wrapper (FFmpeg 8.x compatible, JUCE targets)
set(FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/../ffmpeg_dev/ffmpeg-master-latest-win64-gpl-shared")
if(EXISTS "${FFMPEG_ROOT}/include")
  add_library(foleys_video_engine STATIC foleys_video_engine/foleys_video_engine.cpp)
  target_include_directories(foleys_video_engine PUBLIC
    foleys_video_engine
    ${FFMPEG_ROOT}/include
  )
  target_compile_definitions(foleys_video_engine PRIVATE
    FOLEYS_USE_OPENGL=0
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    FOLEYS_USE_FFMPEG=1
  )
  target_link_libraries(foleys_video_engine PUBLIC
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_audio_utils
    juce::juce_opengl
    ${FFMPEG_ROOT}/lib/avcodec.lib
    ${FFMPEG_ROOT}/lib/avformat.lib
    ${FFMPEG_ROOT}/lib/avutil.lib
    ${FFMPEG_ROOT}/lib/swresample.lib
    ${FFMPEG_ROOT}/lib/swscale.lib
  )
  message(STATUS "foleys_video_engine: FFmpeg 8.x linked")
else()
  message(WARNING "FFmpeg not found at ${FFMPEG_ROOT} - foleys disabled")
endif()
