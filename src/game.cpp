/**
 * game.cpp — 游戏主循环实现
 *
 * 模式：
 *   人类模式 — 方向键/WASD 操作, Ctrl+Z 撤销
 *   AI 模式  — 按 A 切换, AI 自动操作, 速度可调(+/-)
 */

#include "game.h"
#include <chrono>
#include <cstdio>

Game::Game()
    : board_(0), score_(0), best_score_(0),
      running_(false), won_shown_(false),
      state_(GameState::Playing),
      ai_active_(false),
      ai_move_interval_(AI_SPEED_DEFAULT),
      ai_timer_(0.0f),
      rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {}

bool Game::init() {
    init_tables();

    if (!renderer_.init("2048", 500, 730)) {
        return false;
    }
    new_game();
    return true;
}

void Game::new_game() {
    board_ = 0;
    score_ = 0;
    won_shown_ = false;
    state_ = GameState::Playing;
    history_.clear();
    ai_timer_ = 0.0f;

    board_ = spawn_tile(board_, rng_);
    board_ = spawn_tile(board_, rng_);

    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (get_cell(board_, r, c) != 0)
                renderer_.start_spawn_anim(r, c, get_cell(board_, r, c));
}

void Game::run() {
    running_ = true;
    auto last_time = std::chrono::high_resolution_clock::now();

    while (running_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running_ = false;
                break;
            }
            handle_input(e);
        }

        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - last_time).count();
        last_time = now;

        renderer_.update_anims(dt);

        // AI 自动操作
        if (ai_active_ && state_ == GameState::Playing && !renderer_.animating()) {
            ai_timer_ += dt;
            if (ai_timer_ >= ai_move_interval_) {
                ai_timer_ = 0.0f;
                ai_step();
            }
        }

        // 渲染（传入 AI 状态用于 HUD 显示）
        renderer_.render(board_, score_, best_score_, state_,
                         ai_active_, ai_move_interval_);

        SDL_Delay(1);
    }
}

void Game::shutdown() {
    renderer_.shutdown();
}

void Game::handle_input(SDL_Event& e) {
    if (e.type == SDL_KEYDOWN) {
        SDL_Keymod mod = SDL_GetModState();

        // 全局快捷键（任何状态下都可用）
        switch (e.key.keysym.sym) {
            case SDLK_ESCAPE:
                running_ = false;
                return;
            case SDLK_n:
                new_game();
                return;
            case SDLK_a:
                ai_active_ = !ai_active_;
                ai_timer_ = 0.0f;
                return;
            case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS:
                // 加速 AI（减小间隔）
                ai_move_interval_ = std::max(AI_SPEED_MIN, ai_move_interval_ * 0.7f);
                return;
            case SDLK_MINUS: case SDLK_KP_MINUS:
                // 减速 AI（增大间隔）
                ai_move_interval_ = std::min(AI_SPEED_MAX, ai_move_interval_ * 1.4f);
                return;
            case SDLK_z:
                if (mod & KMOD_CTRL) { undo(); return; }
                break;
            case SDLK_u:
                undo();
                return;
            default:
                break;
        }

        // AI 模式下忽略方向键
        if (ai_active_) return;

        if (state_ == GameState::GameOver) {
            if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_SPACE)
                new_game();
            return;
        }
        if (state_ == GameState::Won) {
            if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_SPACE) {
                state_ = GameState::Playing;
                won_shown_ = true;
            }
            return;
        }

        if (renderer_.animating()) return;

        int dir = -1;
        switch (e.key.keysym.sym) {
            case SDLK_UP:    case SDLK_w: dir = UP;    break;
            case SDLK_DOWN:  case SDLK_s: dir = DOWN;  break;
            case SDLK_LEFT:  dir = LEFT;  break;
            case SDLK_RIGHT: case SDLK_d: dir = RIGHT; break;
            default: return;
        }
        if (dir >= 0) do_move(dir);
    }
    else if (e.type == SDL_MOUSEBUTTONDOWN) {
        int mx = e.button.x, my = e.button.y;

        if (renderer_.is_new_game_clicked(mx, my)) {
            new_game();
            return;
        }
        if (renderer_.is_undo_clicked(mx, my)) {
            undo();
            return;
        }
        if (renderer_.is_ai_toggle_clicked(mx, my)) {
            ai_active_ = !ai_active_;
            ai_timer_ = 0.0f;
            return;
        }
        if (renderer_.is_speed_up_clicked(mx, my)) {
            ai_move_interval_ = std::max(AI_SPEED_MIN, ai_move_interval_ * 0.7f);
            return;
        }
        if (renderer_.is_speed_down_clicked(mx, my)) {
            ai_move_interval_ = std::min(AI_SPEED_MAX, ai_move_interval_ * 1.4f);
            return;
        }
        if (state_ == GameState::Won && renderer_.is_continue_clicked(mx, my)) {
            state_ = GameState::Playing;
            won_shown_ = true;
        }
    }
}

void Game::do_move(int direction) {
    board_t moved = execute_move(direction, board_);
    if (moved == board_) return;

    // 保存撤销快照
    if ((int)history_.size() >= MAX_UNDO)
        history_.erase(history_.begin());
    history_.push_back({board_, score_});

    score_ += get_move_score(board_, direction);
    if (score_ > best_score_) best_score_ = score_;

    renderer_.start_move_anim(board_, moved, direction);

    board_t old = moved;
    moved = spawn_tile(moved, rng_);
    board_ = moved;

    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (get_cell(old, r, c) == 0 && get_cell(moved, r, c) != 0)
                renderer_.start_spawn_anim(r, c, get_cell(moved, r, c));

    check_game_state();
}

void Game::undo() {
    if (history_.empty()) return;
    auto snap = history_.back();
    history_.pop_back();
    board_ = snap.board;
    score_ = snap.score;
    state_ = GameState::Playing;
    // 不播放动画，直接切换
}

void Game::ai_step() {
    if (is_game_over(board_)) {
        check_game_state();
        return;
    }
    int dir = ai_engine_.find_best_move(board_);
    if (dir >= 0) {
        do_move(dir);
    }
}

void Game::check_game_state() {
    if (!won_shown_ && max_tile(board_) >= 2048) {
        state_ = GameState::Won;
        if (ai_active_) {
            // AI 模式下自动继续
            state_ = GameState::Playing;
            won_shown_ = true;
        }
        return;
    }
    if (is_game_over(board_)) {
        state_ = GameState::GameOver;
        ai_active_ = false; // 自动关闭 AI
    }
}
