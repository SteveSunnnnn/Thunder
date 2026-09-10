# Thunder 引擎架构

最后校准：2026-09-01，对应 Thunder 1.0.0 纯引擎构建之后。

> **模块最小接口用户文档**：六层各自的对外最小调用面（门面头、核心类型、
> 最小示例、契约）见 `docs/modules/` —— `foundation.md` / `scripting.md` /
> `content.md` / `simulation.md` / `presentation.md` / `runtime.md`。
> 宿主集成从 `docs/modules/runtime.md` 开始。

本文是 Thunder 的**顶层权威文档**：职责边界、依赖方向、数据流和"什么还没接通"。
单个子系统的内部契约放在领域文档里（见文末索引），构建与验证的真实状态以
`FINAL_ENGINE_AUDIT.md` 为准。取代了旧版 `ARCHITECTURE.md`（0.1 提案版）、
`ENGINE_ARCHITECTURE.md`（解耦边界稿）、`ENGINE_GAME_BOUNDARY_AUDIT.md` 与
`LOGIC_LAYER_1_0.md`。

## 1. 使命

Thunder 只为一类游戏服务：全球尺度大战略模拟。它的设计压力来自四个约束——
十万级模拟群体、数千地理区域、稠密经济依赖、可连续缩放的三维地图，同时要求
mod 可以不改 C++ 就重写规则。任何不直接服务于这个品类的通用能力都不进引擎
（"no generic-engine tax"）。

## 2. 架构法则

法则仍然是这十条，但每条后面补上"现在由什么强制"。

| # | 法则 | 当前的强制手段 |
|---|---|---|
| 1 | 模拟不依赖渲染，渲染不得改写世界 | 后端只接受紧凑 payload：`VulkanDesktopBackend::submit_ui` / `submit_living_instances` / `submit_map_overlay` / `set_world_political_state` |
| 2 | 玩家与 UI 行为一律变成确定性 Command | `thunder/simulation/kernel` 命令校验 + world checksum |
| 3 | 运行时数据面向数据布局，强类型 ID 索引稠密 SoA | `thunder/foundation/base/StrongId.hpp`、各 `*Store` |
| 4 | 内容是数据不是 C++ | 外部内容根目录通过 VFS + ThunderScript 装载，`src/thunder` 里出现国家硬编码视为缺陷 |
| 5 | 昂贵派生状态反应式重算 | ModifierGraph 脏传播，50,000 节点链式回归防栈递归 |
| 6 | 模拟工作表达为依赖 DAG | `TickScheduler` 波次，未标 `ParallelSafe` 保守串行 |
| 7 | 确定性是功能 | 命名 RNG 流、命令定序、三年多 worker 连续 + 周期性存读档回归 |
| 8 | 昂贵 GIS 离线做完 | `thunder_world_compiler` + `.thunderworld`；运行期不解析 shp/GeoJSON |
| 9 | Mod 是一等公民 | `thunder/content/definition/ModManifest` + `ModVersion`、overlay VFS、确定性 load plan |
| 10 | 引擎不认识某一款游戏 | 配置期守卫：`src/thunder/**` 里出现 `#include "game/..."` 直接 `FATAL_ERROR` |

还有一条同样是 configure 期强制的禁令：`src/thunder` 源码不得再引用
`set_world_map_layers` 或 `world_map(_ids|_terrain|_height).thunderimg`。全图栅格
atlas 这条路径已经被正式判死，地图只有一个权威来源——`.thunderworld`。

## 3. 物理分层

```text
src/thunder  -> thunder_runtime (引擎能力，唯一可分发库)
src/apps     -> thunder_cli / thunder_world_compiler / *_cooker  (引擎命令行工具)
shaders/     -> GLSL 源码，由外部 glslc 编译为 SPIR-V
scripts/     -> GIS、资产烘焙、shader、诊断与 Windows 引擎脚本
cmake/       -> 共享 CMake 模块（警告策略、工具目标助手）
thirdparty/  -> 第三方依赖
```

依赖方向严格单向：引擎工具只能依赖 `thunder_runtime`。引擎不包含游戏组合层或随仓库
附带的作者化内容。

### 3.1 引擎库的分层

`src/thunder` 下按**依赖层级**划分为 6 层，依赖只能自顶向下，不允许反向或跨层回边：

```text
L0 foundation     基础，无引擎内部依赖
                    base/ memory/ jobs/ io/ profiling/ geo/
L1 scripting      ThunderScript 词法/语法/编译/字节码/VM/profiler
L2 content        数据驱动内容的装载与打包
                    definition/ localization/ assets/ worldpack/
L3 simulation     确定性模拟
                    kernel/ world/ economy/ grand_strategy/ warfare/
                    research/ ai/ living/ gameplay/
L4 presentation   表现层，只读模拟快照
                    render/ ui/
L5 runtime        顶层装配与持久化
                    engine/ save/ editor/
```

`thunder_runtime` 是唯一可分发库；`thunder_vulkan_backend`（`presentation/render/vulkan`）
为可选静态库，由 `THUNDER_BUILD_VULKAN` 开关控制。

### 3.2 磁盘格式标识

引擎的二进制格式采用 `THUNDR` 前缀的 **8 字节定长魔数**，扩展名统一为 `.thunder*`。

| 载体 | 魔数 | 扩展名 |
|---|---|---|
| 世界包 | `THUNDRWP` | `.thunderworld` |
| 资源包 / 材质 / 建筑套件 | `THUNDRAS` / `THUNDRMT` / `THUNDRAR` | `.thunderasset` / `.thundermat` / `.thunderarch` |
| 图像 / 字体 | `THUNDRIM` / `THUNDRF1` | `.thunderimg` / `.thunderfont` |
| 存档 / zstd 存档 | `THUNDRSV` / `THUNDRZS` | `.thundersav` |
| 矢量地图 v1 / v2 | `THUNDRV1` / `THUNDRV2` | `.thundervec` |
| 内容 / 材质定义 / GUI | — | `.thundermod` / `.thundergui` / `.thundermap` / `.thunderlod` |

**魔数必须严格 8 字节**：它们以 `std::array<char,8>` 存储并通过 `memcmp(..., 8)` 校验。
历史上曾把 `COREVEC1` 改写成 11 字符的 `"THUNDERVEC1"` 却仍按 8 字节比较，导致 V1 与 V2
的前 8 字节同为 `"THUNDERV"`、版本判定失效。改动魔数时务必保持等长，并同步修补
`tests/fixtures/` 与 `assets/fonts/` 下二进制文件的文件头。


## 4. 权威图

```text
GIS / DEM / 作者化空间层  (外部数据；--provinces 必填，见 tools/thunder_gis_compile.py:1183)
        |
        v
  thunder_world_compiler  -->  只读 world.thunderworld
        |                        |
        v                        v
  WorldTopology              流式地图页面
  (纯解码，无模拟)                 |
        |                        v
        v                  WorldMapPageSource  <- 拾取与渲染共用同一页面契约
   WorldBootstrap                |
        |                        v
        |                  WorldMapPageStreamer (准入/CPU 常驻/有限待上传)
        v                        |
     World  <---- WorldContentBinder        v
  (可变模拟状态)      ^                VulkanWorldMap
                      |                (只做 atlas 分配、staging、barrier)
        DefinitionDatabase + EconomyDefinitions
        (只编译不可变作者定义，不知道 World 存在)
```

三条最容易踩坏的红线：

- `DefinitionDatabase` 不 include `World`、不创建模拟实体。把 script key 解析成
  运行期 ID、生成国家/建筑/POP、按日期落历史，**只有** `WorldContentBinder`
  能做（`src/thunder/content/definition/WorldContentBinder.hpp:16`）。
- `WorldPack` 拥有地理、拓扑、页面与 placement，不得变成第二个国家/经济数据库。
- `WorldMapPageStreamer` 拥有地图侧的有界 planner/cache；后端可以拥有 GPU 常驻，
  但不得重新实现 pack 解码或页面淘汰。

## 5. 目录归属与允许依赖

| 目录 | 拥有 | 允许依赖 |
|---|---|---|
| `thunder/foundation/base` `thunder/foundation/memory` `thunder/foundation/io` `thunder/foundation/jobs` | ID、hash、受限内存、文件访问、调度 | 标准库与更低层 Thunder |
| `thunder/content/worldpack` | `.thunderworld` 格式、索引、压缩、元数据 | base、io |
| `thunder/simulation/world` | 地理、邻接、placement、state-region 视图、拓扑解码、pack bootstrap | base、worldpack、economy 定义接口 |
| `thunder/simulation/economy` | goods / markets / buildings / POP stores 与经济 kernel | base、jobs；`.cpp` 内不得已的跨 store 操作才可及 World |
| `thunder/simulation/kernel` | 可变 `World` 聚合、clock、command、modifier、调度 | economy、world、gameplay 状态 |
| `thunder/content/definition` | VFS、面向 parser 的定义、localization 入口、显式 content binder | scripting；binder 可及 world/simulation |
| `thunder/scripting` | 解析、编译、字节码、VM、profiler | base、content 数据 |
| `thunder/simulation/gameplay` `thunder/simulation/ai` `thunder/simulation/research` `thunder/simulation/grand_strategy` `thunder/simulation/warfare` | 各自独立模拟域 | simulation 接口 + 自身域依赖 |
| `thunder/runtime/save` | 存档 codec、tagged section、legacy checksum 兼容 | simulation、economy、scripting |
| `thunder/presentation/render` | 后端无关的渲染数据、地图页面、plan、cache、相机 | base、worldpack、UI 契约；snapshot builder 是唯一模拟桥 |
| `thunder/presentation/render/vulkan` | Vulkan 句柄、命令录制、GPU 上传 | render 契约 + Vulkan |
| `thunder/presentation/ui` `thunder/runtime/editor` | 可复用 UI/编辑器机制 | render 契约、scripting、显式 world 服务 |
| `thunder/runtime/engine` | `ThunderEngine` 组合门面、内容安装 | 全部运行期域（顶层） |

## 6. 引擎模块具体拆分

引擎"看起来不一样"的来源是以下物理拆分，全部已核实存在（部分进入 `thunder_runtime`
的行见 `CMakeLists.txt`）。
放代码的依据是它拥有什么数据，而不是第一个用到它的功能。

**世界与地图**
- `world/WorldTopology.*` 拥有纯 `.thunderworld` 解码；`WorldBootstrap.*` 只把不可变
  记录组合成 `World`；`WorldBootstrapWire.*` 拥有二进制 wire 序列化。
- `world/WorldStaticLayers.*` 拥有静态层目录、建筑区域与资源分布表；渲染侧对应
  `render/map/WorldStaticGeometry.hpp` 与 `WorldStaticLayerSource.*` 按需解码河流/运输。
- `world/StateRegionIndex.hpp` 是只读的地理分组层，今天仍是 1:1 兼容映射。
- `economy/CountryStore.*` 拥有国家列与财政操作，`simulation/World.*` 只剩聚合
  checksum 与世界组装。
- `render/map/WorldMapPage.hpp`（解码后 payload）/`WorldMapPageSource.*`（唯一 CPU
  读取器）/`WorldMapPageStreamer.*`（准入与常驻）/`WorldMapPageKey.hpp`（身份）四分。
- `render/map/VectorMapPipeline.*` + `VectorMapTypography.*`：矢量边界、晕滃线、
  罗盘线与地图排版，作为引擎能力提供给使用方。

**渲染后端**（`VulkanDesktopBackend` 仍是门面，实现按职责落文件）
`VulkanDevice`（实例/设备/swapchain 图像/帧同步）、`VulkanFrame`（swapchain 重建、
帧命令、呈现）、`VulkanScene`（政治页面网格与 living 实例的场景通道）、
`VulkanPostProcess`（tonemap + FXAA）、`VulkanUi`（UI batch 与动态旗帜模块通道）、
`VulkanUiStaging`（draw-list 转换、字形兜底、buffer 增长）、`VulkanUiResources`
（字体度量与图像上传）、`VulkanLivingMap`（instance buffer staging）、
`VulkanRuntimeRenderer`（pipeline/descriptor 组装与 teardown）。
`ui/UiRenderTypes.hpp` 是 UI 到后端的最小契约——Vulkan 头文件不再按值持有
`FontAtlas`，也不再 include 整套 Strategy UI 实现。

**脚本与经济**
- `scripting`：`ScriptProgramDatabase`（编译产物存储）、`ScriptCompiler` 与
  `ScriptCompilerScoped`（作用域遍历与顶层程序编译）、`ScriptVm` 与
  `ScriptVmValues`（作用域执行与脚本值求值）、`ScriptProfiler` 各自独立；
  `ScriptProgram.cpp` 有意只保留为兼容翻译单元。
- `economy`：`EconomyPopulationPhases.cpp`（人口 gathers/就业/生产/消费）与
  `EconomyMarketPhases.cpp`（贸易/价格/结算/投资池/建造）从 `EconomySystem.cpp`
  拆出，后者只保留资产负债表、scratch/index 初始化与阶段编排。
- `grand_strategy`：`GrandStrategyWeekly.cpp`（周政治/外交/战争/军队/机构/科技/抵抗
  转移）与 `GrandStrategyValidation.cpp`（校验、checksum、内存计量）从 store 拆出。

**存档与 UI 运行时**
- `save`：`SaveGame.cpp` 为编解码与 wire 格式主入口，`SaveGameScriptSections`
  （tagged gameplay/AI/notification）、`SaveGameWorldSections`（市场/金融/地理/slot）、
  `SaveGameLegacy`（历史 checksum 兼容）、`SaveGameRuntime`（运行期 checksum 组合）。
- `ui`：`ScriptedGui`（schema/蓝本模型）/`ScriptedGuiCompiler`（解析到蓝本）/
  `ScriptedGuiValues`（呈现值）/`ScriptedGuiLayout`（节点尺寸与子布局）/
  `ScriptedGuiPainter`（语义绘制与 tooltip）/`ScriptedGuiRuntime`（retained tree）；
  `StrategyUiPrimitives`（几何与 batching）/`StrategyUiComponents`（主题面板与按钮）/
  `StrategyUiAnalytics`（进度、图表、经济专有绘制）/`StrategyUi`（装饰、控件、文本、命中）。
- `localization/LocalizationStore` 拥有查表、插值与分词，`ui/LocalizationRichText`
  是唯一面向 draw-list 的富文本适配器。
- `content/ModVersion.cpp` 拥有语义化版本解析与比较，`ModManifest.cpp` 只保留
  manifest 语法与 load plan 构造。

## 7. 一次新游戏启动的数据流

```text
外部 GIS / 作者化内容
  -> thunder_world_compiler -> 外部 .thunderworld
       -> WorldTopology（纯解码）
       -> WorldBootstrap -> World（可变模拟状态）
外部 ThunderScript 内容 -> VFS -> DefinitionDatabase / WorldContentBinder -> World
引擎工具 -> thunder_runtime 的只读 world、内容与资产接口
```

关键点：外部作者化内容不进入引擎源码；`.thunderworld` 是地图运行时的唯一权威来源，
而 `WorldTopology`、`WorldBootstrap` 和 `WorldContentBinder` 保持职责分离。

## 8. 已知未接通（不要当成已完成）

这些是解耦后暴露出的真实边界，写在这里避免文档比代码乐观：

1. **仓库不附带作者化世界包。** `thunder_world_compiler` 的 `--provinces` 是必填外部输入，
   运行时只能加载外部提供的 `.thunderworld`，世界包无法只靠引擎源码重建。
2. **矢量地图管线仍是引擎能力。** `VectorMapPipeline`、三档边界 payload 与
   `WorldMapPageStreamer` 已进入 `thunder_runtime`，具体游戏客户端接入由引擎使用方负责。
3. **`RenderSnapshot` 是定义但未采用的边界。** `RenderSnapshotData.hpp` 与
   `RenderSnapshotBuilder.cpp` 目前只被引擎工具使用；快照通道要成为真正的同步
   边界，需要先扩出 ownership/mode/living payload。
4. **`StateRegionIndex` 仍是 1:1 兼容层。** 德国式分裂州与州级经济查询需要
   world compiler 先产出共享 region key。
5. **市场拓扑缺一层。** market capital、港口/贸易中心与航线呈现模型尚未成型；
   经济目前主要按市场分区，单个巨型世界市场无法用满所有核心。
6. ~~财政内部表示已落地权威定点数。~~ 国家库藏以 `CountryStore::treasury_milli_`
   定点 milli-units 为权威；所有 double 写入路径（`create`/`set_treasury`/
   `add_treasury`）在边界量化到 milli，兼容视图始终严格等于 `treasury_milli_/scale`，
   不再独立积累浮点误差。剩余 double 热路径的契约见 `DETERMINISM.md` "Floating point"
   （含 configure 期 fast-math 拒绝与 `FpEnvironment` 运行期指纹）。
7. **当前 MSVC 验证缺口。** 引擎源码清理后，`src/thunder` 与 `src/apps` 的 MSVC
   编译状态需要重新复验。
8. **RenderGraph 为编译期 DAG 分析器而非运行时调度权威。** 当前桌面渲染器运行时由
   `VulkanFrame`（`src/thunder/presentation/render/vulkan/VulkanFrame.cpp`）作为唯一
   权威执行实体，显式记录指令缓冲并经 Synchronization2（`VkImageMemoryBarrier2`）插入
   资源屏障；`RenderGraph` 负责编译期 DAG 危害分析、拓扑排序与静态验证，目前不被
   `VulkanFrame` 直接消费。
9. **GPU Culling 现阶段为 CPU 参考实现（`CpuVisibilityPipeline` / `GpuCullingReference`）。**
   类名与实现已明确对齐其真实能力：目前在 CPU 完成视锥体（Frustum）与距离剔除、
   LOD 分级判定，并输出标准 `VkDrawIndexedIndirectCommand` 结构。基于 Compute Shader
   的 GPU-driven 剔除、Compaction 与 indirect-count 属于后续演进阶段，避免类名提前代表尚未接通的能力。

## 9. 参考模式与下一步拆分

Victoria 3 的公开 modding 材料把四件事分开，这是 Thunder 借鉴的**权威分离**，不是要
复制它的资产或私有实现：省份地图提供稳定地理身份；state region 聚合省份，政治州
在 region 内创建并可被多个所有者拆分；国家定义与 state/building/POP 历史写在内容
文件里而不是渲染器里；市场与贸易中心是地图之上的经济抽象，由港口、建筑与人口
影响而非直接画在地图上。
参考：[Dev Diary #60 – Modding](https://www.paradoxinteractive.com/games/victoria-3/news/victoria-3-dev-diary-60-modding)、
[Dev Diary #57 – The Journey So Far](https://www.paradoxinteractive.com/games/victoria-3/news/dev-diary-57-the-journey-so-far)、
[Dev Diary #38 – Trade Routes & Tariffs](https://www.paradoxinteractive.com/games/victoria-3/news/dev-diary-38-trade-routes-tariffs)。

按这个分离，下一批拆分的既定顺序是：

1. 引入显式 `StateRegionId` 成员关系，同时保留 `StateId` 表达政治 subdivision。这是
   德国式分裂州与州级经济查询的前置条件。
2. 在地理之上加市场拓扑层（`MarketCapital`、港口、贸易中心、运输可达性），由经济
   与地图 lens 查询，不嵌进省份渲染。
3. ~~继续拆 Vulkan 后端~~ 已完成（§6 渲染后端）；保持门面为短命 facade，直到出现
   更强的后端接口值得立。
4. 把 `RenderSnapshot` 扩成 map ownership / mode / living-instance payload，
   这是在真实 POP 数量下提高模拟吞吐所需的同步边界。

这些步骤的目标始终是"可复用的框架"：不把国家数值或事件内容搬进 C++，也不在外部
内容包到位之前重写可用的 `World` 聚合。

## 10. 完成定义

引擎框架完成的判据是全部四条同时成立：`.thunderworld` 读取、拓扑到 `World` 的组装、
外部 ThunderScript 内容绑定和存档/校验契约均保持稳定；工具只能通过公共 Thunder API
工作，不能把作者化数据写回引擎源码。

当前结论：**纯引擎框架，可供外部游戏项目接入，不包含可发布游戏内容。**

## 11. 文档索引

顶层：本文（边界与权威）、`FINAL_ENGINE_AUDIT.md`（验证状态，冲突时以此为准）、
`ROADMAP.md`（能力计划）。

领域契约（仍是各自子系统的权威，标题已去版本号）：
确定性 `DETERMINISM.md`；调度 `JOB_SYSTEM.md`；脚本 `THUNDER_SCRIPT.md`；
内容契约 `SCRIPT_FIRST_CONTENT.md`、`MOD_RUNTIME.md`；
逻辑域 `ECONOMY_ARCHITECTURE.md`、`FINANCE_BANKING.md`、`RESEARCH_ARCHITECTURE.md`、
`NOTIFICATION_RUNTIME.md`；
地图与渲染 `WORLD_COMPILER.md`、`POLITICAL_MAP_ARCHITECTURE.md`、
`LIVING_MAP_ARCHITECTURE.md`、`MAP_MODE_ARCHITECTURE.md`、`TERRAIN_ARCHITECTURE.md`、
`VULKAN_BACKEND.md`、`GPU_TIERS.md`；
UI `SCRIPTED_GUI.md`、`UI_THEME.md`；
性能 `PERFORMANCE_BUDGET.md`、`PERFORMANCE_DESIGN.md`。

同一主题只允许一个权威文档；新增总览类文档前，先看能否并入本文或对应领域契约。
