# Install script for directory: C:/Users/user/Desktop/Syntetika Project/Syntetika Studio/libs/readerwriterqueue

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/Syntetika")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/readerwriterqueue" TYPE FILE FILES
    "C:/Users/user/Desktop/Syntetika Project/Syntetika Studio/libs/readerwriterqueue/atomicops.h"
    "C:/Users/user/Desktop/Syntetika Project/Syntetika Studio/libs/readerwriterqueue/readerwriterqueue.h"
    "C:/Users/user/Desktop/Syntetika Project/Syntetika Studio/libs/readerwriterqueue/readerwritercircularbuffer.h"
    "C:/Users/user/Desktop/Syntetika Project/Syntetika Studio/libs/readerwriterqueue/LICENSE.md"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Devel" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/readerwriterqueue/readerwriterqueueTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/readerwriterqueue/readerwriterqueueTargets.cmake"
         "C:/Users/user/Desktop/Syntetika Project/Syntetika Studio/build2/libs/readerwriterqueue/CMakeFiles/Export/8994e3f4f61a5badde4ac84576fbfa78/readerwriterqueueTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/readerwriterqueue/readerwriterqueueTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/readerwriterqueue/readerwriterqueueTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/readerwriterqueue" TYPE FILE FILES "C:/Users/user/Desktop/Syntetika Project/Syntetika Studio/build2/libs/readerwriterqueue/CMakeFiles/Export/8994e3f4f61a5badde4ac84576fbfa78/readerwriterqueueTargets.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Devel" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/readerwriterqueue" TYPE FILE FILES
    "C:/Users/user/Desktop/Syntetika Project/Syntetika Studio/build2/libs/readerwriterqueue/readerwriterqueueConfig.cmake"
    "C:/Users/user/Desktop/Syntetika Project/Syntetika Studio/build2/libs/readerwriterqueue/readerwriterqueueConfigVersion.cmake"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/Users/user/Desktop/Syntetika Project/Syntetika Studio/build2/libs/readerwriterqueue/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
