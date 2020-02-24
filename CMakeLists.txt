cmake_minimum_required(VERSION 3.5.1)
project(king)
set(CMAKE_CXX_STANDARD 14)

# libusb-1.0 on windows
if(DEFINED WIN32)
    set(LIBUSB10_PATH "c:/checkm8/libusb_win/" CACHE PATH "Path to libusb1-0 directory")
endif()

set(SOURCE_FILES 
src/main.cpp src/dfu.cpp 
src/usbexec.cpp
src/ASN1DERElement.cpp
src/lzssdec.c
src/img4tool.cpp
src/aes.c
)

add_executable(king ${SOURCE_FILES})

target_include_directories(king PRIVATE include)

if(DEFINED WIN32) 
    target_include_directories(king PRIVATE ${LIBUSB10_PATH}/include)
    find_library(LIBUSB10 NAMES libusb-1.0.lib PATHS ${LIBUSB10_PATH}/lib)
    target_link_libraries(king ${LIBUSB10} Ws2_32.lib)
else()
    target_link_libraries(king usb-1.0)
endif()
