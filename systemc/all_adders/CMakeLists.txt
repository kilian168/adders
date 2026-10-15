#******************************************************************************
# Architecture-only adders for the Intel SystemC Compiler (ICSC).
# Not validated against the actual tool (not built in this environment) --
# written to follow ICSC's documented synthesizable-subset rules; expect
# to need minor fixes on first real run.
#******************************************************************************
cmake_minimum_required(VERSION 3.12)
enable_testing()

if(NOT DEFINED ENV{ICSC_HOME})
  message("ICSC_HOME is not defined!")
  return()
endif()

project(all_adders)

find_package(SVC REQUIRED)
set(CMAKE_CXX_STANDARD 20)

add_subdirectory(2bit)
add_subdirectory(4bit)
add_subdirectory(8bit)
add_subdirectory(12bit)
add_subdirectory(16bit)
add_subdirectory(32bit)
