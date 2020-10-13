cmake_minimum_required(VERSION 3.5)
project(remramd_client VERSION 1.0 LANGUAGES CXX)

set(DEBUG "-g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${DEBUG}")

add_executable( remramd_client
    ./src/client_core.cpp
    ./src/remramd_client.cpp
)
