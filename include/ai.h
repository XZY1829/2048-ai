#pragma once
/**
 * ai.h — Expectimax AI 冲分引擎
 *
 * Expectimax 是博弈树搜索算法，专为带随机性的游戏设计。
 * MAX 节点: 玩家选最优方向 | CHANCE 节点: 随机放方块取期望
 *
 * 优化: Transposition Table + Probability Pruning + 自适应深度
 *
 * 评估函数支持两种模式：
 *   1. 手工启发式（heur_table 查表）
 *   2. 学习的 6-Tuple Network（TD Learning 训练）
 */

#include "board.h"
#include "tables.h"
#include "tuple_network.h"
#include <unordered_map>
#include <chrono>
#include <memory>

struct AIConfig {
    int   max_depth       = 10;       // 最大搜索深度
    float prob_threshold  = 0.001f;  // 概率裁剪阈值（越大剪枝越激进）
    int   cache_depth     = 9;       // 缓存深度上限
    bool  use_tuple_net   = false;   // 是否使用 Tuple Network 评估
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

    /// 设置 Tuple Network（从外部传入训练好的网络）
    void set_tuple_network(TupleNetwork* net) { tuple_net_ = net; }

    /// 获取当前配置（可修改）
    AIConfig& config() { return config_; }

private:
    AIConfig config_;
    AIStats  stats_;
    TupleNetwork* tuple_net_ = nullptr;

    struct CacheEntry { int depth; float heuristic; };
    std::unordered_map<board_t, CacheEntry> cache_;

    float score_move_node(board_t board, int depth, float cprob);
    float score_chance_node(board_t board, int depth, float cprob);
    int adaptive_depth(board_t board) const;

    /// 统一评估接口：根据配置选择手工启发式或 Tuple Network
    float eval(board_t board) const;
};
