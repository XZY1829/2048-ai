/**
 * game.cpp — 游戏主循环实现
 *
 * 跨平台：Native 用 while 循环，Emscripten 用 emscripten_set_main_loop
 */

#include "game.h"
#include <cstdio>
#include <cmath>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

Game::Game()
    : board_(0), score_(0), best_score_(0),
      running_(false), won_shown_(false),
      state_(GameState::Playing),
      ai_active_(false),
      ai_move_interval_(AI_SPEED_DEFAULT),
      ai_timer_(0.0f),
      touch_start_x_(0), touch_start_y_(0), touch_active_(false),
      rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {}

bool Game::init() {
    init_tables();

    int w = 500, h = 730;
#ifdef __EMSCRIPTEN__
    w = EM_ASM_INT({ return Math.max(window.innerWidth, 320); });
    h = EM_ASM_INT({ return Math.max(window.innerHeight, 480); });
#endif

    if (!renderer_.init("2048", w, h)) {
#ifdef __EMSCRIPTEN__
        EM_ASM({
            var el = document.getElementById('loading');
            if (el) {
                el.innerHTML = '<p style="color:red;font-size:18px;">Game init failed. Check console.</p>';
                el.style.display = 'flex';
            }
        });
#endif
        return false;
    }

#ifdef __EMSCRIPTEN__
    best_score_ = EM_ASM_INT({
        var v = localStorage.getItem("best_score_2048");
        return v ? parseInt(v) : 0;
    });
#endif

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

void Game::tick() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            running_ = false;
            break;
        }
        handle_input(e);
    }

    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(now - last_time_).count();
    last_time_ = now;
    if (dt > 0.1f) dt = 0.016f; // 防止首帧跳帧

    renderer_.update_anims(dt);

    if (ai_active_ && state_ == GameState::Playing && !renderer_.animating()) {
        ai_timer_ += dt;
        if (ai_timer_ >= ai_move_interval_) {
            ai_timer_ = 0.0f;
            ai_step();
        }
    }

#ifdef __EMSCRIPTEN__
    // 响应式：检测窗口尺寸变化
    int new_w = EM_ASM_INT({ return window.innerWidth; });
    int new_h = EM_ASM_INT({ return window.innerHeight; });
    renderer_.check_resize(new_w, new_h);
#endif

    renderer_.render(board_, score_, best_score_, state_,
                     ai_active_, ai_move_interval_);
}

void Game::run() {
    running_ = true;
    last_time_ = std::chrono::high_resolution_clock::now();

#ifndef __EMSCRIPTEN__
    while (running_) {
        tick();
        SDL_Delay(1);
    }
#endif
}

void Game::shutdown() {
    renderer_.shutdown();
}

void Game::handle_input(SDL_Event& e) {
    if (e.type == SDL_KEYDOWN) {
        SDL_Keymod mod = SDL_GetModState();

        switch (e.key.keysym.sym) {
            case SDLK_ESCAPE: running_ = false; return;
            case SDLK_n: new_game(); return;
            case SDLK_a:
                ai_active_ = !ai_active_;
                ai_timer_ = 0.0f;
                return;
            case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS:
                ai_move_interval_ = std::max(AI_SPEED_MIN, ai_move_interval_ * 0.7f);
                return;
            case SDLK_MINUS: case SDLK_KP_MINUS:
                ai_move_interval_ = std::min(AI_SPEED_MAX, ai_move_interval_ * 1.4f);
                return;
            case SDLK_z:
                if (mod & KMOD_CTRL) { undo(); return; }
                break;
            case SDLK_u: undo(); return;
            default: break;
        }

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
    // 触控手势
    else if (e.type == SDL_FINGERDOWN) {
        touch_start_x_ = e.tfinger.x;
        touch_start_y_ = e.tfinger.y;
        touch_active_ = true;
    }
    else if (e.type == SDL_FINGERUP && touch_active_) {
        touch_active_ = false;
        if (state_ != GameState::Playing && !ai_active_) return;
        if (renderer_.animating()) return;

        float dx = e.tfinger.x - touch_start_x_;
        float dy = e.tfinger.y - touch_start_y_;
        float threshold = 0.03f;

        if (std::fabs(dx) > threshold || std::fabs(dy) > threshold) {
            int dir;
            if (std::fabs(dx) > std::fabs(dy))
                dir = dx > 0 ? RIGHT : LEFT;
            else
                dir = dy > 0 ? DOWN : UP;
            do_move(dir);
        }
    }
    else if (e.type == SDL_MOUSEBUTTONDOWN) {
        int mx = e.button.x, my = e.button.y;

        if (renderer_.is_new_game_clicked(mx, my)) { new_game(); return; }
        if (renderer_.is_undo_clicked(mx, my)) { undo(); return; }
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

    if ((int)history_.size() >= MAX_UNDO)
        history_.erase(history_.begin());
    history_.push_back({board_, score_});

    score_ += get_move_score(board_, direction);
    if (score_ > best_score_) {
        best_score_ = score_;
#ifdef __EMSCRIPTEN__
        EM_ASM_({ localStorage.setItem("best_score_2048", $0.toString()); }, best_score_);
#endif
    }

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
}

void Game::ai_step() {
    if (is_game_over(board_)) { check_game_state(); return; }
    int dir = ai_engine_.find_best_move(board_);
    if (dir >= 0) do_move(dir);
}

void Game::check_game_state() {
    if (!won_shown_ && max_tile(board_) >= 2048) {
        state_ = GameState::Won;
        if (ai_active_) { state_ = GameState::Playing; won_shown_ = true; }
        return;
    }
    if (is_game_over(board_)) {
        state_ = GameState::GameOver;
        ai_active_ = false;
    }
}
