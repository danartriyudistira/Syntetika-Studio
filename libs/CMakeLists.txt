add_subdirectory(tinyobjloader EXCLUDE_FROM_ALL)
add_subdirectory(Spout2 EXCLUDE_FROM_ALL)
add_subdirectory(exprtk EXCLUDE_FROM_ALL)
add_subdirectory(freeverb EXCLUDE_FROM_ALL)

# libkeyfinder: custom wrapper (bypass submodule CMakeLists.txt)
find_package(FFTW3 REQUIRED)
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
target_include_directories(syntetika_keyfinder PRIVATE ${FFTW3_INCLUDE_DIR})
target_link_libraries(syntetika_keyfinder PRIVATE ${FFTW3_LIBRARY})
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
option(SYNTETIKA_DISABLE_FOLEYS "Disable foleys_video_engine (needs FFmpeg/OBJC)" OFF)
if(NOT SYNTETIKA_DISABLE_FOLEYS)
  add_subdirectory(foleys_video_engine EXCLUDE_FROM_ALL)
endif()
