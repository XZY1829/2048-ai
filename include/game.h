#pragma once
/**
 * game.h — 游戏逻辑控制器
 *
 * 功能：人类操作 + AI 接管 + 撤销
 */

#include "board.h"
#include "tables.h"
#include "ai.h"
#include "renderer.h"
#include <random>
#include <vector>

struct Snapshot {
    board_t board;
    int score;
};

class Game {
public:
    Game();

    bool init();
    void run();
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

    // 撤销历史
    std::vector<Snapshot> history_;
    static constexpr int MAX_UNDO = 64;

    // AI 接管
    bool  ai_active_;         // AI 是否在控制
    float ai_move_interval_;  // AI 两步之间的间隔(秒)
    float ai_timer_;          // 累计计时器
    static constexpr float AI_SPEED_MIN = 0.01f;  // 最快 10ms/步
    static constexpr float AI_SPEED_MAX = 1.0f;   // 最慢 1s/步
    static constexpr float AI_SPEED_DEFAULT = 0.3f;

    void new_game();
    void handle_input(SDL_Event& e);
    void do_move(int direction);
    void undo();
    void ai_step();
    void check_game_state();
};
