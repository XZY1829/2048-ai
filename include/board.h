#pragma once
/**
 * board.h — 2048 Bitboard 棋盘表示
 *
 * 核心思想：
 *   把 4×4 棋盘编码为一个 64-bit 无符号整数 (uint64_t)。
 *   每个格子占 4 bit（称为 nybble），存储的是 tile 值的 log2：
 *     0 = 空格
 *     1 = 2     (2^1)
 *     2 = 4     (2^2)
 *     3 = 8     (2^3)
 *     ...
 *     11 = 2048  (2^11)
 *     15 = 32768 (2^15，4 bit 能表示的最大值)
 *
 * 棋盘布局（nybble 在 uint64_t 中的位置）：
 *
 *   高位 ←――――――――――――――――――――――――――――→ 低位
 *   [63:60][59:56][55:52][51:48] | [47:44][43:40][39:36][35:32] | ...
 *        row 0                         row 1
 *
 *   row 0: bits 48~63  (最高 16 bit)
 *   row 1: bits 32~47
 *   row 2: bits 16~31
 *   row 3: bits 0~15   (最低 16 bit)
 *
 *   每一行内，从左到右对应从高 nybble 到低 nybble。
 */

#include <cstdint>
#include <random>

// ============================================================
// 类型定义
// ============================================================

/// 棋盘类型：64-bit 整数，4×4 格，每格 4 bit
using board_t = uint64_t;

/// 行类型：16-bit 整数，4 格 × 4 bit
using row_t = uint16_t;

/// 移动方向
enum Direction { UP = 0,
                 DOWN = 1,
                 LEFT = 2,
                 RIGHT = 3 };

// ============================================================
// 基础操作：提取 / 设置格子和行
// ============================================================

/**
 * 获取棋盘第 (row, col) 格的 log2 值。
 *
 * 参数: row ∈ [0,3], col ∈ [0,3]
 * 返回: 0~15（0 表示空，n 表示 tile 值为 2^n）
 *
 * 计算过程:
 *   - 第 (row, col) 在 64-bit 中的起始 bit 位置 = ((3-row)*4 + (3-col)) * 4
 *   - 右移到最低 4 位，再 mask 取出
 */
inline int get_cell(board_t board, int row, int col) {
    int shift = ((3 - row) * 4 + (3 - col)) * 4;
    return (board >> shift) & 0xF;
}

/**
 * 获取格子对应的实际 tile 值（2, 4, 8, ..., 32768）。
 * 空格返回 0。
 */
inline int get_tile_value(board_t board, int row, int col) {
    int power = get_cell(board, row, col);
    return power ? (1 << power) : 0;
}

/**
 * 设置棋盘第 (row, col) 格的 log2 值。
 */
inline board_t set_cell(board_t board, int row, int col, int value) {
    int shift = ((3 - row) * 4 + (3 - col)) * 4;
    board &= ~((board_t)0xF << shift);  // 清除原值
    board |= ((board_t)value << shift); // 写入新值
    return board;
}

/**
 * 提取棋盘的第 i 行 (i=0 是最上面一行)。
 *
 * 返回一个 16-bit 值，高 4 bit 是最左列，低 4 bit 是最右列。
 * 例: row=0 → 提取 bits[63:48]
 */
inline row_t get_row(board_t board, int row) {
    return (board >> ((3 - row) * 16)) & 0xFFFF;
}

/**
 * 提取棋盘的第 i 列 (i=0 是最左列)。
 *
 * 列没有连续存储，需要逐格提取后拼成 row_t 格式：
 *   高4bit = 该列第0行的值, ..., 低4bit = 该列第3行的值
 */
inline row_t get_col(board_t board, int col) {
    row_t result = 0;
    for (int r = 0; r < 4; r++) {
        int val = get_cell(board, r, col);
        result |= (row_t)val << ((3 - r) * 4);
    }
    return result;
}

// ============================================================
// 转置
// ============================================================

/**
 * 棋盘转置：行列互换。
 *
 * 转置前:          转置后:
 *   A B C D         A E I M
 *   E F G H    →    B F J N
 *   I J K L         C G K O
 *   M N O P         D H L P
 *
 * 转置使得"列操作"可以复用"行操作"：
 *   move_up(board) = transpose(move_left(transpose(board)))
 *
 * 实现方法：通过三次位交换完成（类似矩阵转置的分治法）
 */
inline board_t transpose(board_t board) {

    // 交换两个 nybble
    auto swap_nibbles = [](board_t x, int a, int b) -> board_t {
        // a,b 是从最低位开始编号的 nybble index
        // nybble 0 = bits[3:0]
        // nybble 15 = bits[63:60]

        if (a > b)
            std::swap(a, b);

        int shift = (b - a) * 4;

        board_t mask = (board_t)0xF << (a * 4);

        board_t tmp = (x ^ (x >> shift)) & mask;

        x ^= tmp ^ (tmp << shift);

        return x;
    };

    /*
        nybble 编号：

        bit63                       bit0

        15 14 13 12
        11 10  9  8
        7  6  5  4
        3  2  1  0


        需要交换：

        14 <-> 11   (0,1)<->(1,0)
        13 <-> 7    (0,2)<->(2,0)
        12 <-> 3    (0,3)<->(3,0)
        9 <-> 6     (1,2)<->(2,1)
        8 <-> 2     (1,3)<->(3,1)
        4 <-> 1     (2,3)<->(3,2)
    */

    board = swap_nibbles(board, 14, 11);
    board = swap_nibbles(board, 13, 7);
    board = swap_nibbles(board, 12, 3);

    board = swap_nibbles(board, 9, 6);
    board = swap_nibbles(board, 8, 2);

    board = swap_nibbles(board, 4, 1);

    return board;
}

// // 注意：高效转置的位操作比较绕，这里用可读版本：
// // 直接逐格提取再组装（清晰优先，性能差异在查表加速下可忽略）
// board_t result = 0;
// for (int r = 0; r < 4; r++) {
//     for (int c = 0; c < 4; c++) {
//         int val = (board >> (((3 - r) * 4 + (3 - c)) * 4)) & 0xF;
//         result |= (board_t)val << (((3 - c) * 4 + (3 - r)) * 4);
//     }
// }

// ============================================================
// 空格计数
// ============================================================

/**
 * 统计棋盘上的空格数量。
 *
 * 方法: 遍历 16 个 nybble，值为 0 即空格。
 */
inline int count_empty(board_t board) {
    int count = 0;
    for (int i = 0; i < 16; i++) {
        if ((board & 0xF) == 0)
            count++;
        board >>= 4;
    }
    return count;
}

/**
 * 获取棋盘上的最大 tile 值（实际值，不是 log2）。
 */
inline int max_tile(board_t board) {
    int max_power = 0;
    for (int i = 0; i < 16; i++) {
        int val = board & 0xF;
        if (val > max_power)
            max_power = val;
        board >>= 4;
    }
    return max_power ? (1 << max_power) : 0;
}

// ============================================================
// 随机放置新方块
// ============================================================

/**
 * 在棋盘的随机空位放置一个新方块。
 *
 * 规则:
 *   - 90% 概率放 2 (log2 值 = 1)
 *   - 10% 概率放 4 (log2 值 = 2)
 *   - 随机均匀选择一个空位
 *
 * 参数: rng — 随机数生成器引用
 * 返回: 放置后的新 board（若无空位返回原 board）
 */
inline board_t spawn_tile(board_t board, std::mt19937& rng) {
    // 收集所有空位的位置索引 (0~15)
    int empty_positions[16];
    int empty_count = 0;

    board_t tmp = board;
    for (int i = 0; i < 16; i++) {
        if ((tmp & 0xF) == 0) {
            empty_positions[empty_count++] = i;
        }
        tmp >>= 4;
    }

    if (empty_count == 0)
        return board; // 无空位

    // 随机选一个空位
    int pos = empty_positions[rng() % empty_count];

    // 90% 概率放 2 (power=1)，10% 概率放 4 (power=2)
    int power = (rng() % 10 == 0) ? 2 : 1;

    // 在指定位置写入
    return board | ((board_t)power << (pos * 4));
}

// ============================================================
// Game Over 检测
// ============================================================

/**
 * 前向声明：execute_move 在 tables 初始化后可用。
 * 这里提供一个不依赖查表的朴素判定方法。
 *
 * Game Over 条件：没有任何方向能产生有效移动。
 * 有效移动 = 移动后 board 发生变化。
 *
 * 朴素判定：检查是否存在相邻相同值或存在空格。
 */
inline bool is_game_over_naive(board_t board) {
    // 有空格就没结束
    if (count_empty(board) > 0)
        return false;

    // 检查相邻格（同行或同列）是否有相同值
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int val = get_cell(board, r, c);
            // 右邻
            if (c < 3 && val == get_cell(board, r, c + 1))
                return false;
            // 下邻
            if (r < 3 && val == get_cell(board, r + 1, c))
                return false;
        }
    }
    return true; // 无合并可能，游戏结束
}
