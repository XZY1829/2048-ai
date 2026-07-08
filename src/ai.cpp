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
 *     ├─ 如果 depth=0 或 cprob < 阈值 → 返回 eval(board)
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
 *     └─ 返回 best（如果无合法移动，返回 eval）
 */

#include "ai.h"

// ============================================================
// 构造函数
// ============================================================

AIEngine::AIEngine(AIConfig config) : config_(config) {}

// ============================================================
// 统一评估接口
// ============================================================

float AIEngine::eval(board_t board) const {
    if (config_.use_tuple_net && tuple_net_) {
        return tuple_net_->evaluate(board);
    }
    return evaluate_board(board);
}

// ============================================================
// 自适应搜索深度
// ============================================================

int AIEngine::adaptive_depth(board_t board) const {
    int empty = count_empty(board);

    if (empty >= 10) return 3;
    if (empty >= 6)  return 5;
    if (empty >= 4)  return 7;
    if (empty >= 2)  return 9;
    return std::min(config_.max_depth, 6);
}

// ============================================================
// 核心搜索: find_best_move
// ============================================================

int AIEngine::find_best_move(board_t board) {
    stats_ = {};
    cache_.clear();

    auto start = std::chrono::high_resolution_clock::now();

    int depth = adaptive_depth(board);
    float best_score = -1e18f;
    int best_dir = -1;

    for (int dir = 0; dir < 4; dir++) {
        board_t moved = execute_move(dir, board);
        if (moved == board) continue;

        // 即时 reward + 搜索得到的未来 value
        float reward = (float)get_move_score(board, dir);
        float future = score_chance_node(moved, depth, 1.0f);
        float score = reward + future;

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

float AIEngine::score_chance_node(board_t board, int depth, float cprob) {
    if (depth <= 0) {
        stats_.nodes_evaluated++;
        return eval(board);
    }

    if (cprob < config_.prob_threshold) {
        stats_.nodes_evaluated++;
        return eval(board);
    }

    if (depth < config_.cache_depth) {
        auto it = cache_.find(board);
        if (it != cache_.end() && it->second.depth >= depth) {
            stats_.cache_hits++;
            return it->second.heuristic;
        }
    }

    int empty_count = count_empty(board);
    float per_cell_prob = cprob / empty_count;

    float total_score = 0.0f;

    board_t tmp = board;
    for (int pos = 0; pos < 16; pos++) {
        if ((tmp & 0xF) == 0) {
            board_t board_with_2 = board | ((board_t)1 << (pos * 4));
            total_score += score_move_node(board_with_2, depth - 1, per_cell_prob * 0.9f) * 0.9f;

            board_t board_with_4 = board | ((board_t)2 << (pos * 4));
            total_score += score_move_node(board_with_4, depth - 1, per_cell_prob * 0.1f) * 0.1f;
        }
        tmp >>= 4;
    }

    float result = total_score / empty_count;

    if (depth < config_.cache_depth) {
        cache_[board] = {depth, result};
    }

    return result;
}

// ============================================================
// MAX 节点: 玩家选择最优方向
// ============================================================

float AIEngine::score_move_node(board_t board, int depth, float cprob) {
    float best = -1e18f;
    bool has_valid_move = false;

    for (int dir = 0; dir < 4; dir++) {
        board_t moved = execute_move(dir, board);
        if (moved == board) continue;

        has_valid_move = true;
        // MAX 节点: 选 r + future_value 最大的方向
        float reward = (float)get_move_score(board, dir);
        float future = score_chance_node(moved, depth, cprob);
        float score = reward + future;
        if (score > best) {
            best = score;
        }
    }

    if (!has_valid_move) {
        stats_.nodes_evaluated++;
        return eval(board);
    }

    return best;
}
