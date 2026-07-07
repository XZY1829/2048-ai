/**
 * renderer.cpp — SDL2 渲染实现
 */

#include "renderer.h"
#include <cstdio>
#include <algorithm>

// ============================================================
// 辅助
// ============================================================

static void set_color(SDL_Renderer* r, Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

// ============================================================
// 缓动函数
// ============================================================

static float ease_out_cubic(float t) {
    float f = 1.0f - t;
    return 1.0f - f * f * f;
}

static float ease_out_back(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
}

// ============================================================
// 初始化 / 销毁
// ============================================================

Renderer::Renderer() {}

Renderer::~Renderer() { shutdown(); }

bool Renderer::init(const char* title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return false;
    }

    win_w_ = width;
    win_h_ = height;

    window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) return false;

    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) return false;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // 加载字体（使用 Windows 自带的 Segoe UI）
    const char* font_paths[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/msyh.ttc",
    };
    for (auto path : font_paths) {
        font_big_ = TTF_OpenFont(path, 40);
        if (font_big_) {
            font_med_ = TTF_OpenFont(path, 22);
            font_sml_ = TTF_OpenFont(path, 16);
            break;
        }
    }
    if (!font_big_) {
        fprintf(stderr, "Failed to load any font\n");
        return false;
    }
    TTF_SetFontStyle(font_big_, TTF_STYLE_BOLD);
    TTF_SetFontStyle(font_med_, TTF_STYLE_BOLD);

    calc_layout();
    return true;
}

void Renderer::shutdown() {
    if (font_sml_) { TTF_CloseFont(font_sml_); font_sml_ = nullptr; }
    if (font_med_) { TTF_CloseFont(font_med_); font_med_ = nullptr; }
    if (font_big_) { TTF_CloseFont(font_big_); font_big_ = nullptr; }
    if (renderer_) { SDL_DestroyRenderer(renderer_); renderer_ = nullptr; }
    if (window_)   { SDL_DestroyWindow(window_); window_ = nullptr; }
    TTF_Quit();
    SDL_Quit();
}

// ============================================================
// 布局计算
// ============================================================

void Renderer::calc_layout() {
    int header_h = 120;
    int toolbar_h = 50;
    int margin = 16;
    board_size_ = std::min(win_w_ - margin * 2, win_h_ - header_h - toolbar_h - margin * 3);
    board_x_ = (win_w_ - board_size_) / 2;
    board_y_ = header_h + margin;
    cell_gap_ = board_size_ / 50;
    cell_size_ = (board_size_ - cell_gap_ * 5) / 4;

    // Header 按钮
    new_game_btn_ = {win_w_ - 130 - margin, 60, 130, 40};
    continue_btn_ = {(win_w_ - 160) / 2, (win_h_) / 2 + 20, 160, 45};

    // 工具栏按钮（棋盘下方）
    int toolbar_y = board_y_ + board_size_ + 12;
    int btn_h = 36;
    int btn_gap = 8;

    // 从左到右：Undo | AI Toggle | Speed- | Speed+
    undo_btn_       = {board_x_, toolbar_y, 80, btn_h};
    ai_toggle_btn_  = {board_x_ + 88, toolbar_y, 100, btn_h};
    speed_down_btn_ = {board_x_ + 196, toolbar_y, 50, btn_h};
    speed_up_btn_   = {board_x_ + 254, toolbar_y, 50, btn_h};
}

SDL_Rect Renderer::cell_rect(int row, int col) const {
    int x = board_x_ + cell_gap_ + col * (cell_size_ + cell_gap_);
    int y = board_y_ + cell_gap_ + row * (cell_size_ + cell_gap_);
    return {x, y, cell_size_, cell_size_};
}

SDL_Rect Renderer::cell_rect_f(float row, float col) const {
    float x = board_x_ + cell_gap_ + col * (cell_size_ + cell_gap_);
    float y = board_y_ + cell_gap_ + row * (cell_size_ + cell_gap_);
    return {(int)x, (int)y, cell_size_, cell_size_};
}

// ============================================================
// 绘制原语
// ============================================================

void Renderer::draw_rounded_rect(SDL_Rect rect, int radius, Color c) {
    set_color(renderer_, c);
    // 简化实现：先画中心矩形，再画四边，四角用小圆填充
    // 对于游戏这种场景，直接填充矩形即可（圆角视觉差异小）
    SDL_RenderFillRect(renderer_, &rect);
}

void Renderer::draw_text_centered(const char* text, SDL_Rect area, TTF_Font* font, Color c) {
    if (!text || !text[0]) return;
    SDL_Color sdl_c = {c.r, c.g, c.b, c.a};
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text, sdl_c);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    if (!tex) { SDL_FreeSurface(surf); return; }

    SDL_Rect dst;
    dst.w = surf->w;
    dst.h = surf->h;
    dst.x = area.x + (area.w - dst.w) / 2;
    dst.y = area.y + (area.h - dst.h) / 2;

    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

// ============================================================
// 绘制方块
// ============================================================

void Renderer::draw_tile(int row, int col, int exponent, float scale, float alpha) {
    SDL_Rect base = cell_rect(row, col);

    if (scale != 1.0f) {
        int dw = (int)(base.w * (1.0f - scale) / 2);
        int dh = (int)(base.h * (1.0f - scale) / 2);
        base.x += dw; base.y += dh;
        base.w -= dw * 2; base.h -= dh * 2;
    }

    Color bg = tile_bg_color(exponent);
    bg.a = (uint8_t)(alpha * 255);
    draw_rounded_rect(base, 6, bg);

    if (exponent > 0) {
        int value = 1 << exponent;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", value);

        // 根据数字位数选字体大小
        TTF_Font* font = font_big_;
        if (value >= 1000) font = font_med_;
        if (value >= 10000) font = font_sml_;

        Color tc = tile_text_color(exponent);
        tc.a = (uint8_t)(alpha * 255);
        draw_text_centered(buf, base, font, tc);
    }
}

// ============================================================
// 绘制 Header（标题 + 分数）
// ============================================================

void Renderer::draw_header(int score, int best_score) {
    int margin = 16;

    // 标题 "2048"
    SDL_Rect title_area = {margin, 15, 120, 50};
    draw_text_centered("2048", title_area, font_big_, UIColor::header_text);

    // 分数框
    SDL_Rect score_box = {win_w_ - 280 - margin, 10, 130, 55};
    draw_rounded_rect(score_box, 4, UIColor::score_bg);
    SDL_Rect score_label = {score_box.x, score_box.y + 5, score_box.w, 18};
    draw_text_centered("SCORE", score_label, font_sml_, {238, 228, 218, 255});
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", score);
    SDL_Rect score_val = {score_box.x, score_box.y + 22, score_box.w, 28};
    draw_text_centered(buf, score_val, font_med_, UIColor::score_text);

    // 最高分框
    SDL_Rect best_box = {win_w_ - 140 - margin, 10, 130, 55};
    draw_rounded_rect(best_box, 4, UIColor::score_bg);
    SDL_Rect best_label = {best_box.x, best_box.y + 5, best_box.w, 18};
    draw_text_centered("BEST", best_label, font_sml_, {238, 228, 218, 255});
    snprintf(buf, sizeof(buf), "%d", best_score);
    SDL_Rect best_val = {best_box.x, best_box.y + 22, best_box.w, 28};
    draw_text_centered(buf, best_val, font_med_, UIColor::score_text);

    // New Game 按钮
    draw_rounded_rect(new_game_btn_, 4, UIColor::button_bg);
    draw_text_centered("New Game", new_game_btn_, font_sml_, UIColor::button_text);
}

// ============================================================
// 绘制棋盘底板
// ============================================================

void Renderer::draw_board_background() {
    SDL_Rect board_rect = {board_x_, board_y_, board_size_, board_size_};
    draw_rounded_rect(board_rect, 8, UIColor::board_bg);

    // 空格子底色
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            draw_rounded_rect(cell_rect(r, c), 4, tile_bg_color(0));
        }
    }
}

// ============================================================
// 游戏结束/胜利遮罩
// ============================================================

void Renderer::draw_overlay(GameState state) {
    if (state == GameState::Playing) return;

    SDL_Rect board_rect = {board_x_, board_y_, board_size_, board_size_};
    draw_rounded_rect(board_rect, 8, UIColor::overlay);

    const char* msg = (state == GameState::Won) ? "You Win!" : "Game Over!";
    SDL_Rect msg_area = {board_x_, board_y_ + board_size_ / 3, board_size_, 50};
    draw_text_centered(msg, msg_area, font_big_, UIColor::gameover_text);

    if (state == GameState::Won) {
        draw_rounded_rect(continue_btn_, 4, UIColor::button_bg);
        draw_text_centered("Keep Going", continue_btn_, font_sml_, UIColor::button_text);
    }
}

// ============================================================
// 主渲染
// ============================================================

void Renderer::render(board_t board, int score, int best_score, GameState state,
                      bool ai_active, float ai_speed) {
    set_color(renderer_, UIColor::background);
    SDL_RenderClear(renderer_);

    draw_header(score, best_score);
    draw_board_background();

    // === 动画中：渲染动画帧 ===
    if (animating()) {
        // 1) 画不参与动画的静态方块（目标板上存在，但不是动画目标位置的）
        bool animated_pos[4][4] = {};
        for (auto& a : tile_anims_) {
            // 标记动画覆盖的起始和目标位置
            int fr = (int)a.from_row, fc = (int)a.from_col;
            int tr = (int)a.to_row, tc = (int)a.to_col;
            if (fr >= 0 && fr < 4 && fc >= 0 && fc < 4) animated_pos[fr][fc] = true;
            if (tr >= 0 && tr < 4 && tc >= 0 && tc < 4) animated_pos[tr][tc] = true;
        }
        for (auto& m : merge_anims_) {
            if (m.row >= 0 && m.row < 4 && m.col >= 0 && m.col < 4)
                animated_pos[m.row][m.col] = true;
        }
        for (auto& s : spawn_anims_) {
            if (s.row >= 0 && s.row < 4 && s.col >= 0 && s.col < 4)
                animated_pos[s.row][s.col] = true;
        }
        // 绘制不被动画覆盖的静态方块
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                if (animated_pos[r][c]) continue;
                int exp = get_cell(board, r, c);
                if (exp > 0) draw_tile(r, c, exp);
            }
        }

        // 2) 画滑动中的方块
        for (auto& a : tile_anims_) {
            float t = std::min(1.0f, a.elapsed / a.duration);
            float eased = ease_out_cubic(t);
            float r = a.from_row + (a.to_row - a.from_row) * eased;
            float c = a.from_col + (a.to_col - a.from_col) * eased;
            SDL_Rect rect = cell_rect_f(r, c);
            Color bg = tile_bg_color(a.exponent);
            draw_rounded_rect(rect, 4, bg);
            if (a.exponent > 0) {
                int value = 1 << a.exponent;
                char buf[16]; snprintf(buf, sizeof(buf), "%d", value);
                TTF_Font* font = font_big_;
                if (value >= 1000) font = font_med_;
                if (value >= 10000) font = font_sml_;
                draw_text_centered(buf, rect, font, tile_text_color(a.exponent));
            }
        }

        // 3) 合并弹跳（延迟后才启动）
        for (auto& m : merge_anims_) {
            float local = m.elapsed - m.delay;
            if (local < 0) continue;
            float t = std::min(1.0f, local / m.duration);
            float scale = 1.0f + 0.25f * ease_out_back(t) * (1.0f - t);
            draw_tile(m.row, m.col, m.exponent, scale);
        }

        // 4) 新生方块渐显（延迟后才启动）
        for (auto& s : spawn_anims_) {
            float local = s.elapsed - s.delay;
            if (local < 0) continue;
            float t = std::min(1.0f, local / s.duration);
            float eased = ease_out_cubic(t);
            float scale = 0.0f + 1.0f * eased;
            float alpha = eased;
            draw_tile(s.row, s.col, s.exponent, scale, alpha);
        }
    } else {
        // === 静态：直接绘制当前棋盘 ===
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                int exp = get_cell(board, r, c);
                if (exp > 0) draw_tile(r, c, exp);
            }
        }
    }

    draw_overlay(state);

    // 底部工具栏
    draw_toolbar(ai_active, ai_speed);

    SDL_RenderPresent(renderer_);
}

// ============================================================
// 动画控制
// ============================================================

// 模拟单行左移，返回每个 tile 的目标列位置（-1 表示被合并消失）
static void trace_row_left(int tiles[4], int dest_col[4], int merged_at[4]) {
    for (int i = 0; i < 4; i++) { dest_col[i] = -1; merged_at[i] = -1; }

    int write = 0;
    bool just_merged = false;
    for (int i = 0; i < 4; i++) {
        if (tiles[i] == 0) continue;
        if (write > 0 && !just_merged && tiles[i] == (dest_col[write-1] >= 0 ? tiles[i] : 0)) {
            // 找上一个未合并的 tile
        }
        dest_col[i] = write;

        // 检查是否能与前一个合并
        if (write > 0) {
            // 找到写入位置 write-1 上的 tile
            int prev_src = -1;
            for (int j = i - 1; j >= 0; j--) {
                if (dest_col[j] == write - 1 && merged_at[j] == -1) {
                    prev_src = j;
                    break;
                }
            }
            if (prev_src >= 0 && tiles[prev_src] == tiles[i] && !just_merged) {
                dest_col[i] = write - 1;
                merged_at[i] = write - 1;
                merged_at[prev_src] = write - 1; // 标记前一个也参与合并
                just_merged = true;
                continue;
            }
        }
        just_merged = false;
        write++;
    }
}

void Renderer::start_move_anim(board_t before, board_t after, int direction) {
    tile_anims_.clear();
    merge_anims_.clear();

    const float SLIDE_DURATION = 0.12f;  // 滑动时长 120ms
    const float MERGE_DURATION = 0.15f;  // 合并弹跳时长
    const float MERGE_DELAY    = 0.08f;  // 合并延迟（等滑动快结束时启动）

    // 对每行/列追踪 tile 的滑动路径
    for (int line = 0; line < 4; line++) {
        int tiles[4];
        int dest[4], merged[4];

        // 根据方向提取行/列
        switch (direction) {
            case 2: // LEFT
                for (int i = 0; i < 4; i++) tiles[i] = get_cell(before, line, i);
                trace_row_left(tiles, dest, merged);
                for (int i = 0; i < 4; i++) {
                    if (tiles[i] == 0) continue;
                    if (dest[i] == i && merged[i] < 0) continue; // 未移动
                    int target_col = (dest[i] >= 0) ? dest[i] : i;
                    tile_anims_.push_back({(float)line, (float)i,
                                           (float)line, (float)target_col,
                                           tiles[i], 0.0f, SLIDE_DURATION});
                    if (merged[i] >= 0) {
                        merge_anims_.push_back({line, merged[i],
                                                tiles[i] + 1, 0.0f, MERGE_DURATION, MERGE_DELAY});
                    }
                }
                break;
            case 3: // RIGHT
                for (int i = 0; i < 4; i++) tiles[i] = get_cell(before, line, 3 - i);
                trace_row_left(tiles, dest, merged);
                for (int i = 0; i < 4; i++) {
                    if (tiles[i] == 0) continue;
                    int orig_col = 3 - i;
                    if (dest[i] == i && merged[i] < 0) continue;
                    int target_col = (dest[i] >= 0) ? 3 - dest[i] : orig_col;
                    tile_anims_.push_back({(float)line, (float)orig_col,
                                           (float)line, (float)target_col,
                                           tiles[i], 0.0f, SLIDE_DURATION});
                    if (merged[i] >= 0) {
                        merge_anims_.push_back({line, 3 - merged[i],
                                                tiles[i] + 1, 0.0f, MERGE_DURATION, MERGE_DELAY});
                    }
                }
                break;
            case 0: // UP
                for (int i = 0; i < 4; i++) tiles[i] = get_cell(before, i, line);
                trace_row_left(tiles, dest, merged);
                for (int i = 0; i < 4; i++) {
                    if (tiles[i] == 0) continue;
                    if (dest[i] == i && merged[i] < 0) continue;
                    int target_row = (dest[i] >= 0) ? dest[i] : i;
                    tile_anims_.push_back({(float)i, (float)line,
                                           (float)target_row, (float)line,
                                           tiles[i], 0.0f, SLIDE_DURATION});
                    if (merged[i] >= 0) {
                        merge_anims_.push_back({merged[i], line,
                                                tiles[i] + 1, 0.0f, MERGE_DURATION, MERGE_DELAY});
                    }
                }
                break;
            case 1: // DOWN
                for (int i = 0; i < 4; i++) tiles[i] = get_cell(before, 3 - i, line);
                trace_row_left(tiles, dest, merged);
                for (int i = 0; i < 4; i++) {
                    if (tiles[i] == 0) continue;
                    int orig_row = 3 - i;
                    if (dest[i] == i && merged[i] < 0) continue;
                    int target_row = (dest[i] >= 0) ? 3 - dest[i] : orig_row;
                    tile_anims_.push_back({(float)orig_row, (float)line,
                                           (float)target_row, (float)line,
                                           tiles[i], 0.0f, SLIDE_DURATION});
                    if (merged[i] >= 0) {
                        merge_anims_.push_back({3 - merged[i], line,
                                                tiles[i] + 1, 0.0f, MERGE_DURATION, MERGE_DELAY});
                    }
                }
                break;
        }
    }

    // 去重合并动画（同一位置可能被两个源 tile 标记）
    std::vector<MergeAnim> unique_merges;
    for (auto& m : merge_anims_) {
        bool dup = false;
        for (auto& u : unique_merges)
            if (u.row == m.row && u.col == m.col) { dup = true; break; }
        if (!dup) unique_merges.push_back(m);
    }
    merge_anims_ = unique_merges;
}

void Renderer::start_spawn_anim(int row, int col, int exponent) {
    const float SPAWN_DURATION = 0.12f;
    const float SPAWN_DELAY    = 0.10f; // 等滑动基本完成后再出现
    spawn_anims_.push_back({row, col, exponent, 0.0f, SPAWN_DURATION, SPAWN_DELAY});
}

bool Renderer::animating() const {
    return !tile_anims_.empty() || !merge_anims_.empty() || !spawn_anims_.empty();
}

void Renderer::update_anims(float dt) {
    for (auto& a : tile_anims_) a.elapsed += dt;
    for (auto& m : merge_anims_) m.elapsed += dt;
    for (auto& s : spawn_anims_) s.elapsed += dt;

    // 移除完成的动画
    tile_anims_.erase(
        std::remove_if(tile_anims_.begin(), tile_anims_.end(),
                       [](const TileAnim& a) { return a.elapsed >= a.duration; }),
        tile_anims_.end());
    merge_anims_.erase(
        std::remove_if(merge_anims_.begin(), merge_anims_.end(),
                       [](const MergeAnim& m) { return m.elapsed >= m.delay + m.duration; }),
        merge_anims_.end());
    spawn_anims_.erase(
        std::remove_if(spawn_anims_.begin(), spawn_anims_.end(),
                       [](const SpawnAnim& s) { return s.elapsed >= s.delay + s.duration; }),
        spawn_anims_.end());
}

// ============================================================
// 工具栏
// ============================================================

void Renderer::draw_toolbar(bool ai_active, float ai_speed) {
    // Undo 按钮
    Color undo_color = {143, 122, 102, 255};
    draw_rounded_rect(undo_btn_, 4, undo_color);
    draw_text_centered("Undo", undo_btn_, font_sml_, {255, 255, 255, 255});

    // AI 按钮（激活时绿色，否则灰色）
    Color ai_color = ai_active ? Color{60, 179, 113, 255} : Color{143, 122, 102, 255};
    draw_rounded_rect(ai_toggle_btn_, 4, ai_color);
    const char* ai_label = ai_active ? "AI: ON" : "AI: OFF";
    draw_text_centered(ai_label, ai_toggle_btn_, font_sml_, {255, 255, 255, 255});

    // Speed- 按钮
    draw_rounded_rect(speed_down_btn_, 4, {170, 160, 150, 255});
    draw_text_centered("-", speed_down_btn_, font_med_, {255, 255, 255, 255});

    // Speed+ 按钮
    draw_rounded_rect(speed_up_btn_, 4, {170, 160, 150, 255});
    draw_text_centered("+", speed_up_btn_, font_med_, {255, 255, 255, 255});

    // 速度标签
    char speed_label[32];
    int ms = (int)(ai_speed * 1000);
    snprintf(speed_label, sizeof(speed_label), "%dms", ms);
    SDL_Rect speed_area = {speed_up_btn_.x + speed_up_btn_.w + 8,
                           speed_up_btn_.y, 70, speed_up_btn_.h};
    draw_text_centered(speed_label, speed_area, font_sml_, UIColor::header_text);
}

// ============================================================
// UI 交互
// ============================================================

bool Renderer::is_new_game_clicked(int mx, int my) const {
    return mx >= new_game_btn_.x && mx <= new_game_btn_.x + new_game_btn_.w &&
           my >= new_game_btn_.y && my <= new_game_btn_.y + new_game_btn_.h;
}

bool Renderer::is_continue_clicked(int mx, int my) const {
    return mx >= continue_btn_.x && mx <= continue_btn_.x + continue_btn_.w &&
           my >= continue_btn_.y && my <= continue_btn_.y + continue_btn_.h;
}

bool Renderer::is_undo_clicked(int mx, int my) const {
    return mx >= undo_btn_.x && mx <= undo_btn_.x + undo_btn_.w &&
           my >= undo_btn_.y && my <= undo_btn_.y + undo_btn_.h;
}

bool Renderer::is_ai_toggle_clicked(int mx, int my) const {
    return mx >= ai_toggle_btn_.x && mx <= ai_toggle_btn_.x + ai_toggle_btn_.w &&
           my >= ai_toggle_btn_.y && my <= ai_toggle_btn_.y + ai_toggle_btn_.h;
}

bool Renderer::is_speed_down_clicked(int mx, int my) const {
    return mx >= speed_down_btn_.x && mx <= speed_down_btn_.x + speed_down_btn_.w &&
           my >= speed_down_btn_.y && my <= speed_down_btn_.y + speed_down_btn_.h;
}

bool Renderer::is_speed_up_clicked(int mx, int my) const {
    return mx >= speed_up_btn_.x && mx <= speed_up_btn_.x + speed_up_btn_.w &&
           my >= speed_up_btn_.y && my <= speed_up_btn_.y + speed_up_btn_.h;
}
