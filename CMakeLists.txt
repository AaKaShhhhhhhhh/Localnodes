cmake_minimum_required(VERSION 3.16)

# Force CMake to explicitly search the main directory
set(EXTRA_COMPONENT_DIRS "main")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Localnodes)