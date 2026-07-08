cmake_minimum_required(VERSION 3.16)
project(Game2048 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 头文件搜索路径
include_directories(include)

# ============================================================
# 选项: 是否构建仅训练版本（不需要 SDL）
# ============================================================
option(BUILD_TRAINER_ONLY "Build trainer-only executable without SDL" OFF)

if(BUILD_TRAINER_ONLY)
    # === 训练专用构建（无 SDL，纯 CLI）===
    set(TRAINER_SOURCES
        src/main.cpp
        src/tables.cpp
        src/ai.cpp
        src/tuple_network.cpp
        src/trainer.cpp
    )
    add_executable(game2048 ${TRAINER_SOURCES})
    target_compile_definitions(game2048 PRIVATE BUILD_TRAINER_ONLY)
    target_compile_options(game2048 PRIVATE -O2)
    
    # Windows: 不用 static 链接，避免大内存分配问题

elseif(EMSCRIPTEN)
    # === Web (Emscripten) 构建 ===
    set(SOURCES
        src/main.cpp
        src/tables.cpp
        src/ai.cpp
        src/tuple_network.cpp
        src/trainer.cpp
        src/renderer.cpp
        src/game.cpp
    )
    add_executable(game2048 ${SOURCES})

    set(USE_FLAGS "-sUSE_SDL=2 -sUSE_SDL_TTF=2")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${USE_FLAGS}")
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    target_link_options(game2048 PRIVATE
        "SHELL:-sUSE_SDL=2"
        "SHELL:-sUSE_SDL_TTF=2"
        "SHELL:-sINITIAL_MEMORY=67108864"
        "SHELL:-sSTACK_SIZE=1048576"
        "SHELL:-sINVOKE_RUN=1"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@/assets"
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html"
    )
    target_compile_options(game2048 PRIVATE -O2)

else()
    # === Native (Desktop) 完整构建（含 SDL + 训练）===
    set(SOURCES
        src/main.cpp
        src/tables.cpp
        src/ai.cpp
        src/tuple_network.cpp
        src/trainer.cpp
        src/renderer.cpp
        src/game.cpp
    )
    add_executable(game2048 ${SOURCES})

    find_package(SDL2 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)

    include_directories(${SDL2_INCLUDE_DIRS})
    include_directories(${SDL2_TTF_INCLUDE_DIRS})

    target_link_libraries(game2048 PRIVATE
        ${SDL2_LIBRARIES}
        ${SDL2_TTF_LIBRARIES}
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(game2048 PRIVATE -O2)
        if(WIN32)
            target_link_options(game2048 PRIVATE -mwindows)
        endif()
    endif()
endif()
