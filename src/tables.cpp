/**
 * tables.cpp — 预计算查找表的初始化实现
 *
 * 本文件在程序启动时被调用一次 (init_tables())，
 * 将所有 65536 种行状态的移动结果、得分、启发式评估预先计算好。
 *
 * ==================== 向左移动的模拟算法 ====================
 *
 * 以一行 [A, B, C, D] 为例（每个字母代表一个 nybble，0~15）：
 *
 * Step 1 — 压缩（去零靠左）：
 *   把所有非零值挤到左边，空位填到右边。
 *   例: [2, 0, 2, 4] → [2, 2, 4, 0]
 *
 * Step 2 — 合并（相邻同值合并一次）：
 *   从左到右扫描，相邻两个值相同则合并：
 *     - 左边的值 +1（log2 翻倍）
 *     - 右边的值消失（后面前移）
 *     - 已合并的格子不能再次合并（防止连锁）
 *   例: [2, 2, 4, 0] → [3, 4, 0, 0]
 *       含义: tile 4+4=8 (power 2+2→3), tile 16 不变
 *
 * Step 3 — 编码：
 *   把结果 4 个值重新编码为 16-bit row_t。
 *
 * ==================== 右移的实现技巧 ====================
 *
 * 右移 [A, B, C, D] 等价于：
 *   1. 翻转行: [D, C, B, A]
 *   2. 左移:   [...结果...]
 *   3. 翻转回来
 *
 * 所以 move_right_table[row] = reverse(move_left_table[reverse(row)])
 * 不需要单独实现右移逻辑。
 *
 * ==================== 启发式评估函数 ====================
 *
 * 为 AI 搜索提供"局面好坏"的评分。一行的评估包含多个特征：
 *
 * 1. 空格奖励 — 空格越多，灵活性越大，离死越远
 * 2. 单调性 — 行内值从左到右递减（或递增）是好的布局
 * 3. 平滑度 — 相邻格值接近，有利于后续合并
 * 4. 合并潜力 — 相邻相同值，下一步就能合并
 * 5. 大值位置权重 — 大 tile 在边角更安全（蛇形排列）
 */

#include "tables.h"
#include <cmath>
#include <algorithm>

// ============================================================
// 全局表定义
// ============================================================

row_t move_left_table[65536];
row_t move_right_table[65536];
int   score_table[65536];
float heur_table[65536];

// ============================================================
// 启发式权重（可调参）
// ============================================================

namespace heur_weights {
    // 这些权重经过实验调优，直接影响 AI 的冲分能力。
    // 后续可通过 CMA-ES 或遗传算法自动优化。
    constexpr float EMPTY_WEIGHT    = 270.0f;   // 每个空格的奖励
    constexpr float MONO_WEIGHT     = 47.0f;    // 单调性违反的惩罚
    constexpr float SMOOTH_WEIGHT   = 10.0f;    // 平滑度违反的惩罚
    constexpr float MERGE_WEIGHT    = 11.0f;    // 合并潜力的奖励
    constexpr float POSITION_WEIGHT = 1.5f;     // 位置权重系数
}

// ============================================================
// 单行启发式评估
// ============================================================

/**
 * 计算一行的启发式评估值。
 *
 * 参数: tiles[4] — 该行 4 个格的 log2 值
 * 返回: 该行对整体局面评估的贡献值
 *
 * 多个特征的加权求和，正值=好，负值=坏。
 */
static float compute_row_heuristic(const int tiles[4]) {
    float score = 0.0f;

    // ---- 特征 1: 空格奖励 ----
    // 空格越多越好。空格少意味着快要 Game Over。
    int empty_count = 0;
    for (int i = 0; i < 4; i++) {
        if (tiles[i] == 0) empty_count++;
    }
    score += empty_count * heur_weights::EMPTY_WEIGHT;

    // ---- 特征 2: 单调性 (Monotonicity) ----
    // 衡量这一行是否"有序排列"（递增或递减）。
    //
    // 理想情况: [大, 中, 小, 空] 或 [空, 小, 中, 大]
    // 这样大 tile 在边缘，小 tile 自然向大 tile 方向合并。
    //
    // 计算方法: 分别算"从左到右递减"和"从左到右递增"的违反程度，
    //          取较小者作为惩罚（即选择更接近单调的那个方向）。
    float mono_inc = 0.0f; // 假设应该递增，实际递减部分的惩罚
    float mono_dec = 0.0f; // 假设应该递减，实际递增部分的惩罚
    for (int i = 0; i < 3; i++) {
        if (tiles[i] > tiles[i + 1]) {
            // 违反了递增假设
            mono_inc += (float)(tiles[i] - tiles[i + 1]);
        } else if (tiles[i] < tiles[i + 1]) {
            // 违反了递减假设
            mono_dec += (float)(tiles[i + 1] - tiles[i]);
        }
    }
    // 取最小惩罚（两种方向里更单调的那个）
    score -= std::min(mono_inc, mono_dec) * heur_weights::MONO_WEIGHT;

    // ---- 特征 3: 平滑度 (Smoothness) ----
    // 相邻非空格的值差距越小越好（意味着更容易合并）。
    //
    // 例: [3, 3, 4, 4] 很平滑（相邻差=0或1）
    //     [1, 5, 2, 8] 很不平滑（差距大）
    for (int i = 0; i < 3; i++) {
        if (tiles[i] != 0 && tiles[i + 1] != 0) {
            score -= (float)std::abs(tiles[i] - tiles[i + 1]) * heur_weights::SMOOTH_WEIGHT;
        }
    }

    // ---- 特征 4: 合并潜力 (Merge Potential) ----
    // 相邻两格值相同，下一步就能合并得分，给予奖励。
    // 奖励大小与 tile 值成正比（合并大 tile 比合并小 tile 更有价值）。
    for (int i = 0; i < 3; i++) {
        if (tiles[i] != 0 && tiles[i] == tiles[i + 1]) {
            score += (float)tiles[i] * heur_weights::MERGE_WEIGHT;
        }
    }

    // ---- 特征 5: 位置权重 (Position Weight) ----
    // 鼓励大值 tile 占据行的一端（左侧优先，配合蛇形排列策略）。
    // 权重: 左边大，右边小。大 tile 在高权重位置得到额外加分。
    //
    // 蛇形排列策略:
    //   row0: → (从左到右递减)
    //   row1: ← (从右到左递减)
    //   row2: → (从左到右递减)
    //   row3: ← (从右到左递减)
    // 使得全局形成一条递减的"蛇"。
    //
    // 这里对单行，用平方加权强调大值在左侧的重要性。
    static const float pos_weight[4] = {4.0f, 3.0f, 2.0f, 1.0f};
    for (int i = 0; i < 4; i++) {
        if (tiles[i] != 0) {
            // 值的平方 × 位置权重（平方使大 tile 的位置更重要）
            score += (float)(tiles[i] * tiles[i]) * pos_weight[i] * heur_weights::POSITION_WEIGHT;
        }
    }

    return score;
}

// ============================================================
// 主初始化函数
// ============================================================

void init_tables() {
    // ================================================================
    // Pass 1: 计算 move_left_table, score_table, heur_table
    //
    // 必须先完整填好 move_left_table，然后才能计算 move_right_table。
    // 因为 move_right_table[row] 依赖 move_left_table[reverse(row)]，
    // 而 reverse(row) 可能比 row 大（尚未被计算）。
    // ================================================================

    for (unsigned row = 0; row < 65536; row++) {

        // ======== 解码: 16-bit row → 4 个 nybble ========
        //
        // row 的位布局: [nybble3][nybble2][nybble1][nybble0]
        //                高4bit                    低4bit
        //
        // 对应棋盘一行: [左列, ..., ..., 右列]
        // 即 nybble3=最左, nybble0=最右
        int tiles[4] = {
            (int)((row >> 12) & 0xF),  // 最左列
            (int)((row >>  8) & 0xF),
            (int)((row >>  4) & 0xF),
            (int)((row >>  0) & 0xF)   // 最右列
        };

        // ======== Step 1: 压缩（非零值向左靠拢）========
        //
        // 例: [0, 3, 0, 2] → [3, 2, 0, 0]
        //
        // 遍历原始数组，非零值依次填入 result 数组左端
        int compressed[4] = {0, 0, 0, 0};
        int write_pos = 0;
        for (int i = 0; i < 4; i++) {
            if (tiles[i] != 0) {
                compressed[write_pos++] = tiles[i];
            }
        }

        // ======== Step 2: 合并（相邻同值合并）========
        //
        // 规则:
        //   - 从左到右扫描
        //   - 相邻两格值相同 → 左格 +1（翻倍），右格消失
        //   - 同一格在一次移动中只能被合并一次
        //
        // 例: [3, 3, 3, 0]
        //   → 第一对(3,3)合并 → [4, 3, 0, 0]（不是[4, 4, 0, 0]！）
        //   因为合并后指针跳过已合并格，不会让刚合并的 4 再去合并后面的 3
        //
        // 例: [2, 2, 2, 2]
        //   → [3, 3, 0, 0]（两对分别合并，而不是连锁成一个 4）
        int merge_score = 0;
        for (int i = 0; i < 3; i++) {
            if (compressed[i] != 0 && compressed[i] == compressed[i + 1]) {
                compressed[i]++;                        // log2 值 +1 = tile 值翻倍
                merge_score += (1 << compressed[i]);    // 合并得分 = 合并后的 tile 实际值

                // 后续元素前移（填补被合并掉的那个格子）
                for (int j = i + 1; j < 3; j++) {
                    compressed[j] = compressed[j + 1];
                }
                compressed[3] = 0;

                // 注意: 这里不做 i++ 跳过！
                // 因为外层 for 会自增 i，下一轮检查的是新的 compressed[i+1]，
                // 而刚合并的 compressed[i] 已经变大了，不会和新邻居重复合并。
            }
        }

        // ======== Step 3: 编码回 16-bit ========
        row_t left_result = ((row_t)compressed[0] << 12) |
                            ((row_t)compressed[1] <<  8) |
                            ((row_t)compressed[2] <<  4) |
                            ((row_t)compressed[3]);

        move_left_table[row] = left_result;
        score_table[row] = merge_score;

        // ======== 启发式表 ========
        heur_table[row] = compute_row_heuristic(compressed);
    }

    // ================================================================
    // Pass 2: 计算 move_right_table
    //
    // 此时 move_left_table 已全部填好，可以安全引用任意下标。
    //
    // 右移 [A,B,C,D] 等价于:
    //   翻转 → [D,C,B,A]
    //   左移 → [结果]
    //   翻转 → [最终结果]
    //
    // 所以: move_right_table[row] = reverse(move_left_table[reverse(row)])
    // ================================================================

    for (unsigned row = 0; row < 65536; row++) {
        row_t reversed_input = reverse_row((row_t)row);
        row_t left_of_reversed = move_left_table[reversed_input];
        move_right_table[row] = reverse_row(left_of_reversed);
    }
}
