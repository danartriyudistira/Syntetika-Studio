project(tinyobjloader VERSION 2.0.0 LANGUAGES CXX)

add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE .)

add_library(syntetika::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
