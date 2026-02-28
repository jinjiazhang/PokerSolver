# PokerSolver

<p align="center">
  <strong>🃏 高性能无限注德州扑克 GTO 解算器</strong><br>
  基于 Discounted CFR (DCFR) 算法 · 纯 C++20 实现 · 多线程并行 · 亚秒级 River 求解
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

- 🚀 **极致性能** — River 全范围 1081×1081 仅需 **0.5ms/iter**，Flop 全树解算新增 MCCFR 可实现 **72x 提速**
- ⚡ **多线程并行** — Chance 节点级并行 + 无锁 Thread-Local 累加器
- 🎲 **MCCFR 外部采样** — 针对巨大深度 (Flop/Preflop) 的蒙特卡洛发牌采样，几秒内逼近纳什均衡
- 🎨 **花色同构** — 自动识别可交换花色，单色面板信息集降低 **71%**
- 🧠 **DCFR 算法** — 相比 vanilla CFR 收敛速度提升数倍
- 📊 **全街支持** — Flop / Turn / River 子博弈求解
- 🎯 **灵活下注树** — OOP/IP 独立配置 bet/raise/donk 尺寸，All-in 阈值自动合并
- 📝 **标准范围语法** — 支持 `AA`, `AKs`, `TT+`, `22-55`, `AhKh`, `AA:0.75`, `random` 等
- 📉 **实时收敛跟踪** — Best Response 可利用度实时输出，达标自动停止
- 💾 **JSON 导出** — 策略可导出 JSON 格式
- 🖥️ **双模式界面** — 命令行参数模式 + 交互式 REPL 模式

---

## ⚡ 性能基准

| 场景 | 范围大小 | 迭代次数 | 每次迭代 | 用时 |
|------|----------|----------|----------|------|
| River | 1,081 × 1,081 | 50 | **0.5ms** | 0.02s |
| Turn | 20 × 36 | 50 | **1.8ms** | 0.09s |
| Turn (全范围) | 1,128 × 1,128 | 10 | **540ms** | 6.20s |
| Flop (全树展开) | 32 × 32 | 2000 | **298ms** | 597s |
| Flop (**MCCFR 采样**) | 32 × 32 | 2000 | **4.1ms** | **8.3s** |

> Flop 测试环境：Board AhKdQc, OOP范围 32 组合, IP范围 32 组合, 8 线程并行

### 累计优化加速比

| 场景 | 初始版本 | 当前版本 | 加速比 |
|------|---------|---------|--------|
| Turn 20×36 | 297 ms/iter | 1.8 ms/iter | **165×** |
| River 1081×1081 | 117 ms/iter | 0.5 ms/iter | **234×** |
| Flop 35×48 | 不可用 | 230 ms/iter | ∞ |

### 花色同构降维

| 牌面类型 | 示例 | 原始 → 规范组 | 降幅 |
|----------|------|---------------|------|
| 单色 Flop | `AhKhQh` | 1,176 → 344 | **71%** |
| 四同花 River | `AhKhQhJh2c` | 1,081 → 652 | **40%** |
| 两色 Flop | `AhKh5c` | 1,176 → 721 | **39%** |

> 测试环境：Windows, MSVC 19.44, Release (/O2 /arch:AVX2)

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
  --pot 100 --stack 200 \
  --oop-range "AA,KK,QQ,JJ,AKs,AKo" \
  --ip-range "TT+,AQs+,KQs" \
  --iterations 500

# 完整示例：多线程 + 自定义下注 + 收敛停止
poker_solver \
  --board AhKdQcTs \
  --pot 100 --stack 200 \
  --oop-range "AA,KK,QQ,AKs,AQs" \
  --ip-range "JJ,TT,AJs,KQs" \
  --bet-sizes "33,67,100" \
  --raise-sizes "50,100" \
  --allin-threshold 0.67 \
  --accuracy 0.5 \
  --iterations 1000 \
  --mccfr \
  --threads 8 \
  --output strategy.json

# 带权重范围
poker_solver \
  --board AhKd5cTs2s \
  --pot 100 --stack 200 \
  --oop-range "AA:0.75,KK,QQ:0.5,AKs:0.25" \
  --ip-range "TT+,AQs+,KQs" \
  --iterations 500
```

### 交互式模式

```
$ poker_solver --interactive

solver> board AhKd5cTs2s
solver> pot 100
solver> stack 200
solver> oop_range AA,KK,QQ,AKs,AKo
solver> ip_range TT+,AQs+,KQs
solver> set_bet_sizes oop,flop,bet,33,67
solver> set_bet_sizes oop,turn,donk,50
solver> accuracy 0.5
solver> solve
```

---

## 📋 命令行参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--board <cards>` | 公共牌面 (3~5张) | *必填* |
| `--pot <amount>` | 初始底池大小 | 10 |
| `--stack <amount>` | 有效筹码深度 | 100 |
| `--oop-range <range>` | OOP 范围 | `random` |
| `--ip-range <range>` | IP 范围 | `random` |
| `--iterations <n>` | 迭代次数上限 | 200 |
| `--threads <n>` | 线程数 | 1 |
| `--mccfr` | 开启 MCCFR (外部采样) 取代全树展开，大幅加速深层博弈 | - |
| `--bet-sizes <pcts>` | 下注尺寸 (底池%) | `33,67,100` |
| `--raise-sizes <pcts>` | 加注尺寸 (底池%) | `50,100` |
| `--allin-threshold <f>` | All-in 阈值 | `0.67` |
| `--accuracy <pct>` | 目标可利用度 % | `0.5` |
| `--output <file>` | 输出 JSON 路径 | *stdout* |
| `--interactive` | 交互式模式 | - |

---

## 🃏 范围语法

| 语法 | 含义 | 组合数 |
|------|------|--------|
| `AA` | 口袋 A | 6 |
| `AKs` | AK 同花 | 4 |
| `AKo` | AK 不同花 | 12 |
| `AK` | AK 全部 | 16 |
| `TT+` | TT 及以上对子 | 30 |
| `ATs+` | AT同花及以上 | 16 |
| `22-55` | 对子范围 | 24 |
| `AhKh` | 指定具体组合 | 1 |
| `AA:0.75` | 带权重 (75%频率) | 6 |
| `random` | 全部手牌 | C(47,2) |

支持逗号分隔：`AA:0.75,KK,QQ:0.5,AKs,AQs+,JTs`

---

## 🎯 高级功能

### OOP/IP 独立下注配置

```
set_bet_sizes <player>,<street>,<type>,<sizes>
```

| 参数 | 可选值 |
|------|--------|
| `player` | `oop`, `ip` |
| `street` | `flop`, `turn`, `river` |
| `type` | `bet`, `raise`, `donk` |

```bash
set_bet_sizes oop,flop,bet,33,67      # OOP flop 下注 33%, 67%
set_bet_sizes ip,flop,bet,50,100      # IP flop 下注 50%, 100%
set_bet_sizes oop,turn,donk,50        # OOP turn donk 50%
set_bet_sizes ip,river,raise,60,100   # IP river 加注 60%, 100%
```

### Donk Bet

当 OOP 跟注 IP 的下注/加注后进入新一条街，OOP 开注使用 `donk_sizes`。未配置时自动退回使用 `bet_sizes`。

### All-in 阈值

下注后剩余筹码低于 `阈值 × 新底池` 时自动合并为 all-in：

```bash
--allin-threshold 0.67    # 默认
--allin-threshold 0       # 禁用
```

### 收敛精度停止

可利用度降到目标以下时自动停止：

```bash
--accuracy 0.5    # 默认，0.5% pot
--accuracy 0.1    # 更精确
--accuracy 0      # 禁用
```

---

## 🏗️ 项目结构

```
PokerSolver/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── cards.h            # 牌面表示、位掩码、花色同构
│   ├── hand_eval.h        # 手牌评估器 (素数积哈希)
│   ├── game_tree.h        # 博弈树 (OOP/IP 配置 + donk)
│   ├── cfr_engine.h       # DCFR 引擎 (O(M+N) 求值 + 并行)
│   ├── range_parser.h     # 范围解析 (带权重)
│   └── cli.h              # 命令行界面
└── src/
    ├── main.cpp
    ├── cards.cpp           # 花色同构规范化
    ├── hand_eval.cpp       # 查表评估
    ├── game_tree.cpp       # 博弈树构建
    ├── cfr_engine.cpp      # DCFR 核心 + 并行 + 缓存
    ├── range_parser.cpp    # 范围语法解析
    └── cli.cpp             # CLI 实现
```

---

## 🧠 算法原理

### CFR (Counterfactual Regret Minimization)

1. **遍历博弈树**，对每个信息集计算策略
2. **Regret Matching** — 策略与正遗憾值成比例
3. **反事实遗憾更新** — 遗憾 = 动作价值 - 节点价值
4. **收敛定理** — 平均策略可利用度以 O(1/√T) 收敛

### MCCFR (Monte Carlo CFR) 外部采样

在面对庞大的 Flop / Pre-flop 子树时（例如 Flop 需要遍历未来 47 张 Turn 牌及 46 张 River 牌），引擎通过 `--mccfr` 标志切换到外部采样模式：**每次迭代只随机抽取一张实际公共牌，极大避免组合爆炸。**
速度可达到单次几十毫秒内，在数秒钟逼近纳什均衡。对于较浅的深度（Turn/River），则自动使用精确的全树展开保证数学极限无亏损。

### DCFR 参数

| 参数 | 值 | 作用 |
|------|-----|------|
| α (正遗憾折扣) | 1.5 | 正遗憾乘以 `t^α / (t^α + 1)` |
| β (负遗憾折扣) | 0.0 | 负遗憾清零 |
| γ (策略和折扣) | 2.0 | 策略和乘以 `(t/(t+1))^γ` |

### 性能优化架构

```
┌─────────────────────────────────────────────────────────────────┐
│                     性能优化层次                                  │
├─────────────────────────────────────────────────────────────────┤
│ 采样层  │ MCCFR 外部抽取采样，Flop 下发牌分支由 O(1000) 变 O(1)    │
│ 求值层  │ O(M+N) 排序扫描 showdown + O(M+N) 牌面排除 fold        │
│ 缓存层  │ RiverSortedRange 按 board 缓存 + Hand mask 构造缓存     │
│ 遍历层  │ 无拷贝 reach 传递 + flat 策略数组 + 合并更新循环         │
│ 同构层  │ 花色同构 → 合并等价信息集，最高 71% 降维                 │
│ 并行层  │ Chance 节点级多线程 + 无锁 Thread-Local 累加器         │
│ 内存层  │ Chance 节点预分配缓冲区，零 heap 分配                  │
│ 算法层  │ DCFR 单遍折扣 + 自适应 exploitability 频率             │
│ 评估层  │ 素数积哈希 + flush/straight 查表 → O(1) 手牌评估       │
│ 存储层  │ flat array 布局 → CPU cache 友好                       │
└─────────────────────────────────────────────────────────────────┘
```

---

## 📤 输出格式

### 策略摘要

```
=== Strategy Summary (Root Node) ===
Player: OOP
Actions: CHECK | BET 33 | BET 67 | ALLIN

Hand    CHECK     BET 33    BET 67    ALLIN
----------------------------------------------
KcKh    0.0%      0.0%      0.0%      100.0%
AcAd    0.0%      0.0%      0.0%      100.0%
QdQs    73.9%     0.0%      0.0%      26.1%
```

### JSON 输出

```json
{
  "solver": "PokerSolver DCFR",
  "iterations": 200,
  "pot": 100,
  "board": "AhKd5cTs2s",
  "root_strategy": {
    "player": 0,
    "actions": ["CHECK", "BET 33", "BET 67", "ALLIN"],
    "hands": {
      "KcKh": [0.0000, 0.0000, 0.0000, 1.0000],
      "AcAd": [0.0000, 0.0000, 0.0000, 1.0000]
    }
  }
}
```

---

## 📚 交互式命令参考

| 命令 | 说明 | 示例 |
|------|------|------|
| `board <cards>` | 设置公共牌 | `board AhKdQc` |
| `pot <n>` | 设置底池 | `pot 100` |
| `stack <n>` | 设置筹码 | `stack 200` |
| `oop_range <r>` | OOP 范围 | `oop_range AA,KK,AKs` |
| `ip_range <r>` | IP 范围 | `ip_range TT+,AQs+` |
| `set_bet_sizes <...>` | 分玩家下注配置 | `set_bet_sizes oop,flop,bet,33,67` |
| `allin_threshold <f>` | All-in 阈值 | `allin_threshold 0.67` |
| `accuracy <pct>` | 收敛精度 | `accuracy 0.5` |
| `iterations <n>` | 迭代次数 | `iterations 500` |
| `threads <n>` | 线程数 | `threads 4` |
| `solve` | 开始求解 | |
| `export <file>` | 导出 JSON | `export result.json` |
| `help` | 帮助 | |
| `quit` | 退出 | |

---

## 🗺️ 路线图

### ✅ 核心功能

- [x] DCFR 核心算法 + Best Response 可利用度计算
- [x] 素数积哈希手牌评估 (O(1) 查表)
- [x] Flop / Turn / River 全街支持
- [x] 标准范围语法解析 (带权重 `AA:0.75`)
- [x] OOP/IP 分玩家独立下注配置 + Donk Bet
- [x] All-in 阈值 + 收敛精度停止
- [x] 多线程并行 (Chance 节点级 + Thread-Local 累加器)
- [x] 花色同构 (最高 71% 降维)
- [x] JSON 策略导出 + 交互式 REPL

### ✅ 性能及模型优化 (累计提速极大)

- [x] O(M+N) 排序扫描 Showdown (修复反转漏洞)
- [x] MCCFR (Monte Carlo CFR) External Sampling 支持
- [x] O(M+N) 牌面排除 Fold (card_sum[52] 快速求值)
- [x] Chance 节点零 heap 分配 (预分配缓冲区复用)
- [x] 无拷贝 reach 传递 (未修改侧直接传引用)
- [x] Flat 策略数组 + 合并 regret/strategy 更新循环
- [x] Hand mask 构造时缓存
- [x] DCFR 单遍折扣 + 自适应 exploitability 频率

### 🔲 计划中

- [ ] CFR+ 算法变体
- [ ] JSON 博弈树导入/导出
- [ ] Chance 节点花色同构 (减少 deal 遍历次数)
- [ ] 短牌 (Short Deck / 6+) 支持
- [ ] Python 绑定 (pybind11)
- [ ] GUI 可视化界面

---

## 📄 License

MIT License. 详见 [LICENSE](LICENSE) 文件。
