# Thunder × Clausewitz / jomini 对标评估报告

**评估日期**：2026-09-05
**评估对象**：Thunder 引擎（C++23，纯引擎库，~55k 行）`D:\Thunder`
**对标基准**：Paradox Clausewitz 引擎 + `rakaly/jomini` 中间层
**评估目标**：判断当前架构距离"顶尖商用级大战略引擎"的差距，并给出可执行改进路径
**取证方式**：全量源码只读审计 + 文档交叉验证，所有结论附 `文件:行号` 证据

---

## 0. 评估框架

### 0.1 先厘清对标物：Clausewitz 与 jomini 到底各管什么

这是本次评估最关键的一件事。**Clausewitz 和 jomini 不是同一个层次的东西，混为一谈会导致改进方向错误。**

| 层 | Paradox 的实现 | 本质职责 | Thunder 的对应物 |
|---|---|---|---|
| **格式层** | `jomini`（Rust，开源）+ 引擎内二进制 codec | 解析 Paradox 文本格式：对象 / 数组 / 混合容器 / 运算符 / 引号文本 / 日期 / 隐藏键；零拷贝；同时吃文本与二进制存档 | `scripting/ThunderScriptParser.*` |
| **语义层** | Clausewitz 内 C++ 注册表 | 把格式解析出的 key 绑定到几千个 trigger/effect/value 原语、scope 类型与游戏状态 | `scripting/ScriptRegistry.*` + `ScriptCompiler*` + `ScriptVm*` |
| **状态层** | Clausewitz 内 C++ 对象图 | 可变游戏状态、Modifier 桶、日期推进、存档 | `simulation/**` + `runtime/save/**` |
| **表现层** | Clausewitz UI + `.gui` 脚本 | 声明式界面、地图渲染 | `presentation/**` |

**jomini 的核心设计信条**（这是评估 Thunder 的真正标尺）：

1. **格式是自描述且无损的**——同样一段文本能表示对象、数组、标量、以及三者的混装（`mixed container`）。
2. **零拷贝 / 零分配解析**——解析结果借用 mmap 的 `&[u8]`，不构造中间树。
3. **宽容（lenient）**——不认识的键照样解析出来，交给上层决定。这是 mod 生态能活的前提。
4. **文本与二进制同构**——存档有明文和二进制两种形态，格式层统一处理。
5. **不绑定游戏语义**——jomini 不知道"国家"是什么。

Thunder 把格式层和语义层合并成了一个强约束 DSL，并选择了与之相反的三条信条（强类型预解析、非零拷贝、严格拒绝未知字段）。**这不是缺陷，但它决定了 Thunder 与 Clausewitz 生态不兼容，且这个代价必须被清楚认识。**

### 0.2 评分刻度

| 等级 | 含义 |
|---|---|
| **领先** | 商用基线之上，是被对标方应该借鉴的方向 |
| **对齐** | 达到商用可发布水平，机制完备 |
| **部分对齐** | 机制存在但覆盖/深度/可靠性不足，需补强 |
| **明显落后** | 机制缺失或设计选择导致关键能力不可达 |
| **缺失** | 完全未实现 |

---

## 1. 结论速览

### 1.1 总评

> **Thunder 的"引擎骨架"（分层治理、数据布局、确定性工程、静态验证）已经达到甚至超过商用基线；但它的"内容语言层"（格式、脚本表现力、可脚本化域覆盖面）只有商业产品的入门水平，这是通往顶尖商用引擎的唯一真正瓶颈。**

一句话概括差距性质：**Thunder 造了一台非常好的机器，但给这台机器配的编程语言只有商用产品的一小部分，而且说不了一句 Clausewitz 的话。**

### 1.2 维度评分卡

| # | 维度 | 等级 | 分 | 一句话判断 |
|---|---|---|---|---|
| 1 | 分层与依赖治理 | **领先** | 9 | configure 期就 `FATAL_ERROR` 强制引擎/游戏边界，比 Clausewitz 严格 |
| 2 | 文本格式层（jomini 对等物） | **明显落后** | 3 | 只认 `type name { k = v }`；无数组、无运算符、无日期、无回写 |
| 3 | 脚本语言表现力 | **明显落后** | 3 | 无 `if/else/while/random_list`；值表达式退化为 `v*m+a` |
| 4 | 脚本编译与静态验证 | **领先** | 9 | 全库链接、调用环检测、类型签名校验——jomini 完全没有的东西 |
| 5 | 对象与数据模型 | **对齐** | 7 | SoA + 强类型 ID + 代际池；但 StrongId 未携带 generation |
| 6 | 存储布局与性能工程 | **对齐** | 8 | 热/冷列分离、紧凑化、百万 POP 实证 |
| 7 | Modifier / 派生状态 | **对齐** | 8 | 真正复现了 Clausewitz 的 add/mult/min/max 桶 + 层级继承 |
| 8 | 数值与确定性 | **对齐** | 7 | int64 定点 + keyed RNG + 广覆盖 checksum；**但双轨制（double/int64）是隐患** |
| 9 | 内容装载与 Mod VFS | **对齐** | 8 | overlay + 拓扑排序 + 确定性加载序，商用级 |
| 10 | 存档与版本迁移 | **部分对齐** | 6 | 迁移路径扎实；但纯二进制不可读、无加密签名 |
| 11 | 本地化 | **部分对齐** | 5 | 键值/插值/富文本具备；无复数、无文件约定、CJK 未接通 |
| 12 | Scripted GUI | **领先** | 8 | 强类型编译 + 虚拟化 + mod 栈重建，优于 Clausewitz `.gui` |
| 13 | 地图与渲染 | **部分对齐** | 6 | 路径齐全，**但 Vulkan 后端完全不在 CI 门禁内** |
| 14 | 模拟域深度与广度 | **部分对齐** | 4 | 数据结构丰富（条约/海军/前线…），但机制与 AI 极薄 |
| 15 | 工具链与编辑器 | **部分对齐** | 5 | 世界/资产工具可用，地图与 GUI 编辑器是桩 |
| 16 | 测试与质量门禁 | **对齐** | 7 | 27 套件 + CI 门禁；但性能/Vulkan 无门禁、本机不可构建 |
| 17 | Mod 生态就绪度 | **部分对齐** | 5 | 数据 mod 就绪，机制 mod 必须改 C++，且无热重载 |

**加权总评：约 6.2 / 10** —— 一个"工程素质优秀、生态能力未成型"的引擎。

### 1.3 致命短板 Top 5（决定能否商用）

| 序 | 短板 | 为什么致命 |
|---|---|---|
| **1** | 脚本无 `if/else`、`while`、`random_list` | Clausewitz 内容中条件分支与加权随机是绝对主力；缺了它，内容作者无法表达"若满足条件则执行"，只能靠 C++ 加原语绕过 |
| **2** | 可脚本化 scope 只有 5 个 | 引擎已实现的军队/海军/条约/前线/利益集团/政党/列强集团**全部无法被脚本访问**——数据存在，modder 摸不到 |
| **3** | 值模型退化为 `source*multiply+add` | Vic3 的 script value 是任意嵌套算术树。当前实现无法表达"`(人口×0.5 + GDP×0.3) / 基准值`"这类几乎所有平衡公式都要用的东西 |
| **4** | 无比较运算符，比较靠"每个比较一个 C++ 原语" | 内置 16 个 trigger 里 12 个是 `*_above`。这是 O(n) 的 C++ 劳动换取 O(1) 的语言能力，规模上不可持续 |
| **5** | 严格拒绝未知字段 | 与 jomini 的"宽容"信条相反。mod 生态依赖"旧 mod 在新版本上仍能跑"；严格模式会让每个引擎版本都破坏全部现存 mod |

---

## 2. 对齐情况分析（做对了什么）

### 2.1 分层与依赖治理 —— 领先

这是 Thunder 最强的一项，且**明显强于 Clausewitz 的实际工程状态**。

```cmake
# CMakeLists.txt — 配置期即可拒绝引擎/游戏耦合
# src/thunder/** 出现 #include "game/..." 直接 FATAL_ERROR
```

六层依赖单向：`foundation → scripting → content → simulation → presentation → runtime`。
更狠的是它把**已废弃路径也用配置期禁令钉死**：`src/thunder` 不得引用 `set_world_map_layers` 或 `world_map*.thunderimg`（全图栅格 atlas 路径已判死）。

**为什么这比 Clausewitz 强**：Clausewitz 是 20 余年演化的单体引擎，其"引擎/游戏"边界靠约定与代码评审维持；Thunder 用构建系统把它变成机器可验证的不变量。**这条要保持，它是 Thunder 最大的结构性资产。**

### 2.2 数据导向存储 —— 对齐

不是"用了 vector"，而是做了**访问热度驱动的列拆分**：

```cpp
// PopStore.hpp:33-63  热数据（每周扫描）与冷数据物理隔离
struct PopHotData { std::vector<MarketId> markets;
  std::vector<PopulationCount> population; ... std::vector<EconomyAmount> cash_milli; };
struct PopColdData { std::vector<ProvinceId> provinces;
  std::vector<CultureId> cultures; ... }; // 社会/文化/政治
```

配合 `SlotPool` 的 LIFO free-list + 64-bit 存活 bitmap + `SoaCompactor` 紧凑化（`PopStore.cpp:145-174`），以及 bench 实证的 **1,000,000 POPs / 80,000 建筑 / 256 市场**（`bench/economy_bench.cpp:73-75`）。这是商用级的。

### 2.3 Modifier 系统 —— 对齐（真正吃透了 Clausewitz）

```cpp
// HierarchicalModifierGraph.hpp:29-35  桶模型
enum class ModifierOp { Add=0, Multiply=1, Min=2, Max=3, BoolFlag=4 };
// :52-60  breakdown 含 flat_add / percent_mult / min_bound / max_bound
struct ModifierBreakdown { double base, flat_add, percent_mult;
  double min_bound=-inf, max_bound=inf; bool bool_flag; };
// :20-27  层级作用域
enum class ModifierScopeLevel { Global, Country, State, Province, Building };
```

Clausewitz 的 modifier 求值语义（先加后乘、再夹 min/max）与作用域继承链（building→province→state→country→global）被准确复现，且用 per-scope revision 做脏传播。**这一项可以说已经毕业。**

### 2.4 确定性工程 —— 对齐

```cpp
// DeterministicRng.hpp:17-31  无状态 keyed 流：同一 (seed, stream_key, counter)
// 不论 worker 数/窃取顺序都产生同值 —— 这是并行确定性的正确解法
static std::uint64_t keyed_u64(base_seed, stream_key, counter=0);
```
```cpp
// Hash.hpp:46-68  归一化 NaN 与负零
if (std::isnan(value)) add(canonical_nan); else if(value==T{0}) value=T{0};
```

无状态 keyed 采样（而非有状态流）是一个**优于许多商用实现的选择**——它让"并行度变化不改变结果"成为结构性保证，而不是靠"小心不要改调度顺序"来维持。checksum 覆盖到 slot 分配器的 generation/bitmap/free-list 状态（`World.cpp:12-46` + `add_slot_allocator_state`），覆盖面比典型商业实现更严。

### 2.5 脚本静态验证 —— 领先（jomini 之上的能力）

这是 Thunder **超越**对标物的地方，值得重点强调：

```
// docs/THUNDER_SCRIPT.md:93-101  全库链接器报告
- unknown scripted trigger/effect/value targets;
- caller/callee root-scope mismatch;
- missing, extra or duplicate named arguments;
- statically provable argument-kind and scope-subtype mismatch;
- references to undeclared typed parameters;
- direct or indirect script and scripted-value call cycles.
```

jomini 只做格式解析，语义正确性完全推迟到运行期——这也是为什么 Paradox 的 mod 报错常常是"进入游戏后某年某月崩溃"。Thunder 在装载期就静态证明调用图无环、类型匹配、作用域合法。**这是真正的差异化优势，应该成为对外宣传与生态建设的核心卖点。**

### 2.6 Mod VFS 与确定性加载序 —— 对齐

```cpp
// ModManifest.cpp:884-897  无依赖时按 load_priority 再按 canonical ID 排序
// 注册顺序不作为 tie-breaker —— 这是确定性的关键细节
```
`build_mod_load_plan` 用 Kahn 拓扑排序处理 `load_before`/`load_after`/`required`，VFS 按 logical_path 遮蔽实现单文件与目录级覆盖。商用级完备。

### 2.7 Scripted GUI —— 对齐偏领先

15 种控件、11 种强类型绑定值、虚拟列表、编译为紧凑数组（运行期零字符串查找）、mod 栈可重建、自带 checksum 用于内容握手。相比 Clausewitz `.gui` 的"运行期按名解析、无类型 schema"，Thunder 的方案更适合大规模 mod 生态。

---

## 3. 关键差异点（架构层面）

### 3.1 差异一：格式层是"强约束 DSL"，不是"自描述通用格式"

**这是所有兼容性问题的根。**

jomini 能解析的全部形态 vs Thunder 的实际支持：

| 形态 | 示例 | jomini | Thunder | 证据 |
|---|---|---|---|---|
| 对象 | `a = { b = 1 }` | ✅ | ✅ | `ThunderScriptParser.cpp:221-224` |
| 数组 | `a = { 1 2 3 }` | ✅ | ❌ 报 "expected property name" | `:199-204` |
| 混合容器 | `a = { 1 2 b = 3 }` | ✅ | ❌ | 同上 |
| 顶层裸赋值 | `NGAME = 5` | ✅ | ❌ 要求 `type name {` | `:144-159` |
| 比较运算符 | `a > 5` / `a <= 3` | ✅ | ❌ 词法层无 `<` `>` `!` | `:17, :36-46` |
| 日期字面量 | `1444.11.11` | ✅ | ⚠️ 退化为符号，无日期语义 | `:95-98` |
| 引号文本 | `"text"` | ✅ 区分引用/未引用 | ⚠️ 二者坍缩为同一 SymbolId | `:229-232` |
| 文本回写 | 写出可读文本 | ✅ | ❌ 无任何序列化器 | 全仓无 `ScriptWriter` |
| 零拷贝 | 借用 mmap 缓冲 | ✅ | ❌ 全量 `std::string` 拷贝 | 无 mmap 调用 |
| 未知键宽容 | 不认识的键照常产出 | ✅ | ❌ 硬错误 | `DefinitionDatabase.cpp:399-405` |

**影响判断**：这不是"少几个语法糖"。它意味着——

1. **Thunder 无法直接消费任何现有 Clausewitz 内容**（even 格式转换都做不了，因为语义层也完全不同）。生态必须从零建。
2. **格式不可回写** → 无法做"引擎改写 mod 文件"、无法做编辑器保存、无法做内容迁移工具。
3. **严格拒绝未知字段** → 引擎每次升级都会让现有 mod 全部失效。这与 jomini 的信条直接对立。

**但要注意**：这也不完全是坏事。Thunder 的强约束换来了静态可验证性（§2.5）。**正确的策略不是"改成 jomini"，而是在保持静态验证的前提下把格式层补齐**（见 §6 P0-1）。

### 3.2 差异二：数值双轨制

```
经济主链路：int64 定点，economy_scale = 1000（milli），带饱和乘除
  EconomicTypes.hpp:14-19   mul_div_nonnegative / saturating_add
脚本与 Modifier：double
  ScriptValue.hpp:29        double number = 0.0;
  ModifierGraph.hpp:24      double value = 0.0;
  CountryStore.hpp:126-133  同一 Store 内 double population_/treasury_/prestige_ 与 int64 treasury_milli_ 并存
```

`treasury_` 与 `treasury_milli_` 在同一个 Store 里并存，是最刺眼的一处——它就是双轨制的化石。

**风险**：IEEE-754 双精度本身是确定的，所以这不必然破坏确定性；但它是**语义**风险——定点链路上的饱和语义（溢出截断）与 double 链路上的 Inf/NaN 语义不同，跨边界时会静默改变结果。商用引擎里这类 bug 极难定位。

### 3.3 差异三：有数据，但不可脚本化

`GrandStrategyStore` 已经实现了相当丰富的领域记录：

```cpp
// GrandStrategyStore.hpp  —— 条约/文章/参与方、陆军、海军、海战、海区、
// 前线、会战、殖民地、舰船设计、投资池、利益集团、政党、强权集团、
// 外交博弈、迁徙流、政府、立法进程、外交关系
```

但：

```cpp
// Scope.hpp:7  —— 可脚本化作用域只有 5 个
enum class ScopeType : std::uint8_t { None, Country, State, Province, Pop, Market };
```

**上面那一整片领域状态，脚本一个都碰不到。** 连带后果是内置原语也只覆盖这 5 个域：

```
内置 trigger（16）：population_above gdp_above treasury_above tax_rate_above
  national_debt_above bank_reserves_above bank_lending_capacity_above
  state_population_above province_population_above pop_size_above pop_sol_above
  pop_literacy_above market_supply_above market_demand_above
  government_legitimacy_above government_stability_above
内置 effect（13）：add_treasury set_tax_rate set_gdp set_import_tariff
  set_export_tariff set_trade_logistics_capacity issue_sovereign_bonds
  repay_sovereign_debt set_state_owner set_province_owner set_pop_wealth
  set_pop_literacy set_market_owner set_primary_currency
```

对照：商用 Clausewitz 游戏的脚本原语与可脚本化字段在 **10³ 量级**。Thunder 目前 **29 个**。

需要公允地说：**Thunder 是纯引擎，不含游戏内容，所以"原语少"部分是设计选择而非缺陷**——原语由外部游戏层通过 `register_trigger/register_effect` 注册，机制是开放的（`ScriptRegistry.hpp:47-54`）。

但这里有一个**验证论真空**：

> 引擎从未用"数千个原语 + 数百种定义"的规模压过自己的设计。`TriggerPrimitiveId` 是 `StrongId<uint16_t>`，上限 65535，理论上够；但**符号表碰撞检测、链接器的 O(n²) 风险、编译期内存占用、装载耗时——这些在 29 个原语下全部不会暴露**。

**这是本次评估中最需要警惕的一点：设计未经过规模验证。** 建议尽快做一个"压力内容包"专项（见 §6 P1-4）。

### 3.4 差异四：AI 与战争机制深度严重不足

| 模块 | 代码量 | 判断 |
|---|---|---|
| `simulation/ai` | **710 行** | 唯一公开入口 `set_country_strategy(CountryId, AiStrategyParameters)`（`StrategicAiPlanner.hpp:37`） |
| `simulation/warfare` | **217 行** | `BattlePhaseSystem`（战术卡 + 每日推进）+ `LogisticsNetwork`（补给枢纽/连接/破交） |

对比参照：Vic3 的 AI 是一个多层系统（战略意图 → 预算分配 → 建造队列 → 外交博弈评估），战争是"前线 + 推进 + 补给 + 指挥官"的完整子系统。Thunder 的战争目前是 `progress_milli` 标量推进。

**判断**：AI 与战争是"**玩家感知最直接**"的两个系统。当前状态属于"数据结构就位、机制未生长"。这不阻塞引擎发布（引擎可不含玩法），但**如果目标是"顶尖商用"，这是必须自建或明确交给游戏层的部分**——而当前引擎里放了半成品，反而会造成归属混乱。

---

## 4. 兼容性差距清单

按"补上后对生态的价值"排序。每项标注工作量量级（人周，粗估，含测试）。

### 4.1 P0 — 语言层（阻塞商用）

| # | Clausewitz/jomini 能力 | Thunder 现状 | 价值 | 量级 |
|---|---|---|---|---|
| P0-1 | **数组与混合容器** `a = { 1 2 3 }` | ❌ 不支持 | 阻塞任何真实内容表达；Clausewitz 里到处是集合字面量 | 2–3 周 |
| P0-2 | **比较运算符** `> < >= <= == !=` | ❌ 词法层没有 | 消除"一个比较一个 C++ 原语"的荒谬现状 | 2 周 |
| P0-3 | **`if` / `else_if` / `else`** | ❌ 完全缺失 | **最高优先级**。条件分支是内容作者的日常语法 | 3 周 |
| P0-4 | **`limit = { ... }` 条件块**（在迭代器内） | ⚠️ `limit` 已被占用为"数量上限"（`ScriptCompilerScoped.cpp:203,454` 仅接受 Number） | **语义冲突**：与 Clausewitz 同名不同义，是迁移陷阱，必须解决 | 1 周（重命名） |
| P0-5 | **嵌套 script value 算术树** | ⚠️ 只有 `source*multiply+add`（`THUNDER_SCRIPT.md:122`） | 平衡公式、加权评分、动态数值全部依赖它 | 4–6 周 |
| P0-6 | **`random_list` / `weight`** | ❌ 缺失（文档列为 outstanding） | 事件选项、AI 加权、随机内容的核心 | 2 周 |
| P0-7 | **Date / Duration 类型** | ❌ `ScriptArgumentKind` 无日期 | 历史条目、到期、冷却、时序事件全部依赖 | 2 周 |
| P0-8 | **扩展 scope 注册**：Character / Building / Army / Navy / War / Front / Treaty / InterestGroup / Institution / DiplomaticPlay | ⚠️ `ScopeType` 仅 5 项 | 解锁 §3.3 那一整片已实现但不可达的领域 | 6–10 周（分域推进） |

### 4.2 P1 — 生态与内容生产

| # | 能力 | 现状 | 说明 | 量级 |
|---|---|---|---|---|
| P1-1 | **宽容解析**（未知字段告警而非报错） | ❌ 严格拒绝（`DefinitionDatabase.cpp:399`） | **mod 兼容性的命门**。改成分级诊断：unknown → warning，malformed → error | 2 周 |
| P1-2 | **事件系统完整化**（event target、option、权重、冷却、触发时机、`hidden_effect`） | 部分（`ScriptedGameplay` 有 Event/Decision/Journal 定义，无权重/冷却） | | 4 周 |
| P1-3 | **on_action 总线** | 部分（调度调用存在，无跨系统事件订阅表） | Vic3 的整个玩法织体靠 on_action 缝合 | 3 周 |
| P1-4 | **规模压力验证**（合成数千原语/定义的内容包） | ❌ 从未做过 | 见 §3.3 验证论真空 | 2 周 |
| P1-5 | **热重载 / 增量构建** | ❌ 未实现（`MOD_RUNTIME.md:160-166`） | 内容迭代速度的 10 倍差距 | 4 周 |
| P1-6 | **独立内容 lint CLI** | ❌ 校验内嵌于装载门禁 | modder 需要离线自检工具 | 1 周 |
| P1-7 | **脚本文本序列化器**（回写） | ❌ 完全没有 | 阻塞编辑器保存、内容迁移、格式化工具 | 3 周 |
| P1-8 | **本地化复数规则 + `l_<lang>` 文件约定** | ❌ 无复数、无约定 | 多语言可用但非商用级 | 2 周 |

### 4.3 P2 — 工程与质量门禁

| # | 问题 | 证据 | 建议 | 量级 |
|---|---|---|---|---|
| P2-1 | **Vulkan 后端完全不在 CI 门禁内** | `THUNDER_BUILD_VULKAN` 默认 OFF，CI 仅 `release-headless` | 这是**最大的质量盲区**：20k 行渲染代码零合并验证 | 1 周（加一个 CI job） |
| P2-2 | **性能无回归门禁** | bench 只 `add_executable` 未 `add_test`，且无阈值断言（`CMakeLists.txt:342-352`） | 加阈值断言 + 接入 ctest | 1 周 |
| P2-3 | **本机 cmake 不可用**（退出 127） | `/d/mingw64/bin/cmake` 无法 exec | 本机无法构建/测试，验证只能靠 `g++ -fsyntax-only`。**这已经严重影响迭代速度** | 立即修复 |
| P2-4 | **`GameClock` 未纳入 `World::checksum`** | `World.hpp` 无 clock 成员 | 时序偏移不会被 desync 检测捕获 | 0.5 周 |
| P2-5 | **StrongId 未携带 generation** | `PopStore.cpp:113` `return PopId{raw};` 丢弃 generation | ABA 风险：旧 ID 静默指向新实体 | 2 周 |
| P2-6 | **并行度名不副实** | 9 个 TickTask 仅 2 个 `ParallelSafe`，依赖链导致波次实际串行（`ThunderEngine.cpp:74-104`） | 拆依赖，把 economy 内部并行提到调度层 | 3 周 |
| P2-7 | **测试框架不统一** | 6/27 套件用 `main()` 内联 `assert()` 而非 `test_*()` | 失败定位弱 | 1 周 |
| P2-8 | **地图/GUI 编辑器是桩** | `thunder_map_editor` 仅 `--info`/`--paint-test` | 要么补全，要么明确移出仓库避免误导 | 决策项 |

---

## 5. 风险登记

| 风险 | 等级 | 说明 |
|---|---|---|
| **设计规模未验证** | 🔴 高 | 29 个原语下，链接器复杂度、符号碰撞、装载耗时、编译内存均未受压。商用规模是 100 倍 |
| **渲染路径零合并门禁** | 🔴 高 | 20,363 行 presentation 代码（占全库 37%）在 CI 中根本不编译 |
| **内容语言不可达已实现领域** | 🔴 高 | 条约/海军/前线/利益集团等数据全在，脚本全摸不到——等于白写 |
| **双轨数值语义** | 🟡 中 | 定点饱和语义 vs IEEE Inf/NaN 语义，跨边界静默分歧 |
| **ABA 悬垂引用** | 🟡 中 | 代际机制存在但未接线到对外 ID |
| **`limit` 语义冲突** | 🟡 中 | 与 Clausewitz 同名不同义，将来做兼容层时是地雷 |
| **严格解析的生态代价** | 🟡 中 | 每次引擎升级破坏全部现存 mod；与 jomini 信条相反 |
| **本机工具链断裂** | 🟡 中 | 无法本地构建，只能做语法检查，迭代与验证成本极高 |
| **AI/战争半成品归属不清** | 🟢 低 | 引擎内含薄实现，容易让使用方误判能力边界 |

---

## 6. 改进建议与路线图

### 6.0 战略原则：对齐什么，拒绝什么

**必须对齐的**（不补齐就无法商用）：
- 内容语言的表达力（分支、循环、加权、算术、比较）
- 可脚本化域的覆盖面
- mod 兼容的宽容性
- 渲染路径的验证门禁

**应当拒绝对齐的**（Clausewitz 的弱点，Thunder 已做得更好，不要倒退）：
- ❌ **不要放弃静态链接验证去换取"宽容"**。正确做法是分级诊断：格式宽容 + 语义严格。
- ❌ **不要为了兼容 Clausewitz 语法而放弃强类型 ID / SoA**。那是 Clausewitz 的历史包袱。
- ❌ **不要做"明文存档 + 无校验"**。Thunder 的二进制 + checksum + 迁移路径优于明文；要补的是"可调试性"（导出工具），不是改成明文。
- ❌ **不要照搬 Clausewitz 的"运行期才报错"**。Thunder 的编译期拒绝是优势。

**Thunder 应该押注的差异化定位**：

> **"可静态验证的大战略内容平台"** —— 在保证 Clausewitz 级表达力的同时，提供 modder 在装载期就能拿到完整类型错误、调用环、作用域不匹配的诊断，而不是进游戏三小时后才崩。
>
> 这是 Paradox 生态最大的痛点之一，也是 Thunder 唯一有可能做"不同"而非"更好"的地方。

### 6.1 阶段一：语言可达性（约 12–16 周）—— 最高优先级

目标：让内容作者能表达 Clausewitz 能表达的绝大多数东西。

1. **格式层扩展**（P0-1, P0-2, P0-7）
   - `ScriptValueKind` 增加 `Array` / `Text` / `Date` 分支
   - 词法层增加 `> < >= <= == !=` 与 `!`
   - `ScriptNode` 支持无键值（数组元素）
   - 日期解析为结构化 `GameDate`，保留原始文本用于诊断
   - **保持**：解析仍产出 AST，仍可静态验证——这是与 jomini 的路线分歧，是有意的
2. **控制流**（P0-3, P0-4, P0-6）
   - `ScopedEffectKind` 增加 `If` / `ElseIf` / `Else` / `While` / `RandomList` / `WeightedList`
   - **重命名** 迭代器数量上限 `limit` → `max_count`，把 `limit = { ... }` 留给条件块
   - `random_list` 必须以 scripted value 作为权重，复用 `DeterministicRng::keyed_u64` 的 callsite salt 机制
3. **值系统**（P0-5）
   - 把 `source*multiply+add` 换成真正的算术表达式 IR：`add / subtract / multiply / divide / min / max / round / clamp` + 嵌套子值 + 作用域取值
   - 与 ModifierGraph 对接（这是"modifier 也能写公式"的前提）

**验收指标**：能用 ThunderScript 重写以下任意一段真实 Vic3 内容且语义等价：
`common/scripted_effects/` 中一个含 `if/limit/random_list` 的效果；`common/script_values/` 中一个三层嵌套的值。

### 6.2 阶段二：域覆盖与生态（约 10–14 周）

4. **scope 扩展**（P0-8）：按优先级 `Building → Character → Army/Navy → War/Front → Treaty → InterestGroup/Institution`。
   每一域配 `ScopeResolver` 导航与一组原语。**这一步做完，§3.3 里"不可达的数据"才真正变成资产。**
5. **宽容解析**（P1-1）：诊断分级 `Error / Warning / Info`；引擎默认 `Warning`；提供 `--strict` 与 CI 门禁使用 `Error`。
6. **事件与 on_action 完整化**（P1-2, P1-3）。
7. **规模压力验证**（P1-4）：写一个生成器，合成 ≥5,000 原语 / ≥50,000 定义的内容包，测量装载耗时、峰值内存、链接耗时，并设预算。

### 6.3 阶段三：工程质量与生态工具（约 8 周）

8. **CI 补 gate**（P2-1, P2-2）：新增 `ci-vulkan` job（编译 + 冒烟）；bench 接入 ctest 并设阈值。
9. **修工具链**（P2-3）：**立即**修复本机 cmake——这是当前迭代效率的最大拖累。
10. **正确性修补**（P2-4, P2-5, P2-6）：clock 进 checksum；StrongId 携带 generation；拆调度依赖链。
11. **编辑器决策**（P2-8）：要么补全 `thunder_map_editor` / `thunder_gui_editor`，要么明确标注为 preview 并移出默认构建。
12. **序列化器**（P1-7）+ **lint CLI**（P1-6）+ **热重载**（P1-5）。

### 6.4 阶段四：差异化能力（持续）

13. 把静态验证能力产品化：导出 mod 诊断报告（HTML/JSON）、编辑器实时诊断、内容 diff。
14. 把"确定性"产品化：desync 定位工具（checksum 二分 + 作用域快照 diff）。这是多人大战略的刚需，Paradox 至今做得一般。
15. 建立"参考内容包"——不是游戏，而是一套**用来证明引擎能力边界**的最小可玩经济 + 事件 + UI 样例。当前引擎零内容，外部使用方无法评估能做什么。

---

## 7. 最终判断

**Thunder 目前是什么**：一个工程素质优秀、架构治理严格、确定性设计正确、但**内容语言层尚未展开**的大战略引擎内核。它的"机器"部分（存储、调度、Modifier、确定性、存档、UI 框架）已经可以对标商用；它的"语言"部分（格式、脚本、可脚本化域）还停留在原型阶段。

**距离"顶尖商用级"还有多远**：

| 维度 | 差距 |
|---|---|
| 引擎内核 | **1–2 个数量级的工作已在身后**。补 §6.3 的工程质量项即可达到商用可发布 |
| 内容语言 | **约 6 个月集中投入**（§6.1 + §6.2），前提是有专职人力且不再扩张新领域 |
| 生态与内容 | **12 个月以上**，且取决于是否有参考内容包与第一批外部 modder |
| 玩法深度（AI/战争） | **未知**——当前是空白而非落后，需要先做归属决策（引擎层 or 游戏层） |

**如果只做一件事**：补 `if/else` + 值算术树 + 比较运算符（P0-2/3/5）。这三件事做完，Thunder 的内容表达能力会从"原型"跳到"可用"，且它们会连锁解锁其余 P0 项的实际价值。

**如果只避免一件事**：不要在补齐表达力的过程中丢掉静态验证。那是 Thunder 唯一真正领先对标物的东西。

---

## 附录 A：证据索引

| 结论 | 证据 |
|---|---|
| 格式层只支持 `type name { k = v }` | `scripting/ThunderScriptParser.cpp:137-165, 191-243` |
| 词法层无比较运算符 | `ThunderScriptParser.cpp:17, 36-46` |
| 值种类只有 Number/Symbol/Block | `scripting/ThunderScriptParser.hpp:11` |
| 脚本值种类无 Date/Array | `scripting/ScriptValue.hpp:22` |
| ScopeType 仅 5 项 | `scripting/Scope.hpp:7` |
| 无 if/else/while/random_list | 全仓 grep `"if"\|"else"\|"while"\|"random_list"` 零命中（效果关键字） |
| `limit` 被占用为数量上限 | `ScriptCompilerScoped.cpp:203, 454` |
| 值表达式退化为 `source*multiply+add` | `docs/THUNDER_SCRIPT.md:122` |
| 内置原语 29 个 | `scripting/ScriptRegistry.cpp:233-341`（16 trigger + 13 effect） |
| 原语 ID 为 uint16 | `scripting/ScriptRegistry.hpp:19-20` |
| Modifier 桶模型 | `simulation/kernel/HierarchicalModifierGraph.hpp:29-35, 52-60` |
| 数值双轨（int64 定点 vs double） | `economy/EconomicTypes.hpp:14-19` vs `CountryStore.hpp:126-133`, `ModifierGraph.hpp:24` |
| StrongId 未携带 generation | `economy/PopStore.cpp:113, 176-185` |
| GameClock 不在 World::checksum | `simulation/kernel/World.hpp:20-33` |
| keyed RNG | `foundation/base/DeterministicRng.hpp:17-31` |
| NaN/负零归一化 | `foundation/base/Hash.hpp:46-68` |
| 严格拒绝未知字段 | `content/definition/DefinitionDatabase.cpp:399-405` |
| 非零拷贝装载 | `content/definition/VirtualFileSystem.cpp:89-99` |
| 确定性 mod 加载序 | `content/definition/ModManifest.cpp:884-897` |
| 二进制存档 + 迁移路径 | `runtime/save/SaveGame.cpp:458, 728-773` |
| 无加密签名 | `docs/MOD_RUNTIME.md:81` |
| 调度并行度低 | `runtime/engine/ThunderEngine.cpp:74-104` |
| Vulkan 不在 CI | `CMakeLists.txt:21, 236`；`CMakePresets.json:18-26` |
| bench 无门禁 | `CMakeLists.txt:342-352` |
| 27 测试套件 / 217 用例 | `CMakeLists.txt:314-340` |
| 无脚本文本序列化器 | 全仓 grep 无 `ScriptWriter` / `to_text` |
| 无 mmap | 全仓 grep `mmap`/`MapViewOfFile` 零命中 |
| AI 单入口 | `simulation/ai/StrategicAiPlanner.hpp:37` |
| 战争模块 217 行 | `simulation/warfare/`（BattlePhaseSystem + LogisticsNetwork） |
| 代码规模分布 | foundation 1.5k / scripting 5.9k / content 5.7k / simulation 16.6k / presentation 20.4k / runtime 5.0k |

## 附录 B：评估边界声明

- 本次评估基于**源码与文档的静态审计**，未构建或运行引擎（本机 cmake 不可用，见 P2-3）。涉及运行时性能的判断引用仓库自带 bench 数据，未独立复现。
- 关于 Clausewitz / jomini 的描述基于其**公开**实现与文档（jomini 为开源项目）。Paradox 私有引擎内部细节不在可核实范围内，涉及处均以公开行为推断。
- "10³ 量级原语"为对商用 Clausewitz 游戏公开 modding 文档规模的量级估计，非精确统计。
- 工作量估算为粗量级，用于排序参考，不构成排期承诺。
