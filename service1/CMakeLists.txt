cmake_minimum_required(VERSION 3.16)
project(service1 LANGUAGES CXX)
add_executable(service1 src/main.cpp)
include(../common/dependencies.cmake)
