# 实施记录：牛皮纸删除 + 饱和平面政治图 + 国名排版调整

`audit/2026-09-05_flat_political_map_change.md`
日期：2026-09-05（20:48 决策，当晚实施）
决策依据：`audit/2026-09-05_map_view_and_parchment.md` §4 建议 A/C/B/D。
用户拍板：**牛皮纸直接删除、不保留**；远景=饱和平面政治图（地图风格）；
近景=3D 表现；国名保持嵌入式衬线、不加粗、半透明。

---

## 1. `shaders/world_map.frag`（live 地图着色器）

### 删除的牛皮纸元素
- **纸基底**（原 1195–1224 行）：`c_parchment_base`（未漂白亚麻）、
  `c_stain_tint`（茶渍大理石纹）、多倍频陈渍噪声、纤维素纤维/纸纹颗粒、
  地图折叠折痕、档案边缘晕影，及未使用的 `broad`/`medium` fbm。
- **铜版阴影线 hachure**（原 1533–1540 行）：19 世纪雕版山体刻线整块删除；
  保留中性 hillshade 并压平到 `0.92 + hill_shade * 0.16`（原 `0.88+0.24`）。
- **桃花心木书桌 + 黄铜画框**（原 §7）：替换为**印刷地图式 neatline**——
  墨色边带 `srgb(0.16,0.17,0.19)` + 单条浅灰规线 `srgb(0.45,0.48,0.52)`。
- **减法水彩上釉**：`watercolor_fill = aged_paper_lin * mix(1, country, 0.88)`
  整个纸底乘法链删除。

### 新远景（饱和平面政治图）
- 陆地 = **官方国家色直接平铺**（`far_land = country_lin`），不再被纸色软化。
- 去中心化地块 = 中性未殖民灰 `srgb(0.78,0.76,0.72)`（非纸派生）。
- 内边缎带 = 国家色加深 20%（`country_lin * 0.80`），机制不变。
- 沙漠釉：主权国 0.72 → **0.35**（政治色主导，仅留地形提示）；
  去中心化保持 0.92（无主之地显示地貌）。
- 经纬网：纸褐色 → 中性板岩 `srgb(0.42,0.47,0.54)`，强度 0.16 → 0.14。
- 国界墨线：`aged_paper_lin*vec3(0.16,0.14,0.12)` → `srgb(0.13,0.12,0.11)`。
- 远景海洋：青瓷/纸调 → **饱和海图蓝**（近岸 `srgb(0.40,0.61,0.76)` →
  深海 `srgb(0.18,0.35,0.52)`）；删除纸色混入（原 1246 行）；
  水线墨强度 **0.88 → 0.30**（保留淡海图感，服务"地图感"而非"纸感"）。
- 相机、`closeFactor`/`view_blend` 过渡、近景 PBR/3D 挤出全部未动
  （近景本来就是 3D 表现；无建筑网格前维持 PBR 地形形态）。

## 2. `PaperMapTransition` 模块整体删除（未接线的死代码）

- 删除 `src/thunder/presentation/render/map/PaperMapTransition.{hpp,cpp}`
  （9 参数纸样式配置 + 过渡求值器，此前仅 `(void)` 烟雾测试、从未喂 uniform）。
- 同步清理：`CMakeLists.txt` 源列表行、`VulkanDesktopBackend.hpp`
  的 include 与 `wired_paper_config_` 成员、`VulkanWorldMap.cpp` 的
  初始化烟雾测试块与每帧 `(void)compute_transition_factor` 调用。
- `terrain_3d_alpha`/`paper_ornament_alpha` 预留随之消失——将来近景 3D
  淡入直接用着色器现成的 `view_blend`/`closeFactor`，不再有双实现漂移。

## 3. 国名排版（嵌入式衬线、不加粗、半透明）

- `shaders/ui_msdf.frag`（`mapTextMode > 0.5` 分支，`map_label_pipeline_` 使用）：
  - 光晕色：暖羊皮纸乳白 `vec3(0.96,0.93,0.88)` → **中性冷调** `vec3(0.93,0.94,0.95)`；
  - 光晕贡献 0.45 → **0.38**，外扩 0.10 → 0.08（视觉更轻、不显粗）；
  - 墨体 coverage 机制不变（无人工加粗），`finalAlpha` 仍随 `color.a` 缩放。
- `src/thunder/presentation/render/map/VectorMapTypography.cpp`：
  国名墨色 `kCountryInkColor` alpha **0xf2（95%）→ 0xa8（约 66%）**，
  RGB 保持雕版深炭色。半透明基线 + 原有尺寸渐隐（<5.5px 淡入、>135px 淡出）
  叠乘，远景标签干净溶解。
- 说明：标签链路（worldpack LBL1 国名脊线 → `VectorMapTypography` 雕版布局 →
  `UiDrawList::map_text` → `map_label_pipeline_`）在引擎内**基础设施齐备**，
  但逐帧组合层在外部游戏宿主（`submit_ui` 本仓无调用方）——样式源头已按
  新方向改好，宿主接线后即按此渲染。

## 4. 明确不动的部分（范围界定）

- **UI 面板的羊皮纸材质**（`UiTheme` parchment_panel、木纹/黄铜按钮、
  `MapDecorationRenderer` 罗盘/晕影/cartouche）：属 UI 主题层而非地图，
  有测试覆盖（ui_theme_tests、grand_strategy_advanced_tests、
  vector_map_and_ui_tests）。若也要清理，建议另开一轮"UI 主题去纸化"。
- `terrain.frag`/`ocean.frag`/`political_overlay.frag`：无任何运行时加载方
  （孤立着色器文件），非 live 外观，未动。
- `VectorMapSystem::calculate_paper_map_blend`：矢量 overlay 半退役路径，
  仅测试引用，非 live 外观，未动。

## 5. 验证

- `glslc`（VulkanSDK 1.4.357.0）：`world_map.frag` / `world_map.vert` /
  `ui_msdf.frag` 全部编译通过（world_map.frag 仅原有 highp precision 提示）。
- `g++ -std=c++23 -fsyntax-only -Isrc`：`VectorMapTypography.cpp` 通过；
  `VulkanWorldMap.cpp`（含 `-Ithirdparty/vulkan-headers/include`）通过
  （仅原有 nodiscard 警告，与本改动无关）。
- 残留检索：源码/CMake/测试中 `PaperMap*`、`aged_paper`、`parchment` 均
  零命中（仅 audit 文档保留历史记录）。
- 本机 cmake 仍不可用（退出 127），27 套件 ctest 门禁由 CI 承担。

## 6. 后续（与既有审计衔接）

- 近景 3D：网格导入器 + BuildingInstancer（见 2026-09-05 近景 3D 审计）；
  淡入直接复用着色器 `view_blend`，无需重建过渡模块。
- UI 主题去纸化（可选，用户未要求）。
- 远景国色对比若需微调，饱和度杠杆在 `SATURATION = 1.08` 与调色板本身。
