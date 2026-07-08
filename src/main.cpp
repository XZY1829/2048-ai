/**
 * main.cpp — 程序入口
 */

#include "game.h"
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __EMSCRIPTEN__
static Game* g_game = nullptr;

static void em_main_loop() {
    if (g_game) g_game->tick();
}
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

#ifdef __EMSCRIPTEN__
    static Game game;
    if (!game.init()) {
        fprintf(stderr, "Game initialization failed!\n");
        EM_ASM({
            var el = document.getElementById('loading');
            if (el) {
                el.innerHTML = '<p style="color:red;text-align:center">Init failed.<br>Check browser console (F12).</p>';
                el.style.display = 'flex';
            }
        });
        return 1;
    }
    g_game = &game;
    emscripten_set_main_loop(em_main_loop, 0, 1);
#else
    Game game;
    if (!game.init()) {
        fprintf(stderr, "Game initialization failed!\n");
        return 1;
    }
    game.run();
    game.shutdown();
#endif
    return 0;
}
