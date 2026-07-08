/**
 * tuple_network.cpp — 高性能 6-Tuple Network 实现
 *
 * 关键优化：
 *   1. 预计算所有 64 个 tuple 的位置映射（构造时一次性完成）
 *   2. evaluate() 无需棋盘变换，直接按位置提取
 *   3. 使用 8 个多样化 pattern 覆盖更多棋盘特征
 *
 * Pattern 设计原则（来自 Yeh et al. 2016, Szubert & Jaskowski 2014）：
 *   - 横向连续: 捕捉行内单调性和合并机会
 *   - 纵向连续: 捕捉列内模式
 *   - 矩形: 捕捉 2D 局部结构
 *   - L/T 型: 捕捉角落和边缘模式
 */

#include "tuple_network.h"
#include <fstream>
#include <cstring>
#include <algorithm>

// ============================================================
// 位置空间对称变换
// ============================================================
//
// 棋盘位置编号:
//  0  1  2  3
//  4  5  6  7
//  8  9 10 11
// 12 13 14 15
//
// pos → (row, col): row = pos/4, col = pos%4
// (row, col) → pos: pos = row*4 + col
//
// 8 种对称变换直接在 (row, col) 坐标上操作：

int TupleNetwork::transform_pos(int pos, int sym) {
    int r = pos / 4, c = pos % 4;
    int nr, nc;
    switch (sym) {
        case 0: nr = r;     nc = c;     break;  // identity
        case 1: nr = c;     nc = 3 - r; break;  // rotate 90 CW
        case 2: nr = 3 - r; nc = 3 - c; break;  // rotate 180
        case 3: nr = 3 - c; nc = r;     break;  // rotate 270 CW
        case 4: nr = r;     nc = 3 - c; break;  // mirror horizontal
        case 5: nr = c;     nc = r;     break;  // mirror H + rotate 90 = transpose
        case 6: nr = 3 - r; nc = c;     break;  // mirror vertical
        case 7: nr = 3 - c; nc = 3 - r; break;  // anti-transpose
        default: nr = r; nc = c; break;
    }
    return nr * 4 + nc;
}

// ============================================================
// 构造函数
// ============================================================

TupleNetwork::TupleNetwork() {
    // 分配权重表
    weights_.resize(NUM_PATTERNS);
    for (int p = 0; p < NUM_PATTERNS; p++) {
        weights_[p].assign(TABLE_SIZE, 0.0f);
    }
    init_expanded();
}

void TupleNetwork::init_expanded() {
    // 8 个 base pattern 定义
    // 覆盖横向、纵向、矩形、L型、对角等多种局部结构
    static const int base_patterns[NUM_PATTERNS][TUPLE_SIZE] = {
        {0, 1, 2, 3, 4, 5},      // 横向: row0 全 + row1 前2
        {4, 5, 6, 7, 8, 9},      // 横向: row1 全 + row2 前2
        {0, 1, 2, 4, 5, 6},      // 2×3 矩形 左上
        {2, 3, 6, 7, 10, 11},    // 2×3 矩形 右中
        {0, 1, 4, 5, 8, 9},      // 3×2 矩形 左
        {0, 1, 5, 6, 10, 11},    // 斜阶梯: 对角方向
        {0, 1, 2, 5, 9, 13},     // 长 L 型
        {0, 4, 8, 12, 13, 14},   // 纵向 + 底横
    };

    // 对每个 base pattern 施加 8 种对称变换，生成 64 个展开 tuple
    for (int p = 0; p < NUM_PATTERNS; p++) {
        for (int sym = 0; sym < NUM_SYMMETRIES; sym++) {
            int tuple_id = p * NUM_SYMMETRIES + sym;
            for (int i = 0; i < TUPLE_SIZE; i++) {
                expanded_[tuple_id][i] = transform_pos(base_patterns[p][i], sym);
            }
        }
    }
}

// ============================================================
// 评估（核心热路径）
// ============================================================

float TupleNetwork::evaluate(board_t board) const {
    float value = 0.0f;

    // 预提取所有 16 个 cell 值，避免重复位运算
    int cells[16];
    for (int i = 0; i < 16; i++) {
        cells[i] = (int)((board >> pos_shift(i)) & 0xF);
    }

    for (int tid = 0; tid < NUM_TUPLES; tid++) {
        const auto& positions = expanded_[tid];
        int index = cells[positions[0]]
                  + cells[positions[1]] * 16
                  + cells[positions[2]] * 256
                  + cells[positions[3]] * 4096
                  + cells[positions[4]] * 65536
                  + cells[positions[5]] * 1048576;
        value += weights_[tid / NUM_SYMMETRIES][index];
    }
    return value;
}

// ============================================================
// TD 更新
// ============================================================

void TupleNetwork::update(board_t board, float delta) {
    int cells[16];
    for (int i = 0; i < 16; i++) {
        cells[i] = (int)((board >> pos_shift(i)) & 0xF);
    }

    for (int tid = 0; tid < NUM_TUPLES; tid++) {
        const auto& positions = expanded_[tid];
        int index = cells[positions[0]]
                  + cells[positions[1]] * 16
                  + cells[positions[2]] * 256
                  + cells[positions[3]] * 4096
                  + cells[positions[4]] * 65536
                  + cells[positions[5]] * 1048576;
        weights_[tid / NUM_SYMMETRIES][index] += delta;
    }
}

// ============================================================
// 序列化
// ============================================================

void TupleNetwork::save(const std::string& path) const {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return;

    int num_patterns = NUM_PATTERNS;
    int table_size = TABLE_SIZE;
    ofs.write(reinterpret_cast<const char*>(&num_patterns), sizeof(int));
    ofs.write(reinterpret_cast<const char*>(&table_size), sizeof(int));

    for (int p = 0; p < NUM_PATTERNS; p++) {
        ofs.write(reinterpret_cast<const char*>(weights_[p].data()),
                  (size_t)TABLE_SIZE * sizeof(float));
    }
    ofs.close();
}

bool TupleNetwork::load(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;

    int num_patterns = 0, table_size = 0;
    ifs.read(reinterpret_cast<char*>(&num_patterns), sizeof(int));
    ifs.read(reinterpret_cast<char*>(&table_size), sizeof(int));

    if (num_patterns != NUM_PATTERNS || table_size != TABLE_SIZE) {
        return false;
    }

    for (int p = 0; p < NUM_PATTERNS; p++) {
        ifs.read(reinterpret_cast<char*>(weights_[p].data()),
                 (size_t)TABLE_SIZE * sizeof(float));
    }
    return ifs.good();
}

void TupleNetwork::reset() {
    for (int p = 0; p < NUM_PATTERNS; p++) {
        std::fill(weights_[p].begin(), weights_[p].end(), 0.0f);
    }
}
