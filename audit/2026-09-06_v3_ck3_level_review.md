# Thunder × V3 / CK3 级别评审（2026-09-06）

**评估对象**：`D:\Thunder`（C++23 确定性大战略引擎，src 约 56.5k 行 + tests 约 9.9k 行）
**对标基准**：Victoria 3 / Crusader Kings 3（Clausewitz + Jomini）。按用户指示排除多人游戏与音频。
**方法**：全量构建 + 全量测试基线 → 各子系统源码审读（脚本/内容/模拟/gameplay/CI 重点）→
对 2026-09-05 三份审计（parity review 6.2/10、completeness check、post-package reassessment）
逐项核实"声称修复"是否真实落地 → 修复本次发现的问题 → 补足语言层缺口。
**全部结论附 `文件:行号` 证据；本轮改动均通过 27/27 套件回归。**
**更新（同日第二批）**：本报告 §7 记录了随后落地的值表达式语法前端——这是 2026-09-05
审计 P0-5（嵌套算术树）与 P0-2（比较运算符）的正式实现，语言层短板从"原型"推进到"可用"。
综合评分修订为 **≈7.0/10**（脚本语言 5→6.5、模拟域 4.5→5、Mod 生态 5.5→6）。

---

## §7 第二批：值表达式语法前端（同日晚间追加）

2026-09-05 审计把"值表达式前端"列为剩余语言工作的最高杠杆项：字节码 VM
（`ScriptVmValues.cpp`，Add/Sub/Mul/Div/Neg、Eq/Ne/Lt/Le/Gt/Ge、And/Or/Not/If、
Max/Min/Clamp）早已完整实现，却没有语法能产出它；`PushScriptedValue` 更是空操作
（不压栈）——一旦有生产者就会静默错值。本批落地：

### 7.1 修复的潜伏缺陷

- **`PushScriptedValue` 空操作**：VM 对该 opcode 什么都不做（`ScriptVmValues.cpp` 原
  100-103 行注释"delegates through standard recursive context check"实际未实现）。
  现按 `value_refs` 池解析、带调用帧守卫与深度传递地递归求值（预算不重置——规避
  ORDER BY 曾犯过的 `begin_execution()` 复位类 bug）。
- **指令体积统计漏账**：`instruction_bytes()` 未计入迭代器过滤条件、random_list 权重、
  else 分支与（新）字节码池。补齐（`ScriptProgramDatabase.cpp:14-38`）。

### 7.2 新语法（对照 specs/02、03 的验收示例）

1. **`expression = (...)`**（`scripted_value` 字段）：操作数为内置值源（与 `source =`
   同一集合，经共享的 `source_value()` 辅助函数读取同一批世界列）、`value:<name>`、
   `var:<name>` 与常量；`+ - * /`、括号、一元负号、标准优先级。新 `PushSource` opcode。
   `value:` 引用进入链接验证（存在性/作用域匹配/调用环，`ScriptProgramDatabase.cpp`）。
2. **触发器行内比较**：`population > 1000`、`value:income - 50 >= 40`、`pop_size <= 100`。
   属性键成为首个表达式操作数，比较符（`> < >= <= !=`）进入词法层，编译为
   `ScopedConditionKind::SourceCompare`（两侧字节码在当前作用域求值，容差语义与
   字节码比较算子一致）。可嵌套进 `all/any/not`、scope 选择器、`limit = {}` 门与 `if`。
   `==` 有意不做行形式（与 `key = value` 语法冲突，等值走既有命名原语）。
3. **常量折叠统一**：常量表达式与含操作数表达式走同一条符号解析路径，折叠结果与
   旧折叠器逐位一致（左结合）；常量除零仍是硬解析错误。

### 7.3 实现要点（工程决策记录）

- 解析器 AST：`ScriptValueKind::Expression` 节点，key=运算符符号（`"+"`/`"neg"`/`">"`…），
  children=操作数；符号树替换字段节点后**保留 source line/column**（诊断定位）。
  常量折叠后的字段保留原 key（`expression`）与 Number kind——编译器按
  key+kind 双条件分发两种形态。
- 触发器比较行的 LHS 支持完整表达式（键作为首操作数经
  `parse_symbolic_expression_from` 续析），无比较符的 `键 + 算子` 行回退为
  原有 "expected '='" 错误路径——零遗留内容行为变化。
- 字节码池：`source_pool`（ValueSource）与 `value_refs`（SymbolId）双池随 op 序列
  顺序消费；`instruction_bytes` 同步计账。
- 调试过程中修复三处自伤（字段 key 被符号树覆盖、常量折叠字段漏分发、
  `ScriptCallGuard` 空参构型），均由新增测试捕获。

### 7.4 验证

- 新测试 `test_value_expressions_and_comparisons`：表达式 scripted_value 对活世界态求值
  （`(1000*0.5 + 100*2)/1000 = 0.7`）、折叠常量、比较双向、变量操作数、未知操作数诊断。
- 参考内容 `examples/10_value_expressions_and_comparisons.core`（示例=夹具=回归三合一）。
- 全量 27/27 通过；存档/回放/确定性套件无回归（字节码为内容层，不触碰存档格式）。

### 7.5 评分影响

| 维度 | 2026-09-05 | 第一批后 | 第二批后 |
|---|---|---|---|
| 脚本语言表现力 | 3 | 5 | **6.5**（算术树+比较符落地，剩 while/数组/日期/加权值） |
| 模拟域深度与广度 | 4 | 4.5 | **5**（已实现数据经表达式+迭代器对内容全面可达） |
| Mod 生态就绪度 | 5 | 5.5 | **6**（平衡公式、事件条件不再需要改 C++） |
| **加权总评** | 6.2 | ≈6.6 | **≈7.0** |

---

## §8 第三批：控制流收尾——`while` + `else_if`（同日追加）

2026-09-05 审计 P0-3/P0-6 的最后一块。至此 spec 02 的控制流清单全部落地
（if/else → 第一批；random_list → 第二批；while → 第三批）：

- **`while = { limit = { <conditions> } ... }`**：条件每轮在当前作用域重评，每轮计入
  VM 工作预算——永不失败的 limit 以预算异常确定性终止（`ScriptVm.cpp` While case，
  `kMaxScriptDepth`/预算机制复用）。测试覆盖五轮变量循环与无限循环的确定性终止。
- **`else_if` 链**：编译期降为嵌套 If（`else_children` 内层），**零新 IR**——存档与
  checksum 完全不受影响；首个命中分支短路，尾部 `else` 收链（兄弟节点布局与
  Clausewitz 一致）。`innermost_else_chain` 辅助处理 `if → else_if → else` 的追加目标。
- 验证：`test_while_loops_and_else_if_chains`、示例
  `examples/11_control_flow_while_and_else_if.core`；全量 27/27 回归通过。

**评分修订**：脚本语言表现力 6.5 → **7**（控制流完备，仅剩数组/日期/加权值）；
综合 **≈7.1/10**。语言层下一杠杆仅剩格式层数组/混合容器（spec 01，原型已验证），
其后应转向 scope 扩张（Building → War/Front → Character）。

---

## §9 第四批：Building 域——scope 扩张第一批（同日追加）

2026-09-05 审计 P0-8 的正式开工。`ScopeType` 从 5 域扩到 **6 域**（Building），
spec 04 建议的"Building 先行"落地：

- **作用域管线**：`ScopeRef::building`；`valid()` 走槽位存活检查（与 Pop 同级）；
  `all()`/`children()` 接入——建筑经由所属市场的国家归属（country→buildings），
  省/州链路走冷列（province/state→buildings），市场直接（market→buildings）；
  `owner =`/`market =`/`province =` 选择器自建筑可用。
- **即时可用的全部新能力**：`every_building`/`ordered_building`/`random_building`
  迭代器、`limit = { ... }` 门控、`while`、`random_list`、行内比较、值表达式——
  第一至三批的语言能力在新区域自动生效（这正是"先修语言、后扩域"顺序红利的直接体现）。
- **内置原语 12 → +8**：trigger `building_level_above` / `building_employees_above` /
  `building_profit_above` / `building_cash_above` / `building_wage_above`；
  effect `set_building_level` / `set_building_employees` / `building_add_cash`。
  值源 `building_level` / `building_employees` / `building_profit` / `building_cash`
  进入 `source_value()` 共享路径——表达式与旧式 source 读同一批世界列。
- **存档安全**：`validate_persistent` 经 `ScopeResolver::valid` 校验（已扩展），
  建筑作用域引用在保留的 gameplay 上下文中可验证、可 checksum；旧存档不受影响
  （ScopeType 序列化为字节，新枚举值只出现在新内容中）。
- **语法规则固化**：表达式值必须以 `(` 开头（裸词值是单符号）——文档已明确，
  示例 10 已同步修正。
- **验证**：`test_building_scope_and_primitives`（国家域门控迭代、市场域
  ordered_building + 表达式排序、建筑域行内算术比较、表达式 scripted value）；
  全量 27/27 回归。

**评分修订**：模拟域深度与广度 5 → **5.5**（建筑域全可脚本化，帝国经济内容可写了）；
Mod 生态 6 → **6.5**。综合 **≈7.3/10**。

**下一杠杆**（按 spec 04 顺序）：War/Front 域（GrandStrategyStore 的战争/前线记录
接入 scope，让 1482 行大战略数据可脚本触达）→ InterestGroup → Character（CK3 级
玩法前提，最大单项）。格式层数组/混合容器（spec 01）可并行。

---

## §10 第五批：War/Front/Army 域——scope 扩张第二批（同日追加）

spec 04 批次二落地，`ScopeType` 达 **9 域**。GrandStrategyStore 的战争/前线/军队
记录（此前审计判定"数据在、脚本摸不到"的最大块资产）全部可脚本触达：

- **导航语义**（与引擎现有遍历规则逐一对齐）：
  - 国家→军队：仅本国单位（`ArmyRecord.country`）；
  - 战争→前线：攻守双方国家对**任意顺序**匹配——与 `run_warfare_weekly` 的
    same/reverse 判定完全一致，内容推演的战场集合与模拟推演的战场集合天然一致；
  - 国家→战争：**仅活跃战争**（玩家可行动的语义）；全局 `every_war` 见全部记录，
    由 `war_active = yes/no` 过滤——布尔 typed trigger 的首个内置用例。
- **原语 +10**：trigger `army_manpower_above` / `army_organization_above` /
  `front_progress_above` / `war_score_above` / `war_weeks_above` / `war_active`；
  effect `army_add_manpower`（饱和加）、`set_army_organization`、
  `shift_front_progress` / `shift_war_score`（钳制在 ±100'000 milli 关系刻度，
  与周更逻辑同界）。值源 5 个进 `source_value()` 共享路径，表达式/行内比较即用。
- **确定性**：record 向量为稠密索引（无 slot pool），遍历天然稳定；effect 全部
  有界钳制，不引入未定义状态。
- **验证**：`test_war_front_army_scopes_and_primitives`（国家域门控动员只触及本国
  部队、战争域前线成对匹配推演、战分迁移、布尔触发+行内比较、前线表达式值）；
  示例 `examples/13_war_front_army_scopes.core`；全量 27/27 回归。

**评分修订**：模拟域 5.5 → **6**（战争/军队内容可写，1482 行数据不再是死资产）；
综合 **≈7.5/10**。

**剩余 scope 缺口**：InterestGroup/Party（政治域，数据已实现）、Treaty/Colonial、
Character/Dynasty（CK3 级玩法前提，数据模型不存在，最大单项）、Navy/SeaZone
（与本批同构，可随时照抄）。

---

## §11 北极星修正与第七批：人口自然增长（2026-09-06 晚）

### 11.1 方向决策（用户裁定，固化为 README 架构法则 11）

- **不做 Character/Dynasty/朝代系统**——此前审计"CK3 级玩法前提是 Character 域"
  的路线图项**作废**。§10 的剩余 scope 缺口中，InterestGroup/Party/Treaty 等
  政治域降级为"仅在服务经济模拟时接入"。
- **北极星 = 经济与金融模拟**，最终目标函数：**经济增长 + 生活水平提高**。
- **CK3 仅对齐地图表现效果**（渲染层），不引入其玩法系统。

这条修正把引擎定位收敛为"以生活水平为目标函数的确定性经济社会模拟引擎 +
CK3 量级的地图表现"。后续所有规划按此过滤。

### 11.2 第七批：自然人口增长——封堵北极星的核心断环

核查发现：**全模拟此前没有出生/死亡**——POP 只因战争伤亡与迁移变化，
"生活水平提高 → 人口/需求增长 → 经济增长"的因果闭环根本不存在。本批落地：

- **`EconomySystem::population_growth`**：`run_weekly` 首相位，纯整数、无 RNG、
  按周确定性执行。采用**真实人口转型（demographic transition）曲线**——
  初版"出生率恒定、越富增长越快"的模型已被推翻重做：
  - 死亡率对生活水平即时响应：温饱（5000 milli）≈ 41/千人年 → 富裕
    （30000 milli）≈ 9/千人年；危机（<2500 milli）额外上浮至 ≈ 64/千人年；
  - 生育率**滞后一代人**才开始下降：温饱到 10000 milli 保持 ≈ 42/千人年，
    富裕时降至 ≈ 10/千人年；
  - 净增长呈转型驼峰：温饱 ≈ +1.0%/年 → 峰值（10000 milli）≈ +0.73%/年 →
    富裕回落至 ≈ +0.1%/年。**财富不再单调推高增长**——这正是现实中
    "越富出生率越低"的正确形状；
  - 赤贫人口收缩（零生活水平时 ≈ −2.2%/年），下限 1 人（残存户可恢复）。
- **分数增长精确累积**：新 `PopStore.growth_progress_milli` 列（毫人）逐周累积、
  跨周进位——任意规模的 POP 都按精确速率增长，无小人口停滞问题。
  列入 checksum（desync 覆盖）、compact/destroy 生命周期，并经新增 tagged
  扩展段持久化（旧存档走既有 legacy 摘要路径保持可读）。
- **验证**：`test_population_growth_tracks_standard_of_living`（转型峰值/富裕/
  温饱/赤贫四类 POP 的精确周增量、**驼峰断言**——中段增长必须高于两端、
  余数进位、残存下限、settlement 聚合增长后规模）+
  `test_population_growth_section_roundtrip`；全量 27/27 回归。

### 11.3 修正后的路线图（覆盖 §4 中与北极星冲突的项）

1. **经济/金融深化**（最高优先）：国家层宏观指标——CPI（菜篮子成本已有
   `profile_basket_cost_milli`）、实际 GDP、国家加权生活水平聚合；POP 需求
   弹性（奢侈/刚需分层）；移民对 SoL 差距的响应已有基础（province attraction）。
2. **增长闭环补强**：技术/生产方式提升拉高 SoL → 人口增长 → 市场扩张的数值
   调平（bench 门禁）。
3. **地图表现对齐 CK3**（表现层唯一目标）：近景资产管线、地形/大气/水体的
   CK3 级观感——需要可视化验收环境，建议单独立项。
4. **冻结**：Character/Dynasty、统治类玩法、纯政治域 scope（除非经济需要）。
5. 保留的工程债项不变：格式层数组/回写、宽容解析、bench 门禁、热重载。

---

## §12 地图表现对齐 CK3/V3 —— 实证验证（2026-09-06 晚）

### 12.1 本机实证（非 CI 复述）

- 本机装有 Vulkan SDK 1.4.357.0，首次以 `THUNDER_BUILD_VULKAN=ON` 完成
  **全量构建（525 目标，含 SDL3、glslc 编译 13 个 SPIR-V 模块、约 20k 行呈现层）**，
  全部通过；该配置下 27/27 测试绿。呈现层此前只有 CI 门禁，本机从未编译过。
- 限制声明：本环境无 GPU/显示，**像素级渲染验收无法进行**——本节的可视结论
  均为静态核查（着色器 + 渲染器 + 既有像素采样审计）。

### 12.2 已具备的地图表现（对齐 V3 的部分）

| 特性 | 证据 |
|---|---|
| 远景政治图：纸感海洋渐变、发丝国界、V3 实测阴影带（8–15px）、墨线海岸 | `shaders/world_map.frag`（1763 行）§border/coast |
| 国名脊线排版对齐 V3 官方 defines（alpha 0.8 / 拉伸 1.6） | `audit/2026-09-05_v3_reference_map_comparison.md` §8 |
| 多层 PBR 地形 splatting + 程序化地貌（针叶林、稻田、沙丘裂隙、冰川、蜿蜒河流、雪线） | `world_map.frag` §material |
| 体积大气 + 云、Gerstner 水体 | `VolumetricAtmosphereAndClouds.cpp`、terrain 管线 |
| 3D 边界网格、地图模式、省份拣选、动态 3D 旗帜、MSDF 文本、tonemap+FXAA | `BorderMesh3D` / `MapModeStore` / `ProvincePickingCache` / `DynamicFlag3D` / 后处理链 |
| 活体地图（Living Map）表现层 | `VulkanLivingMap.cpp`、`living.frag/vert` |

### 12.3 与 CK3 表现的差距（北极星下的地图差距清单）

**路线决策（用户裁定）**：近景**完全对齐 CK3/V3 的成熟实现模式——授权 3D 模型
→ 实例化放置 → LOD 链降级到 billboard——不自创程序化路线**。现有
`world_map.frag` §11 的"程序化城市涂装"本质是平面 albedo（无剪影/无体积/
无视差），在真几何管线就位后降级为**最远 LOD 的临时占位**，最终被实例模型
替代。地形高度图位移与地表 splatting 与 V3/CK3 路线一致，保留。

1. **近景 3D 资产管线未闭合**（最大缺口）：`ForestCanopyInstancer`（102 行）
   已实现但**零调用**（仅头文件 include）；`ArchitectureKit` 建筑套件只有
   cooker 消费——渲染侧没有建筑绘制路径。CK3 近景的树林/农田/城市模型
   恰是"地图表现"的主体。
2. **无网格加载器**：全仓无 obj/gltf importer；`docs/3D_ASSET_SPECIFICATION_
   AND_INVENTORY.md`（V3 规格 LOD0-3、区域建筑套件、海军/陆军模型清单）
   规范齐备但没有任何运行期消费者。
3. 地图单位为 vfx/billboard 级（`LivingMapVfx3D`），非 CK3 式 3D 兵种模型。
4. 编辑器为桩（map/gui editor），无法离线调地图观感。

**结论**：远景/政治图观感已按 V3 官方参数对齐（有像素采样审计链）；近景按
CK3/V3 模式闭合资产管线，落点明确——

**"他们的方式"的工程分解**（对齐 V3/CK3 公开实现的五步）：
1. glTF 2.0 导入器 → 既有 cooker/AssetPack（LOD 等级与流送已支持 Mesh 种类）；
2. 渲染器实例化绘制路径（接通 ForestCanopyInstancer，扩到建筑/单位）；
3. 摆放绑定：建筑记录（模拟层已有 province/market 归属）→ 地图点位 →
   模型等级映射；植被按 biome/气候散布规则；
4. LOD 链：近景网格 → 中景减面 → 最远 billboard/impostor（过渡期由现有
   frag 涂装充当最远占位）；
5. 占位资产先通链路（盒子/低模树），真实资产随后替换——管线与资产解耦。

---

## §0 结论速览

> **Thunder 仍未达到 V3/CK3 级别，但本轮把"能不能表达内容"从原型推进到了可用，
> 并修复了一个让本机质量门禁完全失效的 P0 环境缺陷。**
> 引擎骨架（确定性、SoA、Modifier、存档、脚本静态验证）保持商用级、部分领先；
> 内容语言层、scope 覆盖、战争/AI/角色深度是仍然成立的三道主差距。

一句话：**CK3 的核心域（角色/家族/阴谋）在引擎里完全不存在；V3 的核心域（市场/ pops /
货币/研究）已经做得很深。语言层修完这轮后，差距的主轴已从"表达不了"转移到"域不够"。

## §1 基线核实（本轮实测）

- 工具链：仓库外 `D:/mingw64/bin/cmake.exe` 静默损坏（退出 127/无输出）；可用 cmake 为
  pip 安装的 4.4.2。上一轮审计 P2-3 的根因即此，**仍未修复（仓库外文件），已写入 README 规避**。
- **发现 P0：本机 27 个测试中 15 个以 `0xc0000139`（STATUS_ENTRYPOINT_NOT_FOUND）失败。**
  - 根因：Git Bash 自带 `/mingw64/bin` 的旧 MinGW 运行库经 PATH 遮蔽编译器 GCC 15.2
    的 `libstdc++-6.dll` 等，测试可执行文件加载期找不到新版符号。
  - 这意味着此前"27 套件全绿"的判断在本机从来无法复现——**门禁失效比任何单一 bug 都危险**。
  - 修复：`thunder_bundle_runtime_dlls()`（`cmake/ThunderWarnings.cmake:23`）在构建后把
    编译器自带运行库复制到每个测试/工具/bench 旁（Windows DLL 搜索 app 目录先于 PATH），
    并接入 `thunder_add_test` / `thunder_add_tool` / bench 三处
    （`CMakeLists.txt:306-311,341-355`）。
  - 验证：以"旧运行库优先"的敌意 PATH 重跑 ctest，**27/27 全绿**。
- 旧审计声称已修复、本轮核实**属实**的项：
  - Vulkan CI 门禁：`.github/workflows/ci-linux.yml:22-35` 独立 `vulkan` job，
    `THUNDER_BUILD_VULKAN=ON` + ctest（P2-1 ✅）。
  - Clock 入 checksum：`ThunderEngine.cpp:321-331` `engine_checksum` 覆盖
    world/gameplay/ai/clock/notifications/on_actions（P2-4 ✅）。
  - `if`/`else` 条件效果 + 常量算术折叠：`ScriptCompilerScoped.cpp:479-524`、
    `ThunderScriptParser.cpp` 折叠器，均有测试（✅）。
  - 双轨数值已收敛：`CountryStore` 全部写路径同步维护 milli（权威）与 double（派生），
    `CountryStore.cpp:168-206`（2026-09-05 审计的"最刺眼化石"已消除 ✅）。
- 设计核查中确认**做得对**的（保持，不要动）：
  - 脚本集合权威序在 `std::vector`、`unordered_set` 仅作非持久索引
    （`ScriptContext.hpp:24-37`）；模拟/运行时核心无迭代序敏感的无序容器。
  - OnAction 队列：排序队列 + 稳定 ID 决胜 + 预算/溢出/陈旧作用域全检
    （`OnActionRuntime.cpp:80-168`）。
  - 经济闭环：货币/material 双恒等式审计 + 并行确定性归约
    （`EconomyMarketPhases.cpp:175-256`）；价格死带截断修正（`:229-231`）。

## §2 本轮修复的引擎缺陷（除上述 P0 外）

### 2.1 P1：同类型迭代器语义错误——世界不可枚举（已修复）

`ScopeResolver::children`（`ScopeResolver.cpp:90`）原实现 `s.type == target` 时直接
`return {s}`：**在国家作用域里 `every_country` 只迭代当前国家自己**。Clausewitz 语义中
`every_country/every_state/every_province` 永远全局枚举。后果："每周对全世界国家执行 X"
这类大战略最基本的内容完全无法表达，引擎已有的 31 个内置原语大半被锁死在单实体上。

修复：同类型迭代改为 `ScopeResolver::all(w, target)` 全局枚举（索引序，确定性不变）；
跨类型导航（国家→所属州/省/市场、pop 过滤）保持原语义。唯一调用方为 VM 两处迭代器路径，
全量回归无破坏。

### 2.2 P1：迭代器缺 `limit = { <conditions> }` 候选过滤（已补齐）

Clausewitz 内容的头号惯用法。原编译器只接受数字 `limit`（数量上限），块状 `limit` 会
落到"unknown effect/trigger: limit"诊断。现编译器/VM 双侧接入：

- IR：`CompiledScopedEffect::has_iterator_condition`（效果侧复用 `condition` 槽位）、
  `CompiledScopedCondition::iterator_filter`（触发侧），均在被迭代作用域内编译
  （`ScriptCompilerScoped.cpp` 迭代器两分支）。
- VM：四种模式（Every/Ordered/Random/Collection）先过滤后应用，过滤计入工作预算；
  `ordered_*` 先过滤再 order_by/offset/limit；触发侧 `every_*` 过滤后空集为真空真
  （与 Clausewitz 一致）（`ScriptVm.cpp` 两处 Iterator case）。
- 数字 `limit = N` 语义不变，可与块过滤共存于同一 `ordered_*`。

### 2.3 P1：缺 `random_list` 加权随机（已补齐）

- 解析器：块子项键名接受 Number token（`ThunderScriptParser.cpp`，此前是硬解析错误，
  零兼容性风险）——`random_list = { 10 = { ... } 90 = { ... } }`。
- 编译器：`ScopedEffectKind::RandomList` + `Group`；非数字键给出行号诊断；
  `is_advanced_effect` 路由（`ScriptCompiler.cpp:439`）。
- VM：累计权重对全量取模；新增 `deterministic_draw`（64 位全量值，独立域分隔符），
  `deterministic_index` 配方逐字节不动（既有随机迭代器取值流不变，存档/回放不受影响）。
- 事件选项/AI 加权的内容表达自此可用。

### 2.4 新增验证资产

- `tests/thunderscript_runtime_tests.cpp`：`test_iterator_limit_condition_filtering`
  （效果/触发双侧、ordered 门+上限、空集语义）、`test_random_list_weighted_branches`
  （确定性、双分支可达性、坏权重诊断）。
- `reference-content/examples/09_iterator_gating_and_random_list.core`（示例=夹具=回归三合一）。
- `docs/THUNDER_SCRIPT.md` 两个新语法节 + gaps 清单修订；`CHANGELOG.md` 完整条目。

## §3 对照 V3/CK3 的差距矩阵（本轮复核后）

| # | 维度 | 2026-09-05 | 本轮 | 依据 |
|---|---|---|---|---|
| 1 | 分层与依赖治理 | 9 领先 | 9 | configure 期边界禁令不变 |
| 2 | 文本格式层 | 3 | 3 | 仍无数组/混合容器/比较符/日期/回写 |
| 3 | 脚本语言表现力 | 3 | **5** | if/else+迭代器过滤+random_list 落地；仍缺 while/数组/比较符/算术树前端 |
| 4 | 脚本编译静态验证 | 9 领先 | 9 | 全库链接/调用环/签名校验保持 |
| 5-9 | 数据模型/存储/Modifier/确定性/Mod VFS | 7-8 | 7-8 | 无回归；双轨数值已收敛 |
| 10 | 存档与迁移 | 6 | 6 | 仍缺可读导出 |
| 11 | 本地化 | 5 | 5 | 无复数规则/`l_<lang>` 约定 |
| 12 | Scripted GUI | 8 领先 | 8 | — |
| 13 | 地图与渲染 | 6 | **7** | Vulkan CI 门禁本轮确认落地 |
| 14 | 模拟域深度 | 4 | **4.5** | 经济/货币/研究深；战争 217 行、AI 521 行、无 Character 域 |
| 15 | 工具链与编辑器 | 5 | **6** | 本机测试门禁修复；cmake 损坏属仓库外环境，已文档化规避 |
| 16 | 测试与质量门禁 | 7 | **8** | 27/27 实测可达（含敌意 PATH）；Vulkan job 在 CI |
| 17 | Mod 生态就绪度 | 5 | 5.5 | 迭代器/随机解锁；宽容解析仍缺 |
| | **加权总评** | **6.2** | **≈6.6** | |

### 3.1 与 V3 的具体差距（经济型大战略）

- **已达标或领先**：市场供需定价、五类商品、生产方式、就业、建筑资金闭环（货币+物质双恒等式）、
  四本位制货币+银行+主权债务+FX、研究（含技术扩散）、ScriptedGui、地图数据管线。
- **仍缺**：利益集团/政治运动对玩法层的实际驱动（数据在、机制薄）、战争（前线 73 行标量推进
  承载不了 V3 的 front 系统）、外交博弈只留数据结构、建筑近景资产生成、事件权重/冷却的
  完整化（本轮 random_list 补掉一半）。

### 3.2 与 CK3 的具体差距（角色型大战略）

- **结构性缺席**：`ScopeType` 仅 Country/State/Province/Pop/Market 五域——
  **Character/Dynasty/House/Scheme/Claim/Council 全部不存在**。CK3 的玩法主体
  （角色驱动事件、继承、阴谋、宫廷）在当前引擎上无法起步。这是"是否做 CK3 级"的
  一票否决项，也是 scope 扩张路线图（specs/04）里最重的一块。
- 战争/AI 薄（同 V3 项）；角色域缺失使"决策为什么被做出"失去载体。

### 3.3 语言层剩余清单（按修复顺序）

1. **值表达式语法前端**——`ScriptValueBytecode` VM（Add/Sub/…/If/Max/Min/Clamp）已在
   `ScriptVmValues.cpp` 完整实现，只差把 `a > b`、`(x+y)/z` 语法接到字节码。这是剩余
   语言工作里**性价比最高**的单项：比较符与算术树一次解锁。
2. 数组/混合容器 + 文本回写（原型已验证：`audit/improvement-package/prototypes/format_layer/`）。
3. `while`（VM 预算机制已就绪，编译器加关键字即可）。
4. scripted value 作为 random_list 权重（本轮已留 IR 空间）。
5. Date/Duration 类型、宽容解析分级诊断、内容 lint CLI、热重载。

## §4 未来规划（建议排序）

**原则不变**：保持静态验证优势、不引入多人与音频范围、经济闭环恒等式不破坏。

- **P0（1-2 周）**：值表达式前端接 `ScriptValueBytecode`（§3.3.1）；规模压力包接入
  ctest（`generate_scale_pack.py` 已交付，验证 5k 原语/50k 定义的装载预算）。
- **P1（4-8 周）**：格式层数组+比较符+日期（spec 01）；scope 扩张第一批
  Building→War/Front（spec 04，让 GrandStrategyStore 已实现的条约/海军/前线可被脚本触达）；
  宽容解析分级诊断（spec 05，mod 生态命门）。
- **P2（季度级）**：Character/Dynasty 域（若目标含 CK3 级产品，这是最大单项，先做
  数据模型+scope+原语最小闭环）；战争从标量推进升级到前线-会战解析；AI 多入口长线规划；
  bench 阈值门禁、脚本回写、存档可读导出、本地化复数。
- **持续**：每个新语法特性在 `reference-content/examples/` 落一条"示例=夹具=回归"；
  修 P2-3 的仓库外 cmake（建议删除或替换 `D:/mingw64/bin/cmake.exe`，或在 PATH 前置 pip cmake）。

## §5 最终判断

- **作为引擎发布**：合格且trait清晰——确定性契约、数据布局、静态验证、mod VFS 都是
  商用级或以上，本轮修复后门禁在本机真实可复现。
- **对标 V3**：经济/货币/研究骨架已到位，语言层本轮进入"可用"；补完 §4 P0/P1 后具备
  承载 V3 型玩法内容的生产条件。
- **对标 CK3**：未达标，且差距是结构性的（无角色域）。是否投入 Character 域是路线级
  决策，应先于一切渲染/工具优化。
- **本轮若只记一件事**：本机 15/27 测试静默失败暴露了"门禁失效"比"功能缺失"更危险——
  修复已落地为构建系统不变量，之后任何环境都无法让这个门禁悄悄失效。

## §6 证据索引（本轮改动）

| 改动 | 位置 |
|---|---|
| DLL 就近部署函数 | `cmake/ThunderWarnings.cmake:23-41` |
| 测试/工具/bench 接线 | `CMakeLists.txt:311,344,350,356` |
| 同类型迭代全局语义 | `src/thunder/scripting/ScopeResolver.cpp:90-100` |
| 迭代器过滤（效果侧编译） | `src/thunder/scripting/ScriptCompilerScoped.cpp`（Iterator 分支） |
| 迭代器过滤（触发侧编译） | 同上（`compile_scoped_condition_node` Iterator 分支） |
| 迭代器过滤（VM 双侧） | `src/thunder/scripting/ScriptVm.cpp`（两处 Iterator case） |
| random_list 编译 | `ScriptCompilerScoped.cpp`（`lower == "random_list"` 分支） |
| random_list 求值 + 全量抽签 | `ScriptVm.cpp`（Group/RandomList case、`deterministic_draw`） |
| 数字键解析 | `src/thunder/scripting/ThunderScriptParser.cpp`（parse_block） |
| 高级效果路由 | `src/thunder/scripting/ScriptCompiler.cpp:439` |
| 测试 | `tests/thunderscript_runtime_tests.cpp`（新增 2 函数） |
| 示例 | `reference-content/examples/09_iterator_gating_and_random_list.core` |
| 文档 | `docs/THUNDER_SCRIPT.md`、`README.md`、`CHANGELOG.md` |
