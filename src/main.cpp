/**
 * main.cpp — 核心逻辑验证 + AI 基准测试
 *
 * 功能:
 *   1. 验证查找表和移动逻辑的正确性
 *   2. 运行 AI 自动对局并统计结果
 *   3. 终端显示棋盘状态（简易文本模式）
 *
 * 编译运行:
 *   mkdir build && cd build
 *   cmake .. && cmake --build .
 *   ./game2048
 */

#include "board.h"
#include "tables.h"
#include "ai.h"

#include <iostream>
#include <iomanip>
#include <random>
#include <chrono>
#include <string>
#include <map>

#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================
// 辅助: 终端打印棋盘
// ============================================================

/**
 * 以文本形式打印棋盘。
 *
 * 输出格式:
 *   +------+------+------+------+
 *   |    2 |    4 |   16 |      |
 *   +------+------+------+------+
 *   |    8 |    2 |      |    2 |
 *   +------+------+------+------+
 *   |      |      |    4 |      |
 *   +------+------+------+------+
 *   |      |      |      |    2 |
 *   +------+------+------+------+
 */
void print_board(board_t board) {
    std::string sep = "+------+------+------+------+";
    std::cout << sep << "\n";
    for (int r = 0; r < 4; r++) {
        std::cout << "|";
        for (int c = 0; c < 4; c++) {
            int val = get_tile_value(board, r, c);
            if (val == 0)
                std::cout << "      |";
            else
                std::cout << std::setw(5) << val << " |";
        }
        std::cout << "\n" << sep << "\n";
    }
}

// ============================================================
// 测试 1: 查找表正确性验证
// ============================================================

/**
 * 验证几个已知的移动案例。
 */
bool test_tables() {
    std::cout << "=== 查找表正确性验证 ===\n\n";
    bool all_pass = true;

    // 测试用例: {输入行(hex), 期望左移结果(hex), 期望得分}
    struct TestCase {
        row_t input;
        row_t expected_left;
        int expected_score;
        const char* desc;
    };

    TestCase cases[] = {
        // [2, 2, 0, 0] → [3, 0, 0, 0], 得分 = 2^3 = 8 (tile 4合并为8)
        // 编码: 2=tile4的log2=2, 所以 [2,2,0,0]=0x2200, [3,0,0,0]=0x3000
        {0x1100, 0x2000, 4,   "[2,2,_,_] → [4,_,_,_] score=4"},
        {0x1010, 0x2000, 4,   "[2,_,2,_] → [4,_,_,_] score=4"},
        {0x1001, 0x2000, 4,   "[2,_,_,2] → [4,_,_,_] score=4"},
        {0x1110, 0x2100, 4,   "[2,2,2,_] → [4,2,_,_] score=4"},
        {0x1111, 0x2200, 8,   "[2,2,2,2] → [4,4,_,_] score=8"},
        {0x2211, 0x3200, 12,  "[4,4,2,2] → [8,4,_,_] score=12"},
        {0x0000, 0x0000, 0,   "[_,_,_,_] → [_,_,_,_] score=0"},
        {0x1000, 0x1000, 0,   "[2,_,_,_] → [2,_,_,_] score=0 (不动)"},
        {0x1230, 0x1230, 0,   "[2,4,8,_] → [2,4,8,_] score=0 (无合并)"},
    };

    for (auto& tc : cases) {
        row_t result = move_left_table[tc.input];
        int score = score_table[tc.input];
        bool pass = (result == tc.expected_left && score == tc.expected_score);

        std::cout << (pass ? "[PASS]" : "[FAIL]") << " " << tc.desc;
        if (!pass) {
            std::cout << "\n       got: 0x" << std::hex << result
                      << " score=" << std::dec << score;
            all_pass = false;
        }
        std::cout << "\n";
    }

    // 验证翻转对称性: move_right_table[row] == reverse(move_left_table[reverse(row)])
    std::cout << "\n验证左右移动对称性 (随机抽样 1000 行)... ";
    std::mt19937 rng(42);
    bool sym_pass = true;
    for (int i = 0; i < 1000; i++) {
        row_t row = rng() & 0xFFFF;
        row_t rev_input = reverse_row(row);
        row_t expected = reverse_row(move_left_table[rev_input]);
        if (move_right_table[row] != expected) {
            sym_pass = false;
            break;
        }
    }
    std::cout << (sym_pass ? "[PASS]" : "[FAIL]") << "\n";
    all_pass = all_pass && sym_pass;

    std::cout << "\n";
    return all_pass;
}

// ============================================================
// 测试 2: 棋盘操作验证
// ============================================================

bool test_board_ops() {
    std::cout << "=== 棋盘操作验证 ===\n\n";
    bool all_pass = true;

    // 测试 set_cell / get_cell 往返
    board_t b = 0;
    b = set_cell(b, 0, 0, 5);  // 左上角设为 5 (tile=32)
    b = set_cell(b, 3, 3, 1);  // 右下角设为 1 (tile=2)
    b = set_cell(b, 1, 2, 3);  // (1,2) 设为 3 (tile=8)

    bool pass1 = (get_cell(b, 0, 0) == 5);
    bool pass2 = (get_cell(b, 3, 3) == 1);
    bool pass3 = (get_cell(b, 1, 2) == 3);
    bool pass4 = (get_cell(b, 2, 2) == 0); // 未设置的应为 0

    std::cout << (pass1 ? "[PASS]" : "[FAIL]") << " set/get (0,0)=5\n";
    std::cout << (pass2 ? "[PASS]" : "[FAIL]") << " set/get (3,3)=1\n";
    std::cout << (pass3 ? "[PASS]" : "[FAIL]") << " set/get (1,2)=3\n";
    std::cout << (pass4 ? "[PASS]" : "[FAIL]") << " get (2,2)=0 (empty)\n";
    all_pass = all_pass && pass1 && pass2 && pass3 && pass4;

    // 测试 count_empty
    bool pass5 = (count_empty(b) == 13); // 16-3=13 个空
    std::cout << (pass5 ? "[PASS]" : "[FAIL]") << " count_empty=13\n";
    all_pass = all_pass && pass5;

    // 测试转置
    // 设置一个简单的 board，验证转置后行列互换
    board_t orig = 0;
    orig = set_cell(orig, 0, 1, 7); // (0,1) = 7
    board_t trans = transpose(orig);
    bool pass6 = (get_cell(trans, 1, 0) == 7); // 转置后 (1,0) = 7
    std::cout << (pass6 ? "[PASS]" : "[FAIL]") << " transpose: (0,1)→(1,0)\n";
    all_pass = all_pass && pass6;

    std::cout << "\n";
    return all_pass;
}

// ============================================================
// 测试 3: 完整移动验证
// ============================================================

bool test_full_move() {
    std::cout << "=== 完整移动验证 ===\n\n";

    // 构造一个已知局面:
    // [2, 2, 4, 4]    → 左移 → [4, 8, _, _]
    // [_, _, _, _]
    // [_, 2, _, _]
    // [_, _, _, 2]
    board_t b = 0;
    b = set_cell(b, 0, 0, 1); // 2
    b = set_cell(b, 0, 1, 1); // 2
    b = set_cell(b, 0, 2, 2); // 4
    b = set_cell(b, 0, 3, 2); // 4
    b = set_cell(b, 2, 1, 1); // 2
    b = set_cell(b, 3, 3, 1); // 2

    std::cout << "移动前:\n";
    print_board(b);

    board_t left = board_move_left(b);
    std::cout << "\n向左移动后:\n";
    print_board(left);

    // 验证第一行: [2,2,4,4] → [4,8,_,_] = [2,3,0,0]
    bool pass = (get_cell(left, 0, 0) == 2 && get_cell(left, 0, 1) == 3 &&
                 get_cell(left, 0, 2) == 0 && get_cell(left, 0, 3) == 0);
    std::cout << "\n第一行合并验证: " << (pass ? "[PASS]" : "[FAIL]") << "\n\n";
    return pass;
}

// ============================================================
// AI 基准测试: 自动对局
// ============================================================

/**
 * 让 AI 自动玩一局，返回最终分数和最大 tile。
 */
struct GameResult {
    int score;
    int max_tile_val;
    int moves;
    float avg_move_ms;
};

GameResult play_one_game(AIEngine& ai, std::mt19937& rng, bool verbose = false) {
    board_t board = 0;
    board = spawn_tile(board, rng);
    board = spawn_tile(board, rng);

    int score = 0;
    int moves = 0;
    float total_ms = 0;

    while (true) {
        if (is_game_over(board)) break;

        int dir = ai.find_best_move(board);
        if (dir < 0) break; // 不可能，但安全检查

        total_ms += ai.last_stats().elapsed_ms;

        // 计算合并得分
        score += get_move_score(board, dir);

        // 执行移动
        board = execute_move(dir, board);

        // 放新方块
        board = spawn_tile(board, rng);
        moves++;

        // 详细模式: 打印每步
        if (verbose && moves % 100 == 0) {
            std::cout << "  Move " << moves << " | Score: " << score
                      << " | Max: " << max_tile(board) << "\n";
        }
    }

    return {score, max_tile(board), moves, moves > 0 ? total_ms / moves : 0};
}

void run_benchmark(int num_games) {
    std::cout << "=== AI 基准测试 (" << num_games << " 局) ===\n\n";

    AIEngine ai;
    std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());

    int total_score = 0;
    int max_score = 0;
    std::map<int, int> tile_counts; // tile值 → 达成次数
    float total_time = 0;

    for (int g = 0; g < num_games; g++) {
        auto result = play_one_game(ai, rng);
        total_score += result.score;
        max_score = std::max(max_score, result.score);
        tile_counts[result.max_tile_val]++;
        total_time += result.avg_move_ms * result.moves;

        std::cout << "  Game " << (g + 1) << "/" << num_games
                  << " | Score: " << std::setw(6) << result.score
                  << " | Max: " << std::setw(5) << result.max_tile_val
                  << " | Moves: " << std::setw(4) << result.moves
                  << " | " << std::fixed << std::setprecision(1)
                  << result.avg_move_ms << " ms/move\n";
    }

    std::cout << "\n--- 统计结果 ---\n";
    std::cout << "平均分: " << total_score / num_games << "\n";
    std::cout << "最高分: " << max_score << "\n";
    std::cout << "总耗时: " << std::fixed << std::setprecision(1) << total_time / 1000.0f << " s\n";
    std::cout << "\n最高 Tile 达成率:\n";
    for (auto it = tile_counts.rbegin(); it != tile_counts.rend(); ++it) {
        float pct = 100.0f * it->second / num_games;
        std::cout << "  " << std::setw(5) << it->first << ": "
                  << it->second << "/" << num_games
                  << " (" << std::fixed << std::setprecision(1) << pct << "%)\n";
    }
}

// ============================================================
// 主函数
// ============================================================

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    // 强制关闭 stdout 缓冲（确保输出立即可见）
    std::ios_base::sync_with_stdio(false);
    std::cout << std::unitbuf;

    std::cout << "============================================\n";
    std::cout << "  2048 AI — 核心逻辑验证 + 基准测试\n";
    std::cout << "============================================\n\n";

    // 初始化查找表（必须最先调用）
    std::cout << "初始化查找表... ";
    auto t0 = std::chrono::high_resolution_clock::now();
    init_tables();
    auto t1 = std::chrono::high_resolution_clock::now();
    float init_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    std::cout << "完成 (" << std::fixed << std::setprecision(1) << init_ms << " ms)\n\n";

    // 运行测试
    bool pass = true;
    pass = test_tables() && pass;
    pass = test_board_ops() && pass;
    pass = test_full_move() && pass;

    if (!pass) {
        std::cout << "!!! 存在测试失败，请检查 !!!\n";
        return 1;
    }
    std::cout << "所有验证测试通过 ✓\n\n";

    // AI 基准测试
    std::cout << "--------------------------------------------\n\n";
    run_benchmark(10); // 默认跑 10 局

    return 0;
}
