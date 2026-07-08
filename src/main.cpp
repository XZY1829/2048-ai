/**
 * main.cpp — 2048 AI 入口
 *
 * 模式：
 *   ./game2048 train [episodes] [alpha] [lambda]
 *     TD(λ) 训练，贪心 policy，输出学习曲线
 *
 *   ./game2048 play [weight_path] [num_games] [depth]
 *     Expectimax + Learned V(s'), 目标 1M+ 分
 *
 *   ./game2048 benchmark [weight_path] [num_games]
 *     贪心 (no search), 快速评估网络质量
 *
 *   ./game2048
 *     GUI 模式 (SDL2)
 */

#include "tables.h"
#include "trainer.h"
#include "ai.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <random>
#include <chrono>

#ifndef BUILD_TRAINER_ONLY
#include "game.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif
#endif

#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================
// Play: Expectimax + Learned Value Network
// ============================================================

static void run_play_mode(const std::string& weight_path, int num_games, int search_depth) {
    setvbuf(stdout, nullptr, _IONBF, 0);

    TupleNetwork net;
    if (!net.load(weight_path)) {
        printf("[Play] ERROR: Cannot load '%s'. Train first.\n", weight_path.c_str());
        return;
    }
    printf("[Play] Loaded weights: %s\n", weight_path.c_str());
    printf("[Play] Expectimax depth=%d, %d games\n\n", search_depth, num_games);

    AIConfig ai_config;
    ai_config.use_tuple_net = true;
    ai_config.max_depth = search_depth;
    ai_config.prob_threshold = 0.0015f;
    ai_config.cache_depth = search_depth;
    AIEngine ai(ai_config);
    ai.set_tuple_network(&net);

    std::mt19937 rng(42);
    long long total_score = 0;
    int best_tile_all = 0;
    long long total_moves = 0;
    int count_2048 = 0, count_4096 = 0, count_8192 = 0, count_16384 = 0, count_32768 = 0;

    auto t_start = std::chrono::high_resolution_clock::now();

    for (int g = 0; g < num_games; g++) {
        board_t board = 0;
        board = spawn_tile(board, rng);
        board = spawn_tile(board, rng);
        int score = 0, moves = 0;

        while (true) {
            int dir = ai.find_best_move(board);
            if (dir == -1) break;
            score += get_move_score(board, dir);
            board = execute_move(dir, board);
            board = spawn_tile(board, rng);
            moves++;
        }

        int mt = max_tile(board);
        total_score += score;
        total_moves += moves;
        if (mt > best_tile_all) best_tile_all = mt;
        if (mt >= 2048)  count_2048++;
        if (mt >= 4096)  count_4096++;
        if (mt >= 8192)  count_8192++;
        if (mt >= 16384) count_16384++;
        if (mt >= 32768) count_32768++;

        auto t_now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t_now - t_start).count();
        printf("  Game %3d: score=%7d tile=%5d moves=%4d (%.1fs)\n",
               g + 1, score, mt, moves, elapsed);
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t_end - t_start).count();

    printf("\n=== Results (%d games, depth=%d) ===\n", num_games, search_depth);
    printf("  Avg score:   %lld\n", total_score / num_games);
    printf("  Best tile:   %d\n", best_tile_all);
    printf("  Avg moves:   %lld\n", total_moves / num_games);
    printf("  2048+ rate:  %.1f%%\n", 100.0 * count_2048 / num_games);
    printf("  4096+ rate:  %.1f%%\n", 100.0 * count_4096 / num_games);
    printf("  8192+ rate:  %.1f%%\n", 100.0 * count_8192 / num_games);
    printf("  16384+ rate: %.1f%%\n", 100.0 * count_16384 / num_games);
    printf("  32768+ rate: %.1f%%\n", 100.0 * count_32768 / num_games);
    printf("  Time: %.1fs (%.1f s/game)\n", elapsed, elapsed / num_games);
}

// ============================================================
// Benchmark: Greedy (no search)
// ============================================================

static void run_benchmark_mode(const std::string& weight_path, int num_games) {
    setvbuf(stdout, nullptr, _IONBF, 0);

    TupleNetwork net;
    if (!net.load(weight_path)) {
        printf("[Bench] No weights loaded.\n");
    } else {
        printf("[Bench] Loaded: %s\n", weight_path.c_str());
    }
    printf("[Bench] Greedy policy, %d games\n\n", num_games);

    std::mt19937 rng(42);
    long long total_score = 0;
    int best_tile_all = 0;
    int count_2048 = 0, count_4096 = 0, count_8192 = 0, count_16384 = 0;

    for (int g = 0; g < num_games; g++) {
        board_t board = 0;
        board = spawn_tile(board, rng);
        board = spawn_tile(board, rng);
        int score = 0;

        while (true) {
            float best_val = -1e30f;
            int best_dir = -1;
            for (int dir = 0; dir < 4; dir++) {
                board_t after = execute_move(dir, board);
                if (after == board) continue;
                float r = (float)get_move_score(board, dir);
                float val = r + net.evaluate(after);
                if (val > best_val) { best_val = val; best_dir = dir; }
            }
            if (best_dir == -1) break;
            score += get_move_score(board, best_dir);
            board = execute_move(best_dir, board);
            board = spawn_tile(board, rng);
        }

        int mt = max_tile(board);
        total_score += score;
        if (mt > best_tile_all) best_tile_all = mt;
        if (mt >= 2048)  count_2048++;
        if (mt >= 4096)  count_4096++;
        if (mt >= 8192)  count_8192++;
        if (mt >= 16384) count_16384++;
    }

    printf("=== Benchmark (greedy, %d games) ===\n", num_games);
    printf("  Avg score: %lld\n", total_score / num_games);
    printf("  Best tile: %d\n", best_tile_all);
    printf("  2048+: %.1f%%  4096+: %.1f%%  8192+: %.1f%%  16384+: %.1f%%\n",
           100.0*count_2048/num_games, 100.0*count_4096/num_games,
           100.0*count_8192/num_games, 100.0*count_16384/num_games);
}

// ============================================================
// main
// ============================================================

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    init_tables();

    std::string mode = (argc >= 2) ? argv[1] : "";

    if (mode == "train") {
        TrainConfig config;
        config.num_episodes = 100000;
        config.alpha = 0.00025f;  // conservative: 64 tuples × 0.00025 = 0.016 effective
        config.lambda = 0.0f;    // TD(0) is more stable for large networks
        if (argc >= 3) config.num_episodes = std::stoi(argv[2]);
        if (argc >= 4) config.alpha = std::stof(argv[3]);
        if (argc >= 5) config.lambda = std::stof(argv[4]);
        if (argc >= 6) config.save_path = argv[5];
        TDLambdaTrainer trainer(config);
        trainer.train();
        return 0;
    }

    if (mode == "play") {
        std::string wp = "models/tuple_weights.bin";
        int ng = 10, depth = 3;
        if (argc >= 3) wp = argv[2];
        if (argc >= 4) ng = std::stoi(argv[3]);
        if (argc >= 5) depth = std::stoi(argv[4]);
        run_play_mode(wp, ng, depth);
        return 0;
    }

    if (mode == "benchmark") {
        std::string wp = "models/tuple_weights.bin";
        int ng = 100;
        if (argc >= 3) wp = argv[2];
        if (argc >= 4) ng = std::stoi(argv[3]);
        run_benchmark_mode(wp, ng);
        return 0;
    }

#ifndef BUILD_TRAINER_ONLY
#ifdef __EMSCRIPTEN__
    static Game game;
    if (!game.init()) { return 1; }
    static Game* g_game = &game;
    emscripten_set_main_loop([]() { g_game->tick(); }, 0, 1);
#else
    if (mode.empty()) {
        Game game;
        if (!game.init()) { return 1; }
        game.run();
        game.shutdown();
    } else {
        printf("Usage: game2048 [train|play|benchmark]\n");
    }
#endif
#else
    printf("Usage:\n");
    printf("  game2048 train [episodes] [alpha] [lambda]\n");
    printf("  game2048 play [weights] [games] [depth]\n");
    printf("  game2048 benchmark [weights] [games]\n");
#endif
    return 0;
}
