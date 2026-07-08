#pragma once
/**
 * renderer.h — SDL2 渲染器
 *
 * 负责：窗口管理、棋盘绘制、分数显示、动画、UI 交互
 * 设计原则：简洁扁平化，色彩参考原版 2048
 */

#include "board.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <array>
#include <vector>
#include <cmath>

// ============================================================
// 颜色方案（参考原版 2048 配色）
// ============================================================

struct Color {
    uint8_t r, g, b, a;
};

// 方块背景色（按 tile 指数索引：0=空, 1=2, 2=4, ... 15=32768）
inline Color tile_bg_color(int exponent) {
    static const Color colors[] = {
        {205, 193, 180, 255},  // 0: 空格
        {238, 228, 218, 255},  // 1: 2
        {237, 224, 200, 255},  // 2: 4
        {242, 177, 121, 255},  // 3: 8
        {245, 149,  99, 255},  // 4: 16
        {246, 124,  95, 255},  // 5: 32
        {246,  94,  59, 255},  // 6: 64
        {237, 207, 114, 255},  // 7: 128
        {237, 204,  97, 255},  // 8: 256
        {237, 200,  80, 255},  // 9: 512
        {237, 197,  63, 255},  // 10: 1024
        {237, 194,  46, 255},  // 11: 2048
        { 60,  58,  50, 255},  // 12: 4096
        { 60,  58,  50, 255},  // 13: 8192
        { 60,  58,  50, 255},  // 14: 16384
        { 60,  58,  50, 255},  // 15: 32768
    };
    if (exponent < 0 || exponent > 15) exponent = 0;
    return colors[exponent];
}

// 方块文字色
inline Color tile_text_color(int exponent) {
    if (exponent <= 2) return {119, 110, 101, 255}; // 深色文字（2, 4）
    return {249, 246, 242, 255};                     // 浅色文字（8+）
}

// UI 固定色
namespace UIColor {
    constexpr Color background   = {250, 248, 239, 255};
    constexpr Color board_bg     = {187, 173, 160, 255};
    constexpr Color header_text  = {119, 110, 101, 255};
    constexpr Color score_bg     = {187, 173, 160, 255};
    constexpr Color score_text   = {255, 255, 255, 255};
    constexpr Color button_bg    = {143, 122, 102, 255};
    constexpr Color button_text  = {249, 246, 242, 255};
    constexpr Color overlay      = {238, 228, 218, 180}; // 半透明遮罩
    constexpr Color gameover_text= {119, 110, 101, 255};
}

// ============================================================
// 动画数据
// ============================================================

// 缓动函数类型
enum class EaseType {
    Linear,
    EaseOutCubic,    // 滑动：快起慢停
    EaseOutBack,     // 合并弹跳：超过目标再回弹
    EaseOutElastic,  // 弹性
    EaseInOutQuad,   // 对称缓动
};

struct TileAnim {
    float from_row, from_col;  // 起始格（浮点精度）
    float to_row, to_col;      // 目标格
    int exponent;              // tile 指数（移动前的值）
    float elapsed;             // 已经过时间(s)
    float duration;            // 总时长(s)
};

struct MergeAnim {
    int row, col;
    int exponent;       // 合并后的指数
    float elapsed;
    float duration;
    float delay;        // 延迟启动（等滑动完成）
};

struct SpawnAnim {
    int row, col;
    int exponent;
    float elapsed;
    float duration;
    float delay;        // 延迟启动
};

// ============================================================
// 游戏状态
// ============================================================

enum class GameState {
    Playing,
    Won,       // 达到 2048（可选择继续）
    GameOver
};

// ============================================================
// Renderer 类
// ============================================================

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init(const char* title, int width, int height);
    void shutdown();

    // 每帧调用
    void render(board_t board, int score, int best_score, GameState state,
                bool ai_active = false, float ai_speed = 0.3f);

    // 动画
    void start_move_anim(board_t before, board_t after, int direction);
    void start_spawn_anim(int row, int col, int exponent);
    bool animating() const;
    void update_anims(float dt);
    void check_resize(int new_w, int new_h);

    // UI 交互
    bool is_new_game_clicked(int mx, int my) const;
    bool is_continue_clicked(int mx, int my) const;
    bool is_undo_clicked(int mx, int my) const;
    bool is_ai_toggle_clicked(int mx, int my) const;
    bool is_speed_up_clicked(int mx, int my) const;
    bool is_speed_down_clicked(int mx, int my) const;

    SDL_Window* window() { return window_; }
    SDL_Renderer* sdl_renderer() { return renderer_; }

private:
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    TTF_Font*     font_big_ = nullptr;   // 方块数字
    TTF_Font*     font_med_ = nullptr;   // 分数
    TTF_Font*     font_sml_ = nullptr;   // 标签

    int win_w_, win_h_;
    int board_x_, board_y_, board_size_;
    int cell_size_, cell_gap_;

    std::vector<TileAnim>  tile_anims_;
    std::vector<MergeAnim> merge_anims_;
    std::vector<SpawnAnim> spawn_anims_;

    // 按钮区域
    SDL_Rect new_game_btn_;
    SDL_Rect continue_btn_;
    SDL_Rect undo_btn_;
    SDL_Rect ai_toggle_btn_;
    SDL_Rect speed_down_btn_;
    SDL_Rect speed_up_btn_;

    void draw_rounded_rect(SDL_Rect rect, int radius, Color c);
    void draw_tile(int row, int col, int exponent, float scale = 1.0f, float alpha = 1.0f);
    void draw_text_centered(const char* text, SDL_Rect area, TTF_Font* font, Color c);
    void draw_header(int score, int best_score);
    void draw_board_background();
    void draw_toolbar(bool ai_active, float ai_speed);
    void draw_overlay(GameState state);

    void calc_layout();
    SDL_Rect cell_rect(int row, int col) const;
    SDL_Rect cell_rect_f(float row, float col) const;
};
