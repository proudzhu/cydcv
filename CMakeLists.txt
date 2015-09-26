cmake_minimum_required (VERSION 2.6)
project (cydcv)
set (cydcv_VERSION_MAJOR 1)
set (cydcv_VERSION_MINOR 2)

add_executable(cydcv cydcv.c)
target_link_libraries(cydcv curl yajl readline)
