# Value Network 替代手工评估函数 — 技术分析

## 1. 改造目标

保留现有高性能架构，仅将最依赖人工经验的部分交给网络学习：

```
当前架构:                          目标架构:
┌─────────────────────┐           ┌─────────────────────┐
│  Bitboard (uint64)  │           │  Bitboard (uint64)  │
├─────────────────────┤           ├─────────────────────┤
│  Lookup Table       │           │  Lookup Table       │
│  (O(1) 移动/分数)   │           │  (O(1) 移动/分数)   │
├─────────────────────┤           ├─────────────────────┤
│  Expectimax Search  │           │  Expectimax Search  │
│  (自适应深度+裁剪)   │           │  (自适应深度+裁剪)   │
├─────────────────────┤           ├─────────────────────┤
│  Hand-crafted Eval  │  ──替换──▶ │  Learned Value Net  │
│  heur_table[65536]  │           │  f(board) → score   │
│  5 个手调权重        │           │  TD(0) 自学习       │
└─────────────────────┘           └─────────────────────┘
```

**核心价值：** 搜索 + 学习（Planning + Function Approximation）架构是现代游戏 AI 的标准范式。保留 Expectimax 利用已知环境模型进行规划的优势，同时把最依赖人工经验的评估函数交给网络自动学习。

---

## 2. 当前 evaluate_board 分析

### 现状

```cpp
// tables.h — 当前评估函数
inline float evaluate_board(board_t board) {
    float score = 0.0f;
    board_t transposed = transpose(board);
    score += heur_table[(board >> 48) & 0xFFFF];       // 4 行
    score += heur_table[(board >> 32) & 0xFFFF];
    score += heur_table[(board >> 16) & 0xFFFF];
    score += heur_table[(board >>  0) & 0xFFFF];
    score += heur_table[(transposed >> 48) & 0xFFFF];  // 4 列
    score += heur_table[(transposed >> 32) & 0xFFFF];
    score += heur_table[(transposed >> 16) & 0xFFFF];
    score += heur_table[(transposed >>  0) & 0xFFFF];
    return score;
}
```

- **耗时：** ~5ns/call（8 次数组查表 + 加法）
- **特征：** EMPTY_WEIGHT(270) + MONO_WEIGHT(47) + SMOOTH_WEIGHT(10) + MERGE_WEIGHT(11) + POSITION_WEIGHT(1.5)
- **局限：** 5 个权重全靠手调，无法捕捉更复杂的棋盘模式

### 问题

| 问题 | 说明 |
|---|---|
| 线性加权 | 特征之间的非线性交互被忽略 |
| 全局模式盲 | 按行/列独立评估，无法学到跨行列的全局模式（如 snake pattern） |
| 人工天花板 | 权重需要专家经验或自动搜索（CMA-ES 等），但搜索空间仍受限于手选的 5 个特征 |
| 泛化性差 | 面对从未见过的高 tile 组合，手工启发式可能失效 |

---

## 3. 方案对比

### 方案 A：N-Tuple Network（学术主流）

**原理：** 把棋盘按预设模式（tuple）取出几个格子的 tile 值，拼成一个索引，查 weight table。类似于把 `heur_table` 从"单行查表"推广到"多格组合查表"。

```
N-Tuple 示例（6-tuple pattern）：
┌──┬──┬──┬──┐
│ A│ B│ C│ D│    Pattern 1: (0,1,2,3,4,5) → 第一行4格+第二行前2格
├──┼──┼──┼──┤
│ E│ F│  │  │    索引 = A×15⁵ + B×15⁴ + C×15³ + D×15² + E×15 + F
├──┼──┼──┼──┤
│  │  │  │  │    对应一个 lookup weight
├──┼──┼──┼──┤
│  │  │  │  │
└──┴──┴──┴──┘
```

**评估：** 所有 pattern（含旋转、镜像对称共 32~64 个变体）的 weight 之和。

| 维度 | 值 |
|---|---|
| 推理耗时 | ~20-50ns（纯查表相加，无乘法） |
| 参数量 | 4-tuple: 4×65536 = 262K, 6-tuple: ~数十 M（15⁶≈11M per pattern） |
| 内存占用 | 4-tuple: ~1MB, 6-tuple: ~200-500MB |
| 训练方法 | TD(0) / TD(λ)，afterstate learning |
| 学术成绩 | 4-tuple: 32768 达到率~60%, 6-tuple: 32768~95%+, 65536 可达 |
| 实现复杂度 | 低（核心就是查表+加法+TD更新） |
| 与现有架构兼容 | 完美（evaluate_board 替换为 tuple_eval，纯查表） |

**优点：**
- 推理速度极快（接近当前 heur_table 方案），与 Expectimax 百万次/秒搜索完美匹配
- 学术上 2048 SOTA 一直是 N-Tuple Network + Expectimax
- 自动学习 pattern 间的组合关系，无需手调权重
- TD(0) 训练简单稳定，自博弈数千局即可收敛

**缺点：**
- 6-tuple 内存占用大（但可用 4-tuple 折中）
- pattern 选择仍需设计（但有成熟模板可复用）

---

### 方案 B：MLP (Multi-Layer Perceptron)

**原理：** 把棋盘 16 格编码为输入向量，通过全连接层学习评估值。

```
输入编码（16格 → 256维 one-hot）：
每格 tile 值 one-hot 编码为 16 维 [0=空, 1=2, 2=4, ..., 15=32768]
16格 × 16维 = 256 维输入

网络结构：
Input(256) → Dense(128, ReLU) → Dense(64, ReLU) → Dense(1)
```

| 维度 | 值 |
|---|---|
| 推理耗时 | ~500ns - 2μs（矩阵乘法） |
| 参数量 | 256×128 + 128×64 + 64×1 ≈ 41K |
| 内存占用 | ~200KB |
| 训练方法 | TD(0)，需要学习率调度、batch normalization |
| 预期成绩 | 16384 达到率 ~80-90%（受限于推理速度→搜索深度） |
| 实现复杂度 | 中（需手写前向传播或嵌入轻量推理库） |
| 与现有架构兼容 | 需权衡推理速度 vs 搜索深度 |

**优点：**
- 参数量小，内存友好
- 能捕捉全局非线性模式
- 实现概念清晰，面试展示"搜索+学习"范式直观

**缺点：**
- 推理比 N-Tuple 慢 10-100 倍，直接影响 Expectimax 搜索深度
- 当前搜索 5ns/eval × 100万节点 = 5ms；换 MLP 1μs/eval × 100万节点 = 1000ms
- 需要大幅降低搜索深度（depth 3-4）来保证每步 <50ms，削弱规划能力
- 训练收敛慢，需更多自博弈轮次

---

### 方案 C：4-Tuple Network（推荐起步）

方案 A 的轻量版本。用 4 格组合作为 pattern，内存可控，推理速度接近当前方案。

```
Pattern 设计（共 8 个 4-tuple，含对称变体 32 个）：

Pattern 1:          Pattern 2:          Pattern 3:
┌──┬──┬──┬──┐      ┌──┬──┬──┬──┐      ┌──┬──┬──┬──┐
│ A│ B│ C│ D│      │ A│ B│  │  │      │ A│ B│  │  │
├──┼──┼──┼──┤      ├──┼──┼──┼──┤      ├──┼──┼──┼──┤
│  │  │  │  │      │ C│ D│  │  │      │  │ C│ D│  │
├──┼──┼──┼──┤      ├──┼──┼──┼──┤      ├──┼──┼──┼──┤
│  │  │  │  │      │  │  │  │  │      │  │  │  │  │
├──┼──┼──┼──┤      ├──┼──┼──┼──┤      ├──┼──┼──┼──┤
│  │  │  │  │      │  │  │  │  │      │  │  │  │  │
└──┴──┴──┴──┘      └──┴──┴──┴──┘      └──┴──┴──┴──┘
横向4格连续         2×2 方块             L 型

索引: 15⁴ = 50625 entries/pattern
8 patterns × 50625 = 405,000 权重 ≈ 1.6 MB
加上对称（旋转+镜像 4+4=8 变体）: 评估时 64 次查表相加
```

| 维度 | 值 |
|---|---|
| 推理耗时 | ~30-50ns（64 次查表相加） |
| 参数量 | ~400K float |
| 内存占用 | ~1.6 MB |
| 训练方法 | TD(0) afterstate learning |
| 预期成绩 | 16384 达到率 ~90%+, 32768 偶尔达到 |
| 实现复杂度 | 低 |
| 与现有架构兼容 | 完美 |

---

## 4. 推荐方案及理由

### 推荐路线：方案 C（4-Tuple）起步 → 升级方案 A（6-Tuple）

| 考量 | 分析 |
|---|---|
| 搜索兼容性 | N-Tuple 查表速度与 Expectimax 百万节点搜索匹配；MLP 会拖慢 200 倍 |
| 学习效果 | 4-Tuple 已远超手工权重；6-Tuple 可达 SOTA |
| 实现成本 | 核心代码 ~200 行 C++，无需引入第三方 ML 库 |
| 面试价值 | 展示 TD Learning + Function Approximation + Search 的完整 pipeline |
| 渐进升级 | 先跑通 4-Tuple，验证框架正确，再升级 pattern 规模 |

**为什么不用 MLP：** 在 2048 这个场景下，评估函数被调用百万次/秒。MLP 的矩阵乘法（~1μs）比查表（~30ns）慢 30 倍，直接将搜索深度从 5-7 砍到 2-3，规划能力大幅退化。N-Tuple 本质上是一个"用查表实现的超大稀疏线性模型"，完美契合高频调用场景。

---

## 5. 系统设计

### 5.1 Evaluator 抽象接口

```cpp
// include/evaluator.h
class Evaluator {
public:
    virtual ~Evaluator() = default;
    virtual float evaluate(board_t board) = 0;
    virtual void load(const std::string& path) = 0;
    virtual void save(const std::string& path) = 0;
};

class HeuristicEvaluator : public Evaluator {
    float evaluate(board_t board) override { return evaluate_board(board); }
    void load(const std::string&) override {}
    void save(const std::string&) override {}
};

class NTupleEvaluator : public Evaluator {
    float evaluate(board_t board) override;
    void load(const std::string& path) override;
    void save(const std::string& path) override;
    void td_update(board_t s, board_t s_next, float reward, float alpha);
private:
    std::vector<std::vector<float>> weights_;  // [pattern_id][index]
    // pattern 定义 + 对称变换逻辑
};
```

### 5.2 N-Tuple 评估核心逻辑

```cpp
float NTupleEvaluator::evaluate(board_t board) {
    float value = 0.0f;
    for (int p = 0; p < num_patterns_; p++) {
        // 对每个 pattern 的所有对称变体（旋转×4 + 镜像×2 = 8）
        for (int sym = 0; sym < 8; sym++) {
            board_t transformed = apply_symmetry(board, sym);
            int index = extract_tuple_index(transformed, patterns_[p]);
            value += weights_[p][index];
        }
    }
    return value;
}
```

### 5.3 TD(0) Afterstate Learning

核心思想：学习 **afterstate value**（玩家执行动作后、随机方块放置前的状态价值）。

```
训练一局的流程:

    s₀ → (action) → s₀' → (random tile) → s₁ → (action) → s₁' → ...

    afterstate: s₀', s₁', s₂', ...（玩家动作后的中间状态）

    TD(0) 更新:
    V(s_t') ← V(s_t') + α × [r_{t+1} + V(s_{t+1}') - V(s_t')]

    其中:
    - V(s') = NTupleEvaluator.evaluate(s')
    - r_{t+1} = 本步合并得分
    - α = 学习率（0.0025 ~ 0.01）
```

```cpp
void NTupleEvaluator::td_update(board_t s, board_t s_next, float reward, float alpha) {
    float v_s = evaluate(s);
    float v_next = evaluate(s_next);  // 终局时 v_next = 0
    float td_error = reward + v_next - v_s;

    // 更新所有贡献到 v_s 的权重
    for (int p = 0; p < num_patterns_; p++) {
        for (int sym = 0; sym < 8; sym++) {
            board_t transformed = apply_symmetry(s, sym);
            int index = extract_tuple_index(transformed, patterns_[p]);
            weights_[p][index] += alpha * td_error;
        }
    }
}
```

### 5.4 训练流程

```
┌────────────────────────────────────────────────────┐
│                  训练循环                            │
│                                                    │
│  for episode = 1 to N:                             │
│    board = new_game()                              │
│    while not game_over:                            │
│      dir = greedy_policy(board)  // 或 ε-greedy    │
│      afterstate = execute_move(dir, board)         │
│      reward = move_score                           │
│      board = spawn_random_tile(afterstate)         │
│      s_next_after = next afterstate (peek ahead)   │
│      td_update(afterstate, s_next_after, reward)   │
│    end while                                       │
│    // 终局: td_update(last_afterstate, 0, 0)       │
│  end for                                           │
└────────────────────────────────────────────────────┘

训练策略（policy）:
- 训练初期: 贪心（选 evaluate 最高的方向）
- 可选: 搜索增强（depth=1 的 Expectimax 辅助选方向）
- 注意: 训练时不需要深度搜索，只用当前网络贪心即可
         深度搜索在 inference（实际游戏）时使用
```

### 5.5 Pattern 选择（4-Tuple 起步版）

```
经典 4-Tuple 模板:

Pattern 0: 横排 (0,1,2,3)
┌──┬──┬──┬──┐
│ *│ *│ *│ *│
├──┼──┼──┼──┤
│  │  │  │  │
├──┼──┼──┼──┤
│  │  │  │  │
├──┼──┼──┼──┤
│  │  │  │  │
└──┴──┴──┴──┘

Pattern 1: 竖排 (0,4,8,12)
┌──┬──┬──┬──┐
│ *│  │  │  │
├──┼──┼──┼──┤
│ *│  │  │  │
├──┼──┼──┼──┤
│ *│  │  │  │
├──┼──┼──┼──┤
│ *│  │  │  │
└──┴──┴──┴──┘

Pattern 2: 2×2 方块 (0,1,4,5)
┌──┬──┬──┬──┐
│ *│ *│  │  │
├──┼──┼──┼──┤
│ *│ *│  │  │
├──┼──┼──┼──┤
│  │  │  │  │
├──┼──┼──┼──┤
│  │  │  │  │
└──┴──┴──┴──┘

Pattern 3: L 型 (0,1,2,4)
┌──┬──┬──┬──┐
│ *│ *│ *│  │
├──┼──┼──┼──┤
│ *│  │  │  │
├──┼──┼──┼──┤
│  │  │  │  │
├──┼──┼──┼──┤
│  │  │  │  │
└──┴──┴──┴──┘

Pattern 4: 斜 L (0,1,5,6)
┌──┬──┬──┬──┐
│ *│ *│  │  │
├──┼──┼──┼──┤
│  │ *│ *│  │
├──┼──┼──┼──┤
│  │  │  │  │
├──┼──┼──┼──┤
│  │  │  │  │
└──┴──┴──┴──┘

Pattern 5: T 型 (1,4,5,6)
┌──┬──┬──┬──┐
│  │ *│  │  │
├──┼──┼──┼──┤
│ *│ *│ *│  │
├──┼──┼──┼──┤
│  │  │  │  │
├──┼──┼──┼──┤
│  │  │  │  │
└──┴──┴──┴──┘

每个 pattern 经 8 种对称变换（4旋转 × 2镜像）→ 48 个 tuple 索引查表
```

---

## 6. 三种运行模式

### 6.1 正常模式（现有游戏）

```
evaluate_board() 调用 NTupleEvaluator::evaluate()
搜索深度不变，体验不变
用户感知: "AI 变强了"
```

### 6.2 跑分模式（Benchmark）

```
目的: AI 性能测试，快速跑完 N 局统计分数分布

特点:
- 无 SDL 渲染（或只在每局最后渲染终态）
- 每帧执行多步（不受动画帧率限制）
- 输出: 平均分、最高分、各 tile 达成率、平均步数、平均耗时/步

触发: 命令行参数 --benchmark -n 100
```

### 6.3 训练模式（Training）

```
目的: 纯 CLI 环境运行 TD Learning 训练循环

特点:
- 完全不链接 SDL（编译时条件排除）
- 贪心 policy + TD(0) 更新
- 每 1000 局输出训练进度（平均分趋势）
- 支持断点续训（save/load weights）

触发: 单独的 train 可执行文件，或 --train 参数
编译: cmake -DBUILD_TRAINER=ON
```

---

## 7. 训练超参数参考

| 参数 | 推荐值 | 说明 |
|---|---|---|
| 学习率 α | 0.0025 | 4-Tuple 经验值，过大震荡过小太慢 |
| 训练局数 | 10,000 ~ 100,000 | 4-Tuple 约 5000 局收敛 |
| Tile 编码 | 0~15 (raw log2) | 直接用 Bitboard 中的值 |
| 对称变体 | 8 (4旋转+2镜像) | 数据增强，加速收敛 |
| Policy（训练时） | 贪心（选 eval 最高方向） | 不用搜索，快 |
| Policy（推理时） | Expectimax depth=3~7 | 保留搜索优势 |
| 终局奖励 | 0 | V(terminal) = 0 |
| 折扣因子 γ | 1.0 | 无折扣（afterstate 框架不需要） |
| 权重初始化 | 全零 | TD 会自动调整 |

---

## 8. 预期收益对比

| 指标 | 当前（手工权重） | 4-Tuple TD | 6-Tuple TD |
|---|---|---|---|
| 16384 达成率 | 70% | ~90%+ | ~99% |
| 32768 达成率 | 0% | ~10-30% | ~60-80% |
| 65536 达成率 | 0% | 0% | ~1-5% |
| evaluate 耗时 | ~5ns | ~30-50ns | ~100-200ns |
| 搜索节点/步 | ~500万 | ~200万 | ~50-100万 |
| 每步总耗时 | ~9ms | ~15-30ms | ~30-80ms |
| 内存 | 256KB | ~1.6MB | ~200-500MB |

---

## 9. 实现计划

### Phase 1: 框架搭建（~2h）

1. 创建 `include/evaluator.h` — Evaluator 接口 + HeuristicEvaluator + NTupleEvaluator
2. 修改 `ai.cpp` — 将 `evaluate_board()` 调用改为 `evaluator_->evaluate()`
3. AI 行为不变（先用 HeuristicEvaluator 验证接口正确）

### Phase 2: 训练模式（~2h）

1. 创建 `src/trainer.cpp` — TD(0) 训练主循环
2. 修改 `CMakeLists.txt` — 添加 `BUILD_TRAINER` 选项
3. 实现 save/load（简单二进制文件）
4. 验证: 训练 1000 局后平均分上升

### Phase 3: 跑分模式（~1h）

1. 在 `game.cpp` 或单独文件实现 benchmark 逻辑
2. 无动画快速执行 + 统计输出
3. 验证: 对比 HeuristicEvaluator vs 训练后的 NTupleEvaluator

### Phase 4: 训练 + 调优（~3-8h 机器时间）

1. 跑 10000~50000 局训练
2. 观察 16384 达成率收敛曲线
3. 可选: 升级到 6-Tuple 看效果提升

---

## 10. 文件变更清单

| 文件 | 操作 | 内容 |
|---|---|---|
| `include/evaluator.h` | 新增 | Evaluator 接口 + N-Tuple 实现 |
| `src/ntuple_eval.cpp` | 新增 | N-Tuple 评估、TD 更新、对称变换 |
| `src/trainer.cpp` | 新增 | 训练主循环（纯 CLI，不链接 SDL） |
| `include/ai.h` | 修改 | AIEngine 持有 Evaluator* |
| `src/ai.cpp` | 修改 | evaluate_board() → evaluator_->evaluate() |
| `src/game.cpp` | 修改 | benchmark 模式逻辑 |
| `src/main.cpp` | 修改 | 解析 --benchmark / --train 参数 |
| `CMakeLists.txt` | 修改 | BUILD_TRAINER 选项，trainer 可执行目标 |
| `models/` | 新增目录 | 存放训练好的权重文件 |

---

## 11. 面试价值

本改造完成后，项目展示的完整技术栈：

| 层次 | 技术 | 面试考点 |
|---|---|---|
| 数据结构 | Bitboard + Lookup Table | 位运算、空间换时间、O(1) 操作设计 |
| 搜索算法 | Expectimax + 概率裁剪 + 缓存 | 博弈树搜索、期望值计算、剪枝策略 |
| 机器学习 | N-Tuple Network + TD(0) | 强化学习、值函数近似、时序差分 |
| 工程优化 | 查表推理、对称变换数据增强 | 性能 profiling、内存 vs 速度权衡 |
| 系统设计 | 多态 Evaluator + 多模式编译 | 接口设计、条件编译、CLI 工具链 |
| 部署 | Emscripten WASM + GitHub Pages | 跨平台编译、Web 性能优化 |

**一句话总结：** 这是一个完整的 "Search + Learning" 系统——用 Expectimax 搜索做规划（planning），用 TD Learning 学习价值函数（function approximation），两者结合实现超越人类的 2048 AI。
