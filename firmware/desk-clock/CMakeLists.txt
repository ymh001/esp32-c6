cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(desk-clock)

include(cmake/fix_ws_buffered_read.cmake)
