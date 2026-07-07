/**
 * main.cpp — 程序入口
 *
 * 启动 SDL2 图形界面的 2048 游戏
 *
 * 操作方式:
 *   方向键/WASD: 移动方块
 *   N: 新游戏
 *   ESC: 退出
 */

#include "game.h"
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    Game game;
    if (!game.init()) {
        fprintf(stderr, "Game initialization failed!\n");
        return 1;
    }

    game.run();
    game.shutdown();
    return 0;
}
