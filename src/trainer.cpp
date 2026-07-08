/**
 * trainer.cpp — 正确的 Afterstate TD(λ) 实现
 *
 * ========== 数学推导 ==========
 *
 * Afterstate value function V(s') 定义：
 *   V(s') = E[r_{t+1} + r_{t+2} + ... | afterstate = s']
 *   即从 afterstate s' 出发，未来所有 reward 的期望总和。
 *
 * 一步转移：
 *   afterstate s'_t → (random tile) → state s_{t+1} → (action a_{t+1}) → afterstate s'_{t+1}
 *   获得 reward r_{t+1}（action a_{t+1} 的合并得分）
 *
 * TD(0) target:
 *   V(s'_t) ≈ r_{t+1} + V(s'_{t+1})
 *
 * TD error:
 *   δ_t = r_{t+1} + V(s'_{t+1}) - V(s'_t)
 *
 * TD(λ) 反向视图（offline, pre-computed values）：
 *   G_t = δ_t + λ·δ_{t+1} + λ²·δ_{t+2} + ...  (λ-return 的等价形式)
 *   Δw for s'_t = α · G_t · ∇V(s'_t)
 *
 * 实现：反向累积
 *   accumulated = 0
 *   for t from T-1 down to 0:
 *     δ_t = r_{t+1} + V(s'_{t+1}) - V(s'_t)    ← 用预存的 V 值！
 *     accumulated = δ_t + λ · accumulated
 *     w[s'_t] += α · accumulated
 *
 * ========== 关键修复 ==========
 *
 * 1. 贪心策略选 argmax(r_t + V(s'_t))，不是 argmax V(s'_t)
 * 2. TD 更新用预计算的 V 值（避免 weight drift）
 * 3. 学习率 α 应用于每个 tuple weight（总共 64 个），
 *    有效学习率 = 64α，所以用较小的 α
 */

#include "trainer.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <filesystem>

// ============================================================
// 构造函数
// ============================================================

TDLambdaTrainer::TDLambdaTrainer(TrainConfig config)
    : config_(config), rng_(std::random_device{}()) {

    if (!config_.load_path.empty()) {
        if (network_.load(config_.load_path)) {
            printf("[Trainer] Loaded weights from: %s\n", config_.load_path.c_str());
        } else {
            printf("[Trainer] Failed to load weights, starting fresh.\n");
        }
    }
}

// ============================================================
// 贪心策略：选 r + V(afterstate) 最大的方向
// ============================================================

int TDLambdaTrainer::greedy_action(board_t board) const {
    float best_value = -1e30f;
    int best_dir = -1;

    for (int dir = 0; dir < 4; dir++) {
        board_t afterstate = execute_move(dir, board);
        if (afterstate == board) continue;

        // 正确的贪心: argmax_a [r(s,a) + V(afterstate(s,a))]
        float reward = (float)get_move_score(board, dir);
        float value = reward + network_.evaluate(afterstate);
        if (value > best_value) {
            best_value = value;
            best_dir = dir;
        }
    }
    return best_dir;
}

// ============================================================
// 运行一局并执行 TD(λ) 更新
// ============================================================

EpisodeStats TDLambdaTrainer::run_episode() {
    EpisodeStats stats;

    board_t board = 0;
    board = spawn_tile(board, rng_);
    board = spawn_tile(board, rng_);

    // 收集完整 episode: (afterstate, reward, V_precomputed)
    struct Step {
        board_t afterstate;
        float   reward;     // 产生此 afterstate 的 action 的合并得分
        float   value;      // 预计算的 V(afterstate)，更新前冻结
    };
    std::vector<Step> history;
    history.reserve(3000);

    while (true) {
        int dir = greedy_action(board);
        if (dir == -1) break;

        float reward = (float)get_move_score(board, dir);
        stats.score += (int)reward;
        stats.moves++;

        board_t afterstate = execute_move(dir, board);

        // 预计算 V 值（在任何权重更新之前）
        float v = network_.evaluate(afterstate);
        history.push_back({afterstate, reward, v});

        board = spawn_tile(afterstate, rng_);
    }

    stats.max_tile = max_tile(board);

    // ============================================================
    // Offline TD(λ) 反向更新
    //
    // 使用预计算的 V 值，避免 weight drift 问题：
    //   δ_t = r_{t+1} + V(s'_{t+1}) - V(s'_t)
    //   （终局: δ_{T-1} = 0 + 0 - V(s'_{T-1})）
    //
    // 反向累积:
    //   G = 0
    //   for t = T-1 down to 0:
    //     δ_t = (t<T-1 ? history[t+1].reward + history[t+1].value : 0) - history[t].value
    //     G = δ_t + λ × G
    //     network.update(s'_t, α × G)
    // ============================================================

    int T = (int)history.size();
    if (T == 0) return stats;

    float G = 0.0f;
    for (int t = T - 1; t >= 0; t--) {
        // TD target for V(s'_t):
        // = r_{t+1} + V(s'_{t+1})  if t < T-1
        // = 0                       if t == T-1 (terminal)
        float target = 0.0f;
        if (t + 1 < T) {
            target = history[t + 1].reward + history[t + 1].value;
        }

        float delta = target - history[t].value;
        G = delta + config_.lambda * G;

        network_.update(history[t].afterstate, config_.alpha * G);
    }

    return stats;
}

// ============================================================
// 训练主循环
// ============================================================

void TDLambdaTrainer::train() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    printf("=== TD(lambda) Training ===\n");
    printf("  alpha=%.6f, gamma=%.2f, lambda=%.2f\n",
           config_.alpha, config_.gamma, config_.lambda);
    printf("  episodes=%d, report_every=%d\n",
           config_.num_episodes, config_.report_every);
    printf("  save_path=%s\n", config_.save_path.c_str());
    printf("  num_patterns=%d, num_tuples=%d\n",
           TupleNetwork::NUM_PATTERNS, TupleNetwork::NUM_TUPLES);
    printf("===========================\n\n");

    std::vector<EpisodeStats> recent_stats;
    recent_stats.reserve(config_.report_every);

    for (int ep = 1; ep <= config_.num_episodes; ep++) {
        EpisodeStats stats = run_episode();
        recent_stats.push_back(stats);

        if (ep % config_.report_every == 0) {
            report(ep, recent_stats);
            recent_stats.clear();

            std::filesystem::path save_dir = std::filesystem::path(config_.save_path).parent_path();
            if (!save_dir.empty()) {
                std::filesystem::create_directories(save_dir);
            }
            network_.save(config_.save_path);
        }
    }

    std::filesystem::path save_dir = std::filesystem::path(config_.save_path).parent_path();
    if (!save_dir.empty()) {
        std::filesystem::create_directories(save_dir);
    }
    network_.save(config_.save_path);
    printf("\n[Trainer] Training complete. Weights saved to: %s\n", config_.save_path.c_str());
}

// ============================================================
// 报告
// ============================================================

void TDLambdaTrainer::report(int episode, const std::vector<EpisodeStats>& recent_stats) const {
    if (recent_stats.empty()) return;

    int n = (int)recent_stats.size();
    double avg_score = 0;
    int best_tile = 0;
    double avg_moves = 0;
    int count_2048 = 0, count_4096 = 0, count_8192 = 0, count_16384 = 0;

    for (const auto& s : recent_stats) {
        avg_score += s.score;
        avg_moves += s.moves;
        if (s.max_tile > best_tile) best_tile = s.max_tile;
        if (s.max_tile >= 2048)  count_2048++;
        if (s.max_tile >= 4096)  count_4096++;
        if (s.max_tile >= 8192)  count_8192++;
        if (s.max_tile >= 16384) count_16384++;
    }
    avg_score /= n;
    avg_moves /= n;

    printf("[Episode %7d] avg_score=%8.0f | best_tile=%5d | avg_moves=%6.0f | "
           "2048=%.1f%% 4096=%.1f%% 8192=%.1f%% 16384=%.1f%%\n",
           episode, avg_score, best_tile, avg_moves,
           100.0 * count_2048 / n,
           100.0 * count_4096 / n,
           100.0 * count_8192 / n,
           100.0 * count_16384 / n);
}
