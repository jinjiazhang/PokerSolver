# PokerSolver

<p align="center">
  <strong>🃏 高性能无限注德州扑克 GTO 解算器</strong><br>
  基于 Discounted CFR (DCFR) 算法 · 纯 C++20 实现 · 亚秒级 River 求解
</p>

<p align="center">
  <img src="https://img.shields.io/badge/language-C%2B%2B20-blue.svg" alt="C++20">
  <img src="https://img.shields.io/badge/algorithm-DCFR-green.svg" alt="DCFR">
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg" alt="Platform">
  <img src="https://img.shields.io/badge/license-MIT-orange.svg" alt="License">
</p>

---

## 📖 简介

PokerSolver 是一个从零实现的无限注德州扑克 **博弈论最优 (GTO)** 策略解算器。它使用 **Discounted Counterfactual Regret Minimization (DCFR)** 算法，能够针对给定的牌面、底池、筹码深度和双方范围，快速计算出接近 **纳什均衡** 的策略。

### 特性

- 🚀 **极致性能** — River 面 75×75 范围 1000 次迭代仅需 **< 1 秒**
- 🧠 **DCFR 算法** — 相比 vanilla CFR 收敛速度提升数倍
- 📊 **全街支持** — Flop / Turn / River 子博弈求解
- 🎯 **自定义下注树** — 每条街可独立配置 bet/raise 尺寸
- 📝 **标准范围语法** — 支持 `AA`, `AKs`, `TT+`, `22-55`, `AhKh`, `random` 等
- 💾 **JSON 导出** — 策略结果可导出为 JSON 格式，便于二次开发
- 🖥️ **双模式界面** — 命令行参数模式 + 交互式 REPL 模式

---

## ⚡ 性能基准

| 场景 | 范围大小 | 迭代次数 | 用时 | 每次迭代 |
|------|----------|----------|------|----------|
| River (5张公共牌) | 75 × 75 | 1,000 | **0.88s** | 0.9ms |
| River (全范围) | 1,081 × 1,081 | 200 | **23.4s** | 117ms |
| Turn (4张公共牌) | 30 × 30 | 50 | **7.06s** | 141ms |

> 测试环境：Windows, MSVC 19.44, Release (/O2 /arch:AVX2), 单线程

---

## 🔧 构建

### 依赖

- **CMake** ≥ 3.16
- **C++20** 兼容编译器 (MSVC 2019+, GCC 10+, Clang 12+)

### Windows (Visual Studio)

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Linux / macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

构建产物位于 `build/Release/poker_solver` (Windows) 或 `build/poker_solver` (Linux/macOS)。

---

## 🚀 快速开始

### 命令行模式

```bash
# 基础用法：River 面解算
poker_solver \
  --board AhKd5cTs2s \
  --pot 100 \
  --stack 200 \
  --oop-range "AA,KK,QQ,JJ,AKs,AKo" \
  --ip-range "TT+,AQs+,KQs" \
  --iterations 500

# 自定义下注尺寸 + JSON 输出
poker_solver \
  --board AhKdQc \
  --pot 100 --stack 200 \
  --oop-range "AA,KK,QQ,AKs,AQs" \
  --ip-range "JJ,TT,AJs,KQs" \
  --bet-sizes "33,67,100" \
  --raise-sizes "50,100" \
  --iterations 300 \
  --output strategy.json
```

### 交互式模式

```
$ poker_solver --interactive

╔══════════════════════════════════════════════════════════════╗
║           PokerSolver - Interactive Mode                     ║
╚══════════════════════════════════════════════════════════════╝
Type 'help' for available commands.

solver> board AhKd5cTs2s
Board set: AhKd5cTs2s

solver> pot 100
Pot set to: 100

solver> stack 200
Stack set to: 200

solver> oop_range AA,KK,QQ,AKs,AKo
OOP range: 28 combos

solver> ip_range TT+,AQs+,KQs
IP range: 22 combos

solver> iterations 500
Iterations set to: 500

solver> solve
Building game tree...
  Player nodes: 28
  Total nodes:  81
  ...
Solving complete in 0.42 seconds

=== Root Strategy (OOP) ===
Actions: CHECK | BET 33 | BET 67 | BET 100 | ALLIN

Hand    CHECK     BET 33    BET 67    BET 100   ALLIN
------------------------------------------------------
KcKh    0.0%      0.0%      0.0%      0.0%      100.0%
AcAd    0.0%      0.0%      0.0%      0.0%      100.0%
...
```

---

## 📋 命令行参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--board <cards>` | 公共牌面 (3~5张) | *必填* |
| `--pot <amount>` | 初始底池大小 | 10 |
| `--stack <amount>` | 有效筹码深度 | 100 |
| `--oop-range <range>` | OOP (位置劣势方) 范围 | `random` |
| `--ip-range <range>` | IP (位置优势方) 范围 | `random` |
| `--iterations <n>` | CFR 迭代次数 | 200 |
| `--threads <n>` | 线程数 | 1 |
| `--bet-sizes <pcts>` | 下注尺寸 (底池百分比，逗号分隔) | `33,67,100` |
| `--raise-sizes <pcts>` | 加注尺寸 (底池百分比，逗号分隔) | `50,100` |
| `--output <file>` | 输出 JSON 文件路径 | *stdout* |
| `--interactive` | 启动交互式模式 | - |

---

## 🃏 范围语法

| 语法 | 含义 | 组合数 |
|------|------|--------|
| `AA` | 口袋 A (所有花色组合) | 6 |
| `AKs` | AK 同花 | 4 |
| `AKo` | AK 不同花 | 12 |
| `AK` | AK 全部组合 | 16 |
| `TT+` | TT 及以上对子 (TT, JJ, QQ, KK, AA) | 30 |
| `ATs+` | AT同花及以上 (ATs, AJs, AQs, AKs) | 16 |
| `22-55` | 对子范围 22 到 55 | 24 |
| `AhKh` | 指定具体组合 | 1 |
| `random` | 全部可能手牌 | C(47,2) |

支持逗号分隔组合多种语法：`AA,KK,QQ,AKs,AQs+,JTs`

---

## 🏗️ 项目结构

```
PokerSolver/
├── CMakeLists.txt              # CMake 构建配置
├── README.md                   # 项目说明
├── include/                    # 头文件
│   ├── cards.h                 # 牌面表示、位掩码、花色同构
│   ├── hand_eval.h             # 手牌评估器接口
│   ├── game_tree.h             # 博弈树结构与构建
│   ├── cfr_engine.h            # DCFR 求解引擎
│   ├── range_parser.h          # 范围解析器
│   └── cli.h                   # 命令行界面
└── src/                        # 源文件
    ├── main.cpp                # 入口
    ├── cards.cpp               # 牌面工具实现
    ├── hand_eval.cpp           # 素数积哈希手牌评估
    ├── game_tree.cpp           # 博弈树构建 (bet/raise/fold/allin)
    ├── cfr_engine.cpp          # DCFR 核心 + 预计算 matchup 缓存
    ├── range_parser.cpp        # 范围语法解析
    └── cli.cpp                 # CLI 实现 (参数解析 + REPL)
```

---

## 🧠 算法原理

### Counterfactual Regret Minimization (CFR)

CFR 是求解大规模不完美信息博弈的主流算法，其核心思想是：

1. **遍历博弈树**，对每个信息集 (information set) 计算策略
2. **Regret Matching** — 策略与正遗憾值成比例，无正遗憾时使用均匀策略
3. **反事实遗憾更新** — 每个动作的遗憾 = 该动作价值 - 当前节点价值
4. **收敛定理** — 经过 T 次迭代，平均策略的可利用度 (exploitability) 以 O(1/√T) 速率趋近于零

### Discounted CFR (DCFR)

本项目使用 DCFR 变体，通过非均匀加权历史迭代来加速收敛：

| 参数 | 值 | 作用 |
|------|-----|------|
| α (正遗憾折扣) | 1.5 | 正遗憾乘以 `t^α / (t^α + 1)` |
| β (负遗憾折扣) | 0.0 | 负遗憾清零 (完全遗忘) |
| γ (策略和折扣) | 2.0 | 策略和乘以 `(t/(t+1))^γ` |

这使得近期迭代的策略权重更大，早期不成熟的策略影响被快速淡化。

### 性能优化要点

```
┌─────────────────────────────────────────────────────────────────┐
│                     性能优化层次                                  │
├─────────────────────────────────────────────────────────────────┤
│ 算法层  │ DCFR 折扣 → 更少迭代即可收敛                           │
│ 数据层  │ 预计算 matchup 缓存 → showdown O(1) 查表               │
│ 评估层  │ 素数积哈希 + flush/straight 查表 → O(1) 手牌评估        │
│ 存储层  │ flat array 布局 → CPU cache 友好                       │
│ 编译层  │ AVX2 + LTO + /O2 → 极致机器码优化                      │
└─────────────────────────────────────────────────────────────────┘
```

---

## 📤 输出格式

### 控制台输出

```
=== Strategy Summary (Root Node) ===
Player: OOP
Actions: CHECK | BET 33 | BET 67 | BET 100 | ALLIN

Hand    CHECK     BET 33    BET 67    BET 100   ALLIN
------------------------------------------------------
KcKh    0.1%      0.0%      0.0%      0.0%      99.9%
AcAd    0.0%      0.0%      0.0%      0.0%      100.0%
QdQs    1.6%      0.0%      0.0%      0.0%      98.3%
```

### JSON 输出

```json
{
  "solver": "PokerSolver DCFR",
  "iterations": 200,
  "pot": 100,
  "stack": 200,
  "board": "AhKd5cTs2s",
  "root_strategy": {
    "player": 0,
    "actions": ["CHECK", "BET 33", "BET 67", "BET 100", "ALLIN"],
    "hands": {
      "KcKh": [0.0010, 0.0000, 0.0000, 0.0000, 0.9990],
      "AcAd": [0.0000, 0.0000, 0.0000, 0.0000, 1.0000],
      "QdQs": [0.0160, 0.0000, 0.0000, 0.0000, 0.9830]
    }
  }
}
```

---

## 📚 交互式命令参考

| 命令 | 说明 | 示例 |
|------|------|------|
| `board <cards>` | 设置公共牌 | `board AhKdQc` |
| `pot <amount>` | 设置底池 | `pot 100` |
| `stack <amount>` | 设置有效筹码 | `stack 200` |
| `oop_range <range>` | 设置 OOP 范围 | `oop_range AA,KK,AKs` |
| `ip_range <range>` | 设置 IP 范围 | `ip_range TT+,AQs+` |
| `flop_bet_sizes <pcts>` | Flop 下注尺寸 (%) | `flop_bet_sizes 33,67,100` |
| `turn_bet_sizes <pcts>` | Turn 下注尺寸 (%) | `turn_bet_sizes 50,100` |
| `river_bet_sizes <pcts>` | River 下注尺寸 (%) | `river_bet_sizes 33,67` |
| `flop_raise_sizes <pcts>` | Flop 加注尺寸 (%) | `flop_raise_sizes 50,100` |
| `turn_raise_sizes <pcts>` | Turn 加注尺寸 (%) | `turn_raise_sizes 100` |
| `river_raise_sizes <pcts>` | River 加注尺寸 (%) | `river_raise_sizes 50,100` |
| `iterations <n>` | 设置迭代次数 | `iterations 500` |
| `threads <n>` | 设置线程数 | `threads 4` |
| `solve` | 开始求解 | `solve` |
| `export <file>` | 导出 JSON | `export result.json` |
| `help` | 显示帮助 | `help` |
| `quit` | 退出 | `quit` |

---

## 🗺️ 路线图

- [x] DCFR 核心算法
- [x] 素数积哈希手牌评估
- [x] 预计算 matchup 缓存
- [x] Flop / Turn / River 全街支持
- [x] 自定义 bet/raise 尺寸
- [x] 标准范围语法解析
- [x] JSON 策略导出
- [x] 交互式 REPL
- [ ] 多线程并行 CFR 迭代
- [ ] 花色同构 (suit isomorphism) 降低信息集数量
- [ ] Best Response 可利用度计算
- [ ] Turn/River 子博弈的动态 matchup 缓存
- [ ] GUI 可视化界面

---

## 📄 License

MIT License. 详见 [LICENSE](LICENSE) 文件。
