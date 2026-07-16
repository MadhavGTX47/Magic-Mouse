cmake_minimum_required(VERSION 3.10)
project(MagicMouseServer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    src/main.cpp
    src/InputController.h
    src/InputController.cpp
)

add_executable(MagicMouseServer ${SOURCES})

# Links for Windows Sockets (Winsock2) and Multimedia (for Volume Control if needed)
if(WIN32)
    target_link_libraries(MagicMouseServer ws2_32 winmm)
endif()
