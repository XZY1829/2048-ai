#pragma once
/**
 * ai.h — Expectimax AI 冲分引擎
 *
 * Expectimax 是博弈树搜索算法，专为带随机性的游戏设计。
 * MAX 节点: 玩家选最优方向 | CHANCE 节点: 随机放方块取期望
 *
 * 优化: Transposition Table + Probability Pruning + 自适应深度
 */

#include "board.h"
#include "tables.h"
#include <unordered_map>
#include <chrono>

struct AIConfig {
    int   max_depth       = 6;       // 最大搜索深度
    float prob_threshold  = 0.001f;  // 概率裁剪阈值（越大剪枝越激进）
    int   cache_depth     = 5;       // 缓存深度上限
};

struct AIStats {
    int   nodes_evaluated = 0;
    int   cache_hits      = 0;
    int   depth_reached   = 0;
    float elapsed_ms      = 0;
};

class AIEngine {
public:
    explicit AIEngine(AIConfig config = {});
    int find_best_move(board_t board);
    AIStats last_stats() const { return stats_; }
    void clear_cache() { cache_.clear(); }

private:
    AIConfig config_;
    AIStats  stats_;

    struct CacheEntry { int depth; float heuristic; };
    std::unordered_map<board_t, CacheEntry> cache_;

    float score_move_node(board_t board, int depth, float cprob);
    float score_chance_node(board_t board, int depth, float cprob);
    int adaptive_depth(board_t board) const;
};
