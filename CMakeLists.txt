cmake_minimum_required(VERSION 3.10)
project(king)
set(CMAKE_CXX_STANDARD 14)

# libusb-1.0 on windows
if(DEFINED WIN32)
	SET(LIBUSB10_PATH "c:/checkm8/libusb_win/" CACHE PATH "Path to libusb1-0 directory")
endif()

set(SOURCE_FILES src/main.cpp src/dfu.cpp src/usbexec.cpp src/GdbStub.cpp)

add_executable(king ${SOURCE_FILES})

target_include_directories(king PRIVATE include)

if(DEFINED WIN32) 
	target_include_directories(king PRIVATE ${LIBUSB10_PATH}/include)
	target_link_directories(king PRIVATE ${LIBUSB10_PATH}/lib)
	TARGET_LINK_LIBRARIES(king libusb-1.0 ws2_32.lib)
else()
	TARGET_LINK_LIBRARIES(king usb-1.0)
endif()