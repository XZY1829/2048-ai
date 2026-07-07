/**
 * ai.cpp — Expectimax AI 引擎实现
 *
 * 搜索流程图:
 *
 *   find_best_move(board)
 *     │
 *     ├─ 对每个方向 dir ∈ {UP, DOWN, LEFT, RIGHT}:
 *     │    │
 *     │    ├─ new_board = execute_move(dir, board)
 *     │    │  如果 new_board == board → 跳过（无效移动）
 *     │    │
 *     │    └─ score = score_chance_node(new_board, depth, 1.0)
 *     │
 *     └─ 返回 score 最大的 dir
 *
 *   score_chance_node(board, depth, cprob)    ← CHANCE 节点
 *     │
 *     ├─ 如果 depth=0 或 cprob < 阈值 → 返回 evaluate_board(board)
 *     ├─ 查缓存，命中则返回
 *     │
 *     ├─ 对每个空格位置 pos:
 *     │    ├─ 放 2 (概率 0.9): score += 0.9 * score_move_node(board|2, ...)
 *     │    └─ 放 4 (概率 0.1): score += 0.1 * score_move_node(board|4, ...)
 *     │
 *     ├─ result = score / 空格数
 *     ├─ 存入缓存
 *     └─ 返回 result
 *
 *   score_move_node(board, depth, cprob)      ← MAX 节点
 *     │
 *     ├─ 对每个方向尝试移动
 *     ├─ 取最大的 score_chance_node 结果
 *     └─ 返回 best（如果无合法移动，返回 evaluate_board）
 */

#include "ai.h"

// ============================================================
// 构造函数
// ============================================================

AIEngine::AIEngine(AIConfig config) : config_(config) {}

// ============================================================
// 自适应搜索深度
// ============================================================

int AIEngine::adaptive_depth(board_t board) const {
    int empty = count_empty(board);

    // 空格数与搜索深度的映射关系:
    //
    // 空格多 → 每一步的 CHANCE 节点展开很多分支（空格数 × 2）
    //          分支因子大，树膨胀快，必须限制深度防止超时
    //
    // 空格少 → 分支因子小，可以搜更深
    //          而且空格少意味着局面危险，更需要深度思考
    //
    // 注意: 这里的 depth 是 CHANCE 层的深度（每经过一次"放方块"算一层）。
    //       实际博弈树展开 = depth 层 CHANCE × 每层前后各一个 MAX，
    //       所以实际搜索量随 depth 指数增长。

    if (empty >= 10) return 2;  // 开局/大量空格：极浅搜即可
    if (empty >= 6)  return 3;  // 早中期：适中
    if (empty >= 4)  return 4;  // 中后期：多看一步
    if (empty >= 2)  return 5;  // 后期危险：仔细搜索
    return std::min(config_.max_depth, 6); // 极度危险：全力搜索
}

// ============================================================
// 核心搜索: find_best_move
// ============================================================

int AIEngine::find_best_move(board_t board) {
    // 重置统计
    stats_ = {};
    cache_.clear(); // 每步清空缓存（局面变化大，旧缓存命中率低）

    auto start = std::chrono::high_resolution_clock::now();

    int depth = adaptive_depth(board);
    float best_score = -1e18f;
    int best_dir = -1;

    // 尝试 4 个方向，选评估值最高的
    for (int dir = 0; dir < 4; dir++) {
        board_t moved = execute_move(dir, board);

        // 跳过无效移动（移动后棋盘没变化）
        if (moved == board) continue;

        // 从 CHANCE 节点开始搜索（因为移动后下一步是放随机方块）
        float score = score_chance_node(moved, depth, 1.0f);

        if (score > best_score) {
            best_score = score;
            best_dir = dir;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    stats_.elapsed_ms = std::chrono::duration<float, std::milli>(end - start).count();
    stats_.depth_reached = depth;

    return best_dir;
}

// ============================================================
// CHANCE 节点: 随机方块放置的期望值
// ============================================================

/**
 * CHANCE 节点的工作：
 *
 * "游戏刚在某个空格放了一个方块，接下来棋盘状态的期望评估值是多少？"
 *
 * 遍历所有可能的放置方式:
 *   - 对每个空格: 放 2 (概率 0.9) 和 放 4 (概率 0.1)
 *   - 对每种可能的结果，递归调用 MAX 节点
 *   - 最终取加权平均
 *
 * 概率裁剪:
 *   当到达此节点的累积概率 cprob 低于阈值时，直接用启发式评估替代深搜。
 *   这条路径对最终决策影响极小（<0.01%），省掉的计算量却很大。
 */
float AIEngine::score_chance_node(board_t board, int depth, float cprob) {
    // ---- 终止条件 1: 深度耗尽 ----
    if (depth <= 0) {
        stats_.nodes_evaluated++;
        return evaluate_board(board);
    }

    // ---- 终止条件 2: 概率裁剪 ----
    // 到达这里的路径概率已经很小，继续搜索性价比极低
    if (cprob < config_.prob_threshold) {
        stats_.nodes_evaluated++;
        return evaluate_board(board);
    }

    // ---- 查缓存（Transposition Table）----
    // 同一个 board 可能从不同路径到达，避免重复计算
    if (depth < config_.cache_depth) {
        auto it = cache_.find(board);
        if (it != cache_.end() && it->second.depth >= depth) {
            // 缓存中的结果是用更深或相同深度算的，可以直接用
            stats_.cache_hits++;
            return it->second.heuristic;
        }
    }

    // ---- 展开所有可能的随机放置 ----
    int empty_count = count_empty(board);
    // 每个空格独立等概率被选中
    float per_cell_prob = cprob / empty_count;

    float total_score = 0.0f;

    // 遍历棋盘的 16 个位置，找出空格
    board_t tmp = board;
    for (int pos = 0; pos < 16; pos++) {
        if ((tmp & 0xF) == 0) {
            // pos 位置是空格

            // 放 2 (log2=1, 概率 90%)
            board_t board_with_2 = board | ((board_t)1 << (pos * 4));
            total_score += score_move_node(board_with_2, depth - 1, per_cell_prob * 0.9f) * 0.9f;

            // 放 4 (log2=2, 概率 10%)
            board_t board_with_4 = board | ((board_t)2 << (pos * 4));
            total_score += score_move_node(board_with_4, depth - 1, per_cell_prob * 0.1f) * 0.1f;
        }
        tmp >>= 4;
    }

    // 期望值 = 总分 / 空格数
    float result = total_score / empty_count;

    // ---- 存入缓存 ----
    if (depth < config_.cache_depth) {
        cache_[board] = {depth, result};
    }

    return result;
}

// ============================================================
// MAX 节点: 玩家选择最优方向
// ============================================================

/**
 * MAX 节点的工作：
 *
 * "当前局面下，玩家应该选哪个方向？"
 * 尝试所有合法方向，取评估值最大的那个。
 *
 * 如果没有任何合法移动（Game Over），直接返回当前局面的启发式评估值。
 */
float AIEngine::score_move_node(board_t board, int depth, float cprob) {
    float best = -1e18f;
    bool has_valid_move = false;

    for (int dir = 0; dir < 4; dir++) {
        board_t moved = execute_move(dir, board);

        // 跳过无效移动
        if (moved == board) continue;

        has_valid_move = true;
        float score = score_chance_node(moved, depth, cprob);
        if (score > best) {
            best = score;
        }
    }

    // 没有合法移动 = Game Over，返回当前评估（通常是很低的值）
    if (!has_valid_move) {
        stats_.nodes_evaluated++;
        return evaluate_board(board);
    }

    return best;
}
