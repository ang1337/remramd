cmake_minimum_required(VERSION 3.5)
project(remramd_client VERSION 1.0 LANGUAGES CXX)

set(THREADLIB "-lpthread")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${THREADLIB}")

add_executable( remramd_server
    ./src/remramd_server.cpp
    ./src/remramd_server_main.cpp
)
