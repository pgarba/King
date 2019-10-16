cmake_minimum_required(VERSION 3.10)
project(king)
set(CMAKE_CXX_STANDARD 14)

set(SOURCE_FILES src/main.cpp src/dfu.cpp)

add_executable(king ${SOURCE_FILES})

target_include_directories(king PRIVATE include)
TARGET_LINK_LIBRARIES(king usb-1.0)