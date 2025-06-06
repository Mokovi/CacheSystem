project(CacheSystem)
cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 17)
include_directories(${CMAKE_SOURCE_DIR}/include)
file(GLOB SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/source/*.cpp
)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin)
add_executable(app ${SOURCE_FILES})