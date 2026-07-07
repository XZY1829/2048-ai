cmake_minimum_required(VERSION 3.16)
project(Game2048 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 生成 compile_commands.json 供 clangd 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找 SDL2
find_package(SDL2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)

# 源文件
set(SOURCES
    src/main.cpp
    src/tables.cpp
    src/ai.cpp
    src/renderer.cpp
    src/game.cpp
)

# 头文件搜索路径
include_directories(include)
include_directories(${SDL2_INCLUDE_DIRS})
include_directories(${SDL2_TTF_INCLUDE_DIRS})

# 可执行文件
add_executable(game2048 ${SOURCES})

# 链接库
target_link_libraries(game2048 PRIVATE
    ${SDL2_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
)

# 优化选项（Release 模式）
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(game2048 PRIVATE -O2)
endif()

# Windows: 隐藏控制台窗口（仅 Release）
if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(game2048 PRIVATE -mwindows)
endif()
