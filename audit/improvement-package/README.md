# Thunder 提升工程包（Improvement Package）

> 依据 `audit/2026-09-05_clausewitz_jomini_parity_review.md` 的对标结论与既定提升方案，
> 将每一个提升点（P0/P1/P2/P4）落实到可交付成果。本包为**实施就绪（implementation-ready）**
> 规格集：每份规格给出设计决策、接口签名、代码落点（`文件:行号` 锚点）、改前/改后示例、
> 迁移路径、验收测试清单与工作量量级。
>
> 状态：2026-09-05 编制。代码落点基于同日源码审计；落点行号可能随改动漂移，合并前以
> grep 复核为准。

## 1. 交付物地图

| 交付物 | 内容 | 覆盖提升点 |
|---|---|---|
| `specs/01-format-layer.md` | 格式层扩展：数组/混合容器、比较运算符、日期、引号文本、无损回写 | P0-1 P0-2 P0-7 P1-7（写器设计） |
| `specs/02-script-language.md` | 脚本语言：`if/else/else_if/while/random_list`、`limit` 语义冲突解决、事件选项/冷却/权重、on_action 总线、全局变量 | P0-3 P0-4 P0-6 P1-2 P1-3 |
| `specs/03-value-system.md` | 嵌套算术 script value IR + 与 ModifierGraph 集成 | P0-5 |
| `specs/04-scope-expansion.md` | scope 域扩张手册（Building/Character/Army/Navy/War/Front/Treaty/InterestGroup/Institution）+ 原语挂牌节奏 | P0-8 |
| `specs/05-content-ecosystem.md` | 宽容解析/诊断分级、内容 lint CLI、本地化复数与 `l_<lang>` 约定、热重载、存档可读导出 | P1-1 P1-6 P1-8 P1-5 P1-7 |
| `specs/06-engineering-quality.md` | CI/性能门禁、本机工具链修复、clock checksum 加固、generation 决策、调度并行、测试统一、编辑器决策 | P2-1 … P2-8 |
| `specs/07-scale-and-differentiation.md` | 规模压力验证 + 差异化能力（desync 定位、诊断产品化、参考内容包） | P1-4 P4 |
| `reference-content/examples/*.core` | 新语法验收夹具（每条对应一份规格的示例） | 全部 |
| `reference-content/scripts/generate_scale_pack.py` | 合成压力内容包生成器（数千原语/数万定义），同种子逐字节确定，已运行验证 | P1-4 |
| `prototypes/format_layer/` | **可编译运行**的格式层扩展原型（数组/运算符/日期/引号文本/无损回写），含运行输出 | P0-1 P0-2 P0-7 P1-7 |

## 0. 已运行验证清单（2026-09-05）

| 交付物 | 验证方式 | 结果 |
|---|---|---|
| `prototypes/format_layer` | `g++ 15.2.0 -std=c++23` 编译 + 内置样例与 `sample_v1.core` 运行 | 全断言通过，`ROUNDTRIP_OK`（verbatim 逐字节 + pretty AST 等价） |
| `reference-content/scripts/generate_scale_pack.py` | Python 3.13 双跑同种子 diff | `DETERMINISM_OK`（12 文件逐字节一致） |

## 2. 工作分解（建议执行顺序）

排序逻辑：**格式层是语法前提 → 控制流与值系统是表达力核心 → scope 扩张解锁领域 →
生态工具放大产能 → 质量门禁保证可持续 → 规模验证兜底**。

| 序 | 里程碑 | 产出规格 | 量级（粗估人周） |
|---|---|---|---|
| M1 | 格式层扩展 + 无损写器（含原型已验设计） | 01 | 4–5 |
| M2 | `limit` 重命名与 `if/else/while/random_list` 落地 | 02 | 5–6 |
| M3 | 嵌套算术值 IR + Modifier 集成 | 03 | 4–6 |
| M4 | 第一批 scope（Building 先行） | 04 | 4–6 |
| M5 | 宽容解析 + 诊断分级（`--strict`/默认宽容） | 05 | 2–3 |
| M6 | 事件选项权重/冷却 + on_action 总线 + 全局变量 | 02/06 | 4–5 |
| M7 | 内容 lint CLI + 本地化复数 | 05 | 2–3 |
| M8 | CI vulkan job + bench 门禁 + 工具链修复 | 06 | 1–2 |
| M9 | 规模压力包（生成器+门禁） | 07 | 1–2 |
| M10 | 热重载 + 存档导出 + desync 工具（持续） | 05/07 | 4–6 |

**里程碑间的依赖**：M1 → M2 → M3 为语言主线（格式是语法前提，控制流与值系统是
表达力核心，必须顺序推进）；M4 可与 M2/M3 并行（域扩张不依赖控制流）；M5 依赖
M1 的 Text/日期形态分类；M6 依赖 M2 的 random_list 语义；M7/M8/M9 相互独立可
随时插入；M10 依赖 M1（写器）与 M5（事务）。

每个里程碑的**完成判据**：对应规格的「验收测试清单」全部通过 + 27 套件回归不破 +
新增/改名关键字无存量内容引用（grep 空）。

## 3. 总验收门（全部里程碑完成后）

1. **表达力等价**：用 ThunderScript 重写三段真实 Clausewitz 风格内容并语义等价——
   `common/scripted_effects` 含 `if/limit/random_list` 的效果、`common/script_values`
   三层嵌套值、`events` 含加权选项的事件（见 `reference-content/examples/`）。
2. **覆盖证明**：`generate_scale_pack.py --primitives 5000 --definitions 50000` 产出内容
   可在装载预算内通过全库链接（预算见 07 规格）。
3. **兼容保持**：`limit = 5`（旧语法，别名告警）与 `if = { limit = { } }`（新语法）
   同时可编译；v4 存档照常读取，新上下文仅出现在 v5 区段。
4. **质量门禁**：27 套件 + 新增用例全绿；`ci-vulkan` job 通过；bench 阈值断言通过。
5. **生态闭环**：`thunder_content_lint` 可对参考内容包输出零 error 诊断；`l_en`/`l_zh`
   文件约定可被装载；存档可导出为可读文本。

## 4. 规格书写约定

- **落点**：`路径:行号`，为 2026-09-05 审计基线。
- **接口**：给出真实 C++ 签名草案，可编译为目标形态。
- **示例**：ThunderScript/`.core` 采用与示例夹具一致的口径。
- **迁移**：任何关键字/语义变化必须写明兼容期与别名策略。
- **工作量**：量级仅供排序，非排期承诺。

## 5. 本轮交付边界（重要）

- **已落实为可运行代码**：`prototypes/format_layer`（格式层参考实现，本地编译运行
  全绿）；`reference-content/scripts/generate_scale_pack.py`（压力生成器，确定性
  已验证）。
- **已落实为实施就绪规格**：P0-1 … P0-8、P1-1 … P1-8、P2-1 … P2-8、P4 全部提升点，
  每份含设计/接口/落点/示例/迁移/验收测试/工作量（见第 2 节映射表与三份清单：
  致命短板、兼容性差距、风险登记全部闭环）。
- **未做引擎源码合并**：本机 cmake 不可用（`/d/mingw64/bin/cmake` 退出 127），无法
  构建 27 套件门禁；对 55k 行引擎做跨语义盲改会破坏既有测试，故本轮以
  "规格 + 可运行原型 + 夹具 + 生成器"交付。**M1（格式层合并）应在工具链修复
  （P2-3 规格 §3）之后开工**，以原型为参考实现、以 `examples/01_format_layer.core`
  为首个验收夹具。
- 规格中的工作量与代码落点是对审计基线的估计，合并前以 grep 复核锚点。
