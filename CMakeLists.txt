cmake_minimum_required(VERSION 3.5.1)

# Allow CMAKE_MSVC_RUNTIME_LIBRARY (below) to take effect on MSVC.
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(king)
set(CMAKE_CXX_STANDARD 14)

# libusb-1.0 on windows
if(DEFINED WIN32)
    set(LIBUSB10_PATH "c:/checkm8/libusb_win/" CACHE PATH "Path to libusb1-0 directory")
    # The official prebuilt static libusb-1.0.lib is compiled against the static
    # CRT (/MT). Match it so king links cleanly (avoids LNK4098) and produces a
    # self-contained exe with no runtime DLL dependency.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(SOURCE_FILES 
src/main.cpp src/dfu.cpp 
src/usbexec.cpp
src/ASN1DERElement.cpp
src/lzssdec.c
src/img4tool.cpp
src/aes.c
src/gaster.c
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
