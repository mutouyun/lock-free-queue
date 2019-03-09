cmake_minimum_required(VERSION 3.10)
project(lock-free)

set(CMAKE_CXX_STANDARD 17)
if(NOT MSVC)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT MSVC)
  add_compile_options(-Wno-attributes -Wno-missing-field-initializers -Wno-unused-variable -Wno-unused-function)
endif()

include_directories(./include ./)
file(GLOB SRC_FILES ./*.cpp)
file(GLOB HEAD_FILES ./include/*.h)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/output)

link_directories(${EXECUTABLE_OUTPUT_PATH})

add_executable(${PROJECT_NAME} ${SRC_FILES} ${HEAD_FILES})
if(NOT MSVC)
  target_link_libraries(${PROJECT_NAME} pthread)
endif()
