# 项目基本设置
project(CacheSystem)
cmake_minimum_required(VERSION 3.10)  # 要求CMake最低版本为3.10

# 设置C++标准为17，并强制要求
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # 禁用编译器特定扩展

# 查找线程库（pthread等）
find_package(Threads REQUIRED)

# 设置包含目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 设置源文件目录
file(GLOB SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/source/*.cpp
)

# 设置可执行文件输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin)

# 创建可执行文件
add_executable(app ${SOURCE_FILES})

# 链接线程库（pthread等）到可执行文件
target_link_libraries(app PRIVATE Threads::Threads)