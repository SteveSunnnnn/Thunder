# 规格 07 · 规模压力验证与差异化能力（P1-4 · P4）

> 锚点基线：2026-09-05。生成器：`../reference-content/scripts/generate_scale_pack.py`。

## 1. 验证论真空（P1-4）

审计核心警示：引擎在 29 个内置原语下从未受压；商用规模是 100 倍。符号表碰撞、
链接器复杂度、装载耗时、编译内存等**只在规模下暴露**的问题无从发现。

### 1.1 合成压力内容包生成器

`generate_scale_pack.py`（已随本包交付）能力：

```
python reference-content/scripts/generate_scale_pack.py \
    --out /tmp/scale_pack --primitives 5000 --definitions 50000 \
    --buildings 12000 --goods 256 --pops 8 --events 3000 \
    --seed 20260905
产出：/tmp/scale_pack/{countries,definitions,events,on_actions,localization}/…
      + manifest.thundermod + report.json（规模统计）
```

- 确定性：同一 seed 产出逐字节一致（随机用内置线性同余/固定表，不依赖系统熵）。
- 语义真实：不是随机垃圾——每类定义按真实形态生成（building 引用 goods、
  production_method 引用 recipe、event 引用 on_action、scripted_value 互相嵌套
  引用并构造调用环边界），确保链接器工作量真实。
- 边界用例：随机注入已知坏内容（漏参数/环引用/作用域错配）到 `faults/` 子集，
  供 lint/装载诊断做"错误路径吞吐"测试。

### 1.2 预算与门禁

| 指标 | 预算（dev-headless，参考机器，p95） | 说明 |
|---|---|---|
| 全量装载 | ≤ 4 s | 解析+编译+链接（5000 原语/50000 定义） |
| 链接诊断 | ≤ 1 s | 全库 validate_links |
| 峰值内存 | ≤ 1.5 GB | 装载阶段 |
| AST 节点 | 现 1e6 上限须可配置化 | `ThunderScriptParser.cpp:266` `max_ast_nodes` 在压力包下会撞限 → 改为按文件预算 + 总量预算双限 |

压力包注册为 ctest 可选套件 `thunder_scale_tests`（需显式
`--scale-pack <dir>`），CI 每日任务跑，不阻塞普通 PR；装载耗时/内存写入
`budgets.json`（规格 06 §2 通道）。

### 1.3 符号表碰撞风险量化

5000 原语 + 50000 定义 + 事件/本地化 → 估计 > 300k 个不同符号。现
`script_stable_key`（FNV-1a64，`ScriptValue.hpp:16-20`）碰撞概率 64 位下可忽略，
但**必须在压力包中实际校验**：装载时统计符号总数与 FNV 碰撞数（哈希表以 64 位
键做二次确认），跑一次即出数据，杜绝"理论安全"。

## 2. 差异化能力（P4）

### 2.1 静态验证产品化（定位核心）

- `thunder_content_lint --emit-markdown`（规格 05 §2）：给 modder 的可读诊断报告
  （文件:行 + 修复建议 + 调用链展开）。这是 Paradox 生态缺失的体验。
- 编辑器实时诊断：lint 作为 VFS 文件变更的增量后端（与热重载事务共用
  ContentTransaction 通道）。

### 2.2 desync 定位工具（多人大战略刚需）

设计：checksum 二分 + 作用域快照 diff。
1. `engine_checksum()` 已是整局校验（规格 06 §4）。新增**分区校验**：
   `AuthoritativeStoreRegistry` 已按注册序聚合（`AuthoritativeStoreRegistry.hpp:84-98`），
   展开为逐 store checksum 数组，保存每 tick 的每 store 校验和。
2. 对账：两台机器逐 tick 交换 per-store 校验和 → 首分歧 tick + 分歧 store 定位，
   再叠加该 store 的 scope 引用快照 diff（事件上下文 GCT1 区段已可展开）。
3. 工具形态：`thunder_net_audit --replay-a a.save --replay-b b.save`，输出
   "tick 1847，分歧在 CountryStore::treasury_milli_[12]"，替代"从头猜"。

### 2.3 参考内容包

引擎零内容 → 使用方无法评估能力边界。交付一套**最小但真实**的样例（
`reference-content/examples/` 升级为可装载的 `.core` 场景）：
一个 8 国世界：国家/建筑/科技/事件/决议/journal/本地化/on_action 完整闭环，
能跑通 tick、存档、lint 零 Error。不追求可玩性，追求"能力边界证明"——
每个新语法特性都有一条示例在此包内被 CI 编译验证（示例 = 规格夹具 = 回归测试
三合一）。

## 3. AI / 战争归属决策（修正审计遗留）

审计结论：`simulation/ai`（710 行/单入口 `set_country_strategy`）与 `warfare`
（217 行，标量 progress_milli）处于"数据结构在、机制未生长"状态，归属不清。

**决策**：
1. **引擎保留"内核级"AI/战争基础设施**：utility 框架（`UtilityAi`）、确定性
   计划执行、战斗推进/后勤管线作为引擎能力保留——它们是引擎可复用资产。
2. **策略决策内容归游戏层**：国家目标、预算分配、外交博弈评估等由使用方以
   脚本/内容实现（这正是"mod 一等公民"的延伸，也符合引擎不含游戏内容的边界，
   `ARCHITECTURE.md` 法则 10）。
3. 落地动作：scope 扩张（规格 04）到 War/Front/Army 域时，只挂**引擎已实现**
   的机制原语；未实现机制（殖民推进、海战模型）不提前暴露——写入每域规格的
   "机制缺口评估"节。

## 4. 差异化验收

| 能力 | 判据 |
|---|---|
| 规模压力 | 5000 原语/50000 定义在预算内装载+链接；符号碰撞计数=0；1e6 AST 上限改为可配置且压力包通过 |
| lint 报告 | 参考内容包零 Error；坏内容子集给出可读修复建议 |
| desync 定位 | 构造双机分歧夹具（改一个字段）→ 工具 30 秒内定位到 tick+store+行 |
| 参考内容包 | 8 国闭环可装载/存档/lint 绿；示例=夹具=回归测试三合一 |
| AI/战争边界 | 引擎侧仅内核设施；内容侧可完全覆盖策略行为 |
