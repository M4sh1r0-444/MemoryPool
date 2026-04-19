cmake_minimum_required(VERSION 4.1)
project(MemoryPool)

set(CMAKE_CXX_STANDARD 11)

add_executable(MemoryPool main.cpp
        memoryPool.cpp
        memoryPool.h)
