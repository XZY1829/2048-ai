#pragma once
/**
 * tables.h — 预计算查找表
 *
 * 核心思想：
 *   2048 的移动操作本质上是"一行的变换"。
 *   一行有 4 格，每格 4 bit，共 16 bit = 65536 种可能的行状态。
 *   我们在程序启动时，把每一种行状态的移动结果都预先算好存进数组。
 *   运行时只需要查表，O(1) 完成一行的移动。
 *
 * 查表法的威力：
 *   - 朴素模拟一行移动: ~200ns（循环、条件判断）
 *   - 查表法一行移动:   ~5ns（一次数组访问）
 *   - 加速比: 约 40 倍
 *   - AI 搜索百万节点时，这个差距是"能用"和"卡死"的区别
 *
 * 表占用内存：
 *   move_left:  65536 × 2 bytes =  128 KB
 *   move_right: 65536 × 2 bytes =  128 KB
 *   score:      65536 × 4 bytes =  256 KB
 *   heuristic:  65536 × 4 bytes =  256 KB
 *   总计: ~768 KB，完全可接受
 */

#include "board.h"

// ============================================================
// 查找表（全局数组，程序启动时填充）
// ============================================================

/// 行向左移动的结果表
/// 用法: move_left_table[原始行] = 移动后的行
extern row_t move_left_table[65536];

/// 行向右移动的结果表
extern row_t move_right_table[65536];

/// 行向左移动时产生的合并得分
/// 例: [2,2,0,0] 左移变成 [3,0,0,0]（tile 4+4=8），得分 = 8
extern int score_table[65536];

/// 行的启发式评估值（用于 AI 评估局面好坏）
extern float heur_table[65536];

// ============================================================
// 初始化（必须在游戏开始前调用一次）
// ============================================================

/// 填充所有查找表。程序启动时调用一次即可。
void init_tables();

// ============================================================
// 基于查表的移动操作
// ============================================================

/**
 * 翻转一行（4个nybble左右对调）。
 *
 * 例: 0xABCD → 0xDCBA
 *
 * 用途: 右移 = 翻转 → 左移 → 翻转
 */
inline row_t reverse_row(row_t row) {
    return ((row & 0x000F) << 12) |
           ((row & 0x00F0) <<  4) |
           ((row & 0x0F00) >>  4) |
           ((row & 0xF000) >> 12);
}

/**
 * 整个棋盘向左移动。
 *
 * 实现: 对 4 行分别查 move_left_table，拼回 64-bit board。
 *
 * board 的内存布局（回顾）:
 *   bits [63:48] = row 0
 *   bits [47:32] = row 1
 *   bits [31:16] = row 2
 *   bits [15:0]  = row 3
 */
inline board_t board_move_left(board_t board) {
    board_t result = 0;
    result |= (board_t)move_left_table[(board >> 48) & 0xFFFF] << 48;
    result |= (board_t)move_left_table[(board >> 32) & 0xFFFF] << 32;
    result |= (board_t)move_left_table[(board >> 16) & 0xFFFF] << 16;
    result |= (board_t)move_left_table[(board >>  0) & 0xFFFF];
    return result;
}

/**
 * 整个棋盘向右移动。
 */
inline board_t board_move_right(board_t board) {
    board_t result = 0;
    result |= (board_t)move_right_table[(board >> 48) & 0xFFFF] << 48;
    result |= (board_t)move_right_table[(board >> 32) & 0xFFFF] << 32;
    result |= (board_t)move_right_table[(board >> 16) & 0xFFFF] << 16;
    result |= (board_t)move_right_table[(board >>  0) & 0xFFFF];
    return result;
}

/**
 * 整个棋盘向上移动。
 *
 * 列操作 = 转置 → 行操作 → 转置
 * move_up = transpose → move_left → transpose
 */
inline board_t board_move_up(board_t board) {
    return transpose(board_move_left(transpose(board)));
}

/**
 * 整个棋盘向下移动。
 *
 * move_down = transpose → move_right → transpose
 */
inline board_t board_move_down(board_t board) {
    return transpose(board_move_right(transpose(board)));
}

/**
 * 统一移动接口。
 *
 * 参数: dir — 方向 (UP/DOWN/LEFT/RIGHT)
 * 返回: 移动后的 board（若移动无效，返回值等于输入）
 */
inline board_t execute_move(int dir, board_t board) {
    switch (dir) {
        case UP:    return board_move_up(board);
        case DOWN:  return board_move_down(board);
        case LEFT:  return board_move_left(board);
        case RIGHT: return board_move_right(board);
    }
    return board;
}

/**
 * 计算某次移动产生的合并得分。
 *
 * 注意: 需要传入移动前的 board 和方向，因为 score_table 是按原始行查的。
 */
inline int get_move_score(board_t board, int dir) {
    int total = 0;

    // 左右移动：直接对每行查表
    if (dir == LEFT || dir == RIGHT) {
        for (int i = 0; i < 4; i++) {
            row_t row = (board >> ((3 - i) * 16)) & 0xFFFF;
            total += score_table[row];
        }
    }
    // 上下移动：先转置，使列变成行，再查表
    else {
        board_t tb = transpose(board);
        for (int i = 0; i < 4; i++) {
            row_t row = (tb >> ((3 - i) * 16)) & 0xFFFF;
            total += score_table[row];
        }
    }
    return total;
}

/**
 * Game Over 检测（基于查表的快速版本）。
 *
 * 原理: 如果四个方向移动后 board 都没有变化，就是 Game Over。
 */
inline bool is_game_over(board_t board) {
    return board_move_left(board) == board &&
           board_move_right(board) == board &&
           board_move_up(board) == board &&
           board_move_down(board) == board;
}

/**
 * 启发式评估整个棋盘。
 *
 * 方法: 对 4 行 + 4 列 分别查 heur_table，求和。
 * 一次评估 = 8 次查表 + 7 次加法 = O(1)。
 *
 * 这是 AI 搜索中调用最频繁的函数，性能至关重要。
 */
inline float evaluate_board(board_t board) {
    float score = 0.0f;
    board_t transposed = transpose(board);

    // 4 行的评估
    score += heur_table[(board >> 48) & 0xFFFF];
    score += heur_table[(board >> 32) & 0xFFFF];
    score += heur_table[(board >> 16) & 0xFFFF];
    score += heur_table[(board >>  0) & 0xFFFF];

    // 4 列的评估（转置后按行查）
    score += heur_table[(transposed >> 48) & 0xFFFF];
    score += heur_table[(transposed >> 32) & 0xFFFF];
    score += heur_table[(transposed >> 16) & 0xFFFF];
    score += heur_table[(transposed >>  0) & 0xFFFF];

    return score;
}
