# 引擎完备度体检（排除多人游戏与音频）

`audit/2026-09-05_engine_completeness_check.md`
日期：2026-09-05 深夜
方法：以本日全部审计 + 长期记忆的硬缺口清单为底，对每个可疑点做**当日新鲜扫描**
（行数、grep 证据），逐项核实后分级。范围：除多人/音频外全部子系统。

---

## §0 总评

**引擎核心已达"可承载内容开发"水平，但离"能做完一款 1836 大战略游戏"还差明确的五层。**
骨架（分层治理、确定性契约、经济市场货币、地图数据管线、UI、内容管线）完备且有
27 套件 CI 门禁；短板集中且可枚举：**内容语言表达力、近景资产管线、AI/战争深度、
工具链（Vulkan CI/bench 门禁）、遗留死代码**。所有缺口均有精确落点，无方向性风险。

---

## §1 完备矩阵（当日核实）

### ✅ 完备（可直接承载内容）

| 子系统 | 证据 |
|---|---|
| 确定性契约 | 27 测试套件/29 add_test；checksum 域分隔种子；存档 round-trip 校验 |
| 经济与市场 | 供需定价、就业、建筑/生产方式渐进切换、原料守恒（economy_tests 全绿域覆盖） |
| 货币金融 | 四类本位制+平价+金点+Gresham+铸币税+银行资产负债+主权债务/违约+FX 双结算路径（今日补齐商品侧，`coin_commodity_value_milli`） |
| 地图数据层 | 六级管道、A2-R 全驻留金字塔（≈119 MiB 零流式）、省份 ID 栅格→调色板→等值面边界（同构 Clausewitz） |
| 地图远景外观 | 饱和平面政治图+渐变+发丝界+阴影带+白州界+海岸墨线+V3 海洋模型+经纬网（今日像素级对齐） |
| 内容管线 | DefinitionDatabase ingest/原子绑定、goods 品类五类、Mod VFS、本地化 |
| UI 系统 | ScriptedGui、UiTheme、MSDF 文本+国名脊线排版（Playfair/0.8 alpha/1.6 拉伸，官方参数对齐） |
| 研究系统 | 470 行 + 内容绑定 + 测试 |
| 大战略数据 | GrandStrategyStore 1,482 行：条约/海军/前线/利益集团**数据层已实现**（但见 ⚠️） |
| 渲染地基 | 非锁步 SnapshotExchange、画质分档、相机、Vulkan 后端（glslc 全量编译验证） |

### ⚠️ 部分完成

| 子系统 | 现状 | 缺口 |
|---|---|---|
| 脚本语言 | 解析/编译/VM/静态验证（差异化资产） | **无 if/else/while/random_list**（当日 grep 确认零控制流）；ScopeType 仍 5 个；内置 31 个；格式层无数组/比较运算符/日期/回写 |
| AI | UtilityAi 436 + StrategicAiPlanner 85 = 521 行 | 单入口策略决策，无长线规划/多国博弈 |
| 战争 | BattlePhase 73 + Logistics 43 = **116 行** | 标量推进，无战线推演/战役解析 |
| 近景表现 | PBR 地形+挤出+军队 billboard | 无树/田/建筑（网格导入器缺失）；近景语言偏写实 vs V3 手绘 |
| 存档编辑 | SaveGame round-trip 完整 | **无脚本文本序列化器**（编辑器保存被阻塞，当日确认） |
| 输入 | 相机/UI API 齐备 | 事件泵属外部宿主（引擎/游戏边界设计使然，非缺陷但宿主需自带） |

### ❌ 缺失（工程债，当日核实）

1. **Vulkan CI 门禁**：`ci-linux.yml`/`ci-windows.yml` 均无 vulkan job——
   presentation 层（占库 37%）零合并门禁。
2. **mmap**：VirtualFileSystem 全量 string 拷贝（当日确认零 mmap）。
3. **StrongId generation 未接线**：ABA 风险（判定保留：debug liveness 校验路线）。
4. **scheduler 并行化**：7 个 TickTask 仅 2 个 ParallelSafe，依赖链实际串行。
5. **bench 阈值门禁**：bench 只编译不 add_test，性能回归不拦截。
6. **CJK 字体**：`set_fallback` 零调用（当日确认），6 个 ttf 未进图集。
7. **死代码 Phase 2**：~4.5k 行引擎零消费者类待删（清单在
   `audit/2026-09-05_code_architecture_tidy.md` §3，待批）。
8. 小项：`terrain_detail_octaves` 疑未生效；近景页对齐偏差 ~0.8 页；
   2048 legacy march 路径；UI 面板牛皮纸（用户裁定另轮）。

---

## §2 距离"能做完游戏"的五层（优先级排序）

1. **内容语言三件套**（单点最大，已有原型）：`if/else` + 值算术树 + 比较运算符
   ——解锁 random_list、事件链、mod 规则重写；随后 Scope 扩张把已实现的条约/
   海军/前线/利益集团**变成可脚本化内容**（现在它们只是数据结构）。
2. **Vulkan CI + 本机 cmake 修复**：37% 代码零门禁是工程风险之首；
   本机 cmake 退出 127 导致所有改动只能语法级验证。
3. **近景资产管线**：网格导入器→BuildingInstancer→树/田接线
   （`ForestCanopyInstancer` 已实现待接线；详见近景 3D 审计）。
4. **AI/战争深度**：116 行战争承载不了一战；AI 需多入口长线规划。
5. **工具链收尾**：bench 阈值、脚本回写（编辑器）、CJK、死代码 Phase 2。

## §3 结论

- 引擎**能力面**：骨架与经济/货币/地图数据层完备；外观层今日达到对标 V3。
- **短板高度集中**：语言表达力 + 资产管线 + AI/战争 + 工程门禁，全部有落点、
  无架构性返工风险。
- 排除项确认：全库无音频系统、无网络代码（均按用户指示排除在本次体检外）。
- 综合判断与此前 6.2/10 一致：**就绪度上升（模块接口文档+外观+货币商品侧），
  能力分未变（受语言层瓶颈）**。先做 §2.1 再谈其余。

## §4 证据索引

- 行数（当日）：AI 521 / 战争 116 / 大战略 1,482 / 研究 470 /
  gameplay(Notification+OnAction+ScriptedGameplay) 1,013 / living 699。
- 脚本：`Scope.hpp:7`（5 scope）；`ScriptRegistry.cpp` 31 个 register；
  控制流 grep 零命中；序列化器 grep 零命中。
- CI：`.github/workflows/{ci-linux,ci-windows}.yml` 无 vulkan job。
- 并行：`ThunderEngine.cpp` 7 TickTask / 2 ParallelSafe。
- 存档/VFS/CJK：`set_fallback` 零调用、mmap 零命中。
- 本日增量审计：`audit/2026-09-05_*.md`（对比/实施/架构/货币/接口文档共 8 份）。
