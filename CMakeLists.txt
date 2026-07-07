cmake_minimum_required(VERSION 3.16)
project(Game2048 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 生成 compile_commands.json 供 clangd/VSCode 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 源文件
set(SOURCES
    src/main.cpp
    src/tables.cpp
    src/ai.cpp
)

# 头文件搜索路径
include_directories(include)

# 可执行文件
add_executable(game2048 ${SOURCES})

# 优化选项（Release 模式）
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(game2048 PRIVATE -O3)
endif()

# 静态链接（避免运行时 DLL 依赖）
target_link_options(game2048 PRIVATE -static)
