#pragma once
/**
 * game.h — 游戏逻辑控制器
 *
 * 功能：人类操作 + AI 接管 + 撤销 + 触控
 */

#include "board.h"
#include "tables.h"
#include "ai.h"
#include "renderer.h"
#include <random>
#include <vector>
#include <chrono>

struct Snapshot {
    board_t board;
    int score;
};

class Game {
public:
    Game();

    bool init();
    void run();
    void tick();  // 单帧逻辑（Emscripten 需要）
    void shutdown();

private:
    Renderer renderer_;
    AIEngine ai_engine_;
    board_t  board_;
    int      score_;
    int      best_score_;
    bool     running_;
    bool     won_shown_;
    GameState state_;

    std::mt19937 rng_;
    std::chrono::high_resolution_clock::time_point last_time_;

    // 撤销历史
    std::vector<Snapshot> history_;
    static constexpr int MAX_UNDO = 64;

    // AI 接管
    bool  ai_active_;
    float ai_move_interval_;
    float ai_timer_;
    static constexpr float AI_SPEED_MIN = 0.01f;
    static constexpr float AI_SPEED_MAX = 1.0f;
    static constexpr float AI_SPEED_DEFAULT = 0.2f;

    // 触控状态
    float touch_start_x_;
    float touch_start_y_;
    bool  touch_active_;

    void new_game();
    void handle_input(SDL_Event& e);
    void do_move(int direction);
    void undo();
    void ai_step();
    void check_game_state();
};
