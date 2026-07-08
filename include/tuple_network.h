#pragma once
/**
 * tuple_network.h — 高性能 6-Tuple Network
 *
 * 性能优化核心：
 *   预计算所有 (pattern × symmetry) 的位置映射表。
 *   evaluate() 时直接从原始 board 按预计算位置提取 cell，无需棋盘变换。
 *   每次 evaluate = NUM_TUPLES 次索引提取 + 查表 + 累加。
 *
 * 架构：
 *   8 个 base pattern × 8 种对称变换 = 64 个 tuple
 *   每个 tuple: 6 个位置 → base-16 索引 → 查 weight table
 *   每个 base pattern 的 8 个对称变体共享同一张 weight table
 *
 * 存储: 8 patterns × 16^6 × 4 bytes = 512 MB
 */

#include "board.h"
#include <vector>
#include <array>
#include <string>
#include <cstdint>

class TupleNetwork {
public:
    static constexpr int BASE = 16;
    static constexpr int TUPLE_SIZE = 6;
    static constexpr int TABLE_SIZE = 16 * 16 * 16 * 16 * 16 * 16;  // 16,777,216

    // 8 个 base pattern（学术验证的高性能 pattern 集合）
    static constexpr int NUM_PATTERNS = 8;
    static constexpr int NUM_SYMMETRIES = 8;
    static constexpr int NUM_TUPLES = NUM_PATTERNS * NUM_SYMMETRIES;  // 64

    TupleNetwork();
    ~TupleNetwork() = default;

    /// 评估棋盘（预计算位置直接提取，极速）
    float evaluate(board_t board) const;

    /// TD 更新
    void update(board_t board, float delta);

    void save(const std::string& path) const;
    bool load(const std::string& path);
    void reset();

private:
    // 权重表: weights_[pattern_id][index]
    std::vector<std::vector<float>> weights_;

    // 预计算的展开 tuple 位置: expanded_[tuple_id][cell_in_tuple] = board_position
    // tuple_id = pattern_id * 8 + symmetry_id
    // 每个 tuple 对应哪个 weight table: tuple_id / 8
    std::array<std::array<int, TUPLE_SIZE>, NUM_TUPLES> expanded_;

    // 预计算: 位置 pos (0~15) 的 bit shift
    static constexpr int pos_shift(int pos) {
        int row = pos / 4, col = pos % 4;
        return ((3 - row) * 4 + (3 - col)) * 4;
    }

    // 快速提取 board 的 pos 位置 cell 值
    static int get_cell_at(board_t board, int pos) {
        return (int)((board >> pos_shift(pos)) & 0xF);
    }

    // 根据展开的位置列表提取索引
    int extract_index(board_t board, const std::array<int, TUPLE_SIZE>& positions) const {
        int index = 0;
        int mul = 1;
        for (int i = 0; i < TUPLE_SIZE; i++) {
            index += get_cell_at(board, positions[i]) * mul;
            mul *= BASE;
        }
        return index;
    }

    // 对称变换在位置空间的映射（不操作 board，直接变换 pos 坐标）
    static int transform_pos(int pos, int sym);

    // 初始化 expanded_ 表
    void init_expanded();
};
