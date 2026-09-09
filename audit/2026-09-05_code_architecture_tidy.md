# 代码架构整理审计：同功能重复实现与死代码清理

`audit/2026-09-05_code_architecture_tidy.md`
日期：2026-09-05 深夜
方法：着色器加载清单 vs 磁盘清单比对、25 个疑似类的全仓消费矩阵扫描
（`grep -rl <类名> src/thunder`，排除自身文件）、帧路径调用链追踪、
telemetry 字符串外部引用检查。验证：g++ -fsyntax-only（含 SDL3 临时解包头）。

---

## §1 本轮已执行（Phase 1，编译验证通过）

1. **删除 3 个孤儿着色器**（运行时零加载、全仓零引用）：
   `shaders/terrain.frag`(213 行)、`shaders/ocean.frag`(53)、
   `shaders/political_overlay.frag`(64)。运行时实际加载的着色器仅
   13 个（VulkanRuntimeRenderer.cpp:268-287）。
2. **`decode_utf8` 双实现去重**：`FontAtlas.cpp` 与 `VulkanUiStaging.cpp`
   携带逐字节同算法的拷贝 → 收敛到新头文件
   `src/thunder/presentation/ui/Utf8Decode.hpp`（取带越界保护的版本）。
3. **删除每帧空转的 `update_wired_terrain_state`**（函数 + 声明 + 帧路径调用块）：
   该函数每帧构造相机状态并计算天空环境/云影/clipmap 构建/森林/河流/水面高光，
   **除一行诊断日志外全部 `(void)` 丢弃**（太阳方向还是常量）。每帧成本归零。
4. **裁剪 `ensure_wired_terrain_pipelines`**：删除 5 个"证明接线"烟雾块
   （Gerstner 水面/森林风/河流急流/clipmap 构建/固定坐标演示粒子/边界 ribbon），
   保留一次性 bindless 生物群系注册与天空常量初始化（诊断 dump 消费）。
5. **清理诊断假信号**：`wired_forest_wind_ok=1`、`wired_river_ok=1`、
   `wired_physical_water_ok=1`、`wired_living_vfx_ok=(x>0?1:1)`（恒真式）、
   `wired_border_mesh_ok=1`、`wired_paper_transition_ok=1`、
   `terrain_parity_fixed=1` 共 7 行硬编码常量删除。

---

## §2 消费矩阵（Phase 1 后现状，证据可复现）

| 类 | src/thunder 内消费者 | tests/bench | 状态判定 |
|---|---|---|---|
| `PoliticalMapRenderPlan` | **0** | thunder_tests | 引擎死代码（R6 遗留 overlay） |
| `PoliticalMapStreamingPlanner` | **0** | — | 引擎死代码 |
| `TerrainStreamingPlanner` | **0** | — | 引擎死代码 |
| `TerrainRenderPlan` | **0** | — | 引擎死代码 |
| `MapModeStore` | **0** | — | 引擎死代码 |
| `WorldMapPicker` | **0** | — | 引擎死代码 |
| `MapDecorationRenderer` | **0** | grand_strategy_advanced | 引擎死代码（木框/罗盘/cartouche 装饰） |
| `WorldStaticLayerSource` | **0**（待二次确认） | — | 疑似死代码 |
| `VectorMapSystem`(VectorMapPipeline) | 仅 VectorMapTypography.hpp（用 `VectorPoint` 类型） | vector_map_and_ui | 类型被活代码引用，类本体死 |
| `TerrainPageCache` | 4 处（疑似互相引用） | — | 需传递性分析 |
| `ProvincePickingCache` | 2 处（同上） | — | 需传递性分析 |
| `TerrainMaterialShading` | init 注册 1 处 | thunder_tests | 活（bindless 注册） |
| `PhysicalWaterPass/PhysicalWaterEvaluator` | 0（Phase 1 后） | thunder_tests/bench | 拆除候选 |
| `ForestCanopyInstancer` | 0（Phase 1 后） | bench | 拆除候选（近景树接线时应重建而非保留旧件） |
| `RiverSplineFlowPass` | 0（Phase 1 后） | — | 拆除候选 |
| `VolumetricAtmosphereAndClouds` | init 常量 1 处 | thunder_tests | 保留最小面（或内联常量后删） |
| `BorderMesh3D` | 0（Phase 1 后） | — | 拆除候选 |
| `LivingMapVfx3D` | 0（Phase 1 后，粒子从未进渲染命令） | — | 拆除候选 |
| `TerrainClipmap` | 仅 `max_patch_count()` 诊断 | — | 拆除候选 |

活代码（勿动）：`WorldMapPageStreamer/Source`、`WorldResidentLayout`、`CoastDistancePage`、
`PoliticalMapState`、`PoliticalMapPageBundle`、`VectorMapTypography`、`WorldMapPicker` 之外的
map/ 页面族、`world_map.vert/frag`、UI 族、`FontAtlas`。

---

## §3 Phase 2 清单（待批准后执行；涉及删测试，需 CI 门禁验证）

原则：**只删引擎内零消费者的**；VectorMapTypography 是活国名管线，保留；
`VectorMapPipeline.hpp` 中的 `VectorPoint` 等类型先抽到独立小头再删类本体。

1. 删除引擎死代码 8 个类（.hpp/.cpp + CMake 源行 + 对应测试段落）：
   PoliticalMapRenderPlan / PoliticalMapStreamingPlanner / TerrainStreamingPlanner /
   TerrainRenderPlan / MapModeStore / WorldMapPicker / MapDecorationRenderer /
   WorldStaticLayerSource（执行前二次确认其 0 消费）。
2. `VectorMapPipeline`：抽出 `VectorPoint`/几何类型 → 删 `VectorMapSystem` 类本体
   （657 行）；`VectorMapTypography` 改包含新类型头。
3. 拆除"wired 平价家族"（PhysicalWaterPass、RiverSplineFlowPass、BorderMesh3D、
   LivingMapVfx3D、TerrainClipmap；ForestCanopyInstancer 视近景树计划决定）：
   删文件 + CMake + thunder_tests/bench 对应段 + VulkanDesktopBackend 成员
   （`terrain_clipmap_`、`wired_living_vfx_`）与遥测行。
4. 传递性死代码确认后删：TerrainPageCache / ProvincePickingCache。
5. 预计净删 **~4,500 行**（不含测试段），presentation 占比从 37% 显著下降。

风险与门禁：本机 cmake 不可用，无法本地跑 27 套件；Phase 2 必须在 CI ctest 通过后合入。
建议拆两个 PR：①死类删除 ②平价家族拆除。

---

## §4 其他记录

- 着色器编译脚本按目录通配，删除孤儿着色器同时减少构建/校验时间。
- `VulkanWorldMap.cpp:33` 陈旧 `"destiny --shading-debug"` 字符串与
  `world_map.frag:33-37` 同款注释、2048 legacy shadow-march 路径：
  留待视觉签收后随 Phase 2 一并清理。
- 本次为语法级验证（g++ -fsyntax-only + glslc），语义回归依赖 CI ctest。
