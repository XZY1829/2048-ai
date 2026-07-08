#pragma once
/**
 * trainer.h — TD(lambda) 训练器
 *
 * 算法：Temporal Difference Learning with eligibility traces
 *
 * 核心公式：
 *   afterstate s' = execute_move(dir, s)  （玩家动作后、随机方块放置前的状态）
 *
 *   TD error:
 *     δ = r + γ × V(s'_next) - V(s'_current)
 *
 *   Eligibility trace (累积资格迹):
 *     e_t = γ × λ × e_{t-1} + ∇V(s'_t)
 *
 *   权重更新:
 *     w += α × δ × e
 *
 * 训练模式：
 *   纯 CLI，不链接 SDL，不渲染画面。
 *   每局从空棋盘开始，贪心选择动作（选 evaluate 最高的 afterstate），
 *   直到 Game Over，然后用完整 episode 的经历做 TD(λ) 更新。
 */

#include "tuple_network.h"
#include "board.h"
#include "tables.h"
#include <string>
#include <random>

struct TrainConfig {
    float alpha  = 0.001f;   // 学习率
    float gamma  = 1.0f;     // 折扣因子（2048 无折扣）
    float lambda = 0.7f;     // eligibility trace 衰减

    int   num_episodes  = 100000;  // 训练总局数
    int   report_every  = 1000;    // 每多少局报告一次
    std::string save_path = "models/tuple_weights.bin";  // 权重保存路径
    std::string load_path = "";    // 加载已有权重继续训练
};

struct EpisodeStats {
    int   score      = 0;
    int   max_tile   = 0;
    int   moves      = 0;
};

class TDLambdaTrainer {
public:
    explicit TDLambdaTrainer(TrainConfig config = {});

    /// 运行完整训练流程
    void train();

    /// 获取训练好的网络（用于 play 模式）
    TupleNetwork& network() { return network_; }
    const TupleNetwork& network() const { return network_; }

private:
    TrainConfig config_;
    TupleNetwork network_;
    std::mt19937 rng_;

    /// 运行一局游戏并收集经历
    EpisodeStats run_episode();

    /// 使用贪心策略选择动作（选 afterstate evaluate 最高的方向）
    /// 返回: 最优方向（0~3），如果无合法移动返回 -1
    int greedy_action(board_t board) const;

    /// 报告训练进度
    void report(int episode, const std::vector<EpisodeStats>& recent_stats) const;
};
