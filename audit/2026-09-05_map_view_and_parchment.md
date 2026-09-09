# 地图视角划分与远景牛皮纸风格审计

`audit/2026-09-05_map_view_and_parchment.md`
日期：2026-09-05
前置：本审计建立在 `2026-09-05_map_rendering_vs_clausewitz.md`（Phase B）与
`2026-09-05_map_layer_architecture.md` 之上。

> **[2026-09-05 晚已执行]** 用户拍板：牛皮纸直接删除（不保留）、远景=饱和
> 平面政治图、国名衬线半透明。实施记录见
> `audit/2026-09-05_flat_political_map_change.md`——§4 建议 A/C 已落地
> （A 以"直接删除"替代"复古开关"），B 随纸基移除自然收敛，D 已落地
> （国色直铺）。下文为决策前的审计原文。

---

## §0 结论（先看这里）

1. **当前地图视角不是"远景 / 3D"两种离散模式，而是"一个连续缩放 + 着色器内部风格跳变"。**
   相机 `StrategicCamera` 是纯连续的海拔缩放（无任何离散模式）；"远↔近"的视觉区分
   完全发生在着色器里，由 `closeFactor`（海拔 70k–1.2M）→ `view_blend`
   （smoothstep 0.18–0.75）驱动：远景 = 水彩牛皮纸，近景 = PBR 照片。

2. **远景牛皮纸确实"过度强调纸感、牺牲了地图/游戏感"。** 在满远景
   （`view_blend = 0`）时陆地是**纯牛皮纸**（地形 PBR 完全不参与），国家色被
   "减法水彩"乘以纸底（0.88），国家对比被纸色软化——而这恰恰是战略地图最该做好的事：
   一眼看清谁占哪。牛皮纸是一个"自创语汇"，EU4/CK3/HoI4/V3 的远景都是饱和平面政治图，
   没有"纸↔照片"的硬跳变。

3. **需要调整。** 建议把牛皮纸做成可关闭的"复古皮肤"（默认关），默认回归 PDX 式
   饱和平面政治图；让远↔近过渡在"风格上连续"（细节随缩放增长，观感恒定），
   而非"纸变照片"；并把已设计但没接线的 `PaperMapTransition` 真正接上
   （它预留的 `terrain_3d_alpha` / `paper_ornament_alpha` 正是 V3 式
   "拉近时 3D 建筑/树木淡入" 的行为，目前是死代码）。

---

## §1 当前地图视角是怎么划分的

### 1.1 相机层：连续缩放，无离散模式

`src/thunder/presentation/render/StrategicCamera.cpp`：

- `clamp()`（约 77–83 行）把 `pitch_deg` 钉在 `[25, 88]`、
  `vertical_fov_deg` 钉在 `[25, 80]`，海拔夹在 `[min_altitude_m, max_altitude_m]`。
- `zoom_fraction()` 是 `min/max` 海拔之间的对数刻度（见约 71–75 行）。
- `ground_meters_per_pixel()` 由 `altitude_m × vertical_fov_deg` 推出。

→ 相机本身是**连续海拔缩放**，**没有** "far view" / "3D" 两套离散状态。
"远和 3D" 的区分不在相机，而在着色器。

### 1.2 着色器层：closeFactor → view_blend 的"风格跳变"

远景/近景的视觉 duality 全部由以下两条线决定：

`shaders/world_map.vert`（约 414–415 行）：

```glsl
float altitude = max(camera.x, 350.0);
closeFactor = 1.0 - smoothstep(70000.0, 1200000.0, altitude);
```

- 近景（低海拔）→ `closeFactor ≈ 1`
- 远景（高海拔）→ `closeFactor ≈ 0`

`shaders/world_map.frag`（1401 行）：

```glsl
float view_blend = smoothstep(0.18, 0.75, closeFactor);
```

`shaders/world_map.frag`（1542–1548 行）：

```glsl
vec3 near_land = pbr_terrain;                       // 近景：照片级 PBR
vec3 land_colour = mix(far_land, near_land, view_blend);
```

- `view_blend ≈ 1`（近）→ `near_land` = 照片级 PBR 地形
- `view_blend ≈ 0`（远）→ `far_land` = 水彩牛皮纸

→ 这是一个**二元风格跳变**（水彩纸 ↔ 照片），不是连续风格。

### 1.3 远景"纸"到底占多大比重——几乎 100%

`far_land` 完全由纸基构成，**不含任何地形 PBR**：

- 纸基 `aged_paper_lin`：`world_map.frag` 1199–1224 行
  - `c_parchment_base = vec3(0.915, 0.868, 0.782)` 暖色未漂白亚麻
  - 氧化茶/咖啡渍 `c_stain_tint`、纤维素纤维、折叠折痕、档案边缘晕影
- 国家色以"减法水彩"乘进纸底（1479 行）：
  `watercolor_fill = aged_paper_lin * mix(vec3(1.0), country_lin, 0.88)`
  → 国家色被纸色软化，**远景国家对比度下降**。
- 其余 `far_land` 元素（缎带、去中心化淡象牙、沙漠洗、铜版阴影线 hillshade、
  经纬网 grat_uv、桃花心木/黄铜画框）**全部以 `aged_paper_lin` 为基底**（1467–1614 行）。

而 PBR 在远景**根本不计算**：`world_map.frag` 1404 行
`if (view_blend > 0.001 || ...)` 才进入 `calculate_pbr_terrain_splat`。
→ 满远景时陆地 = 纯牛皮纸 + 水彩国名 + 铁胆墨水边界 + 画框 + 晕影。
地形 PBR 在远景完全消失。

### 1.4 一个设计好却没接线的平行实现（重要债）

`src/thunder/presentation/render/map/PaperMapTransition.{hpp,cpp}` **已经设计好**
一套更干净的过渡：

- `compute_transition_factor(altitude, start_fade=150000, full_paper=1000000)`
  → 干净的 smoothstep（150k–1M）。
- `MapTransitionOutput` 预留：
  - `terrain_3d_alpha = 1.0 - t`（3D 网格/树木/城市的淡出）
  - `paper_ornament_alpha = t`（罗盘、画框、复古 cartouche 的淡入）
  - `border_ink_width` 随缩放变化
- `PaperMapStyleConfig` 有 9 个可调纸参数（底色/渍色/墨水/海色/阴影线强度/折痕/晕影/纤维）。

但它在 `VulkanWorldMap.cpp` 里**只被 `(void)` 烟雾测试**，从未把输出作为
uniform 喂给着色器：

- `VulkanWorldMap.cpp` 124–128 行：`wired_paper_config_ = ...; (void)compute_transition_factor(...);`
  `(void)evaluate_copperplate_hachure(...);` …… 全部 `(void)`。
- `VulkanWorldMap.cpp` 161 行：`(void)PaperMapTransitionEvaluator::compute_transition_factor(...)`。

→ 着色器**完全忽略** `PaperMapTransition`，自己用硬编码常量
（`c_parchment_base`、`c_stain_tint` 字面量）和自己的阈值（70k–1.2M / 0.18–0.75）。
这是与 `PoliticalMapRenderPlan`（Phase B 的 R6 债）**完全相同的"双实现漂移"模式**。
而且注意：即便接上，`PaperMapTransition` 的 150k–1M 阈值与着色器 70k–1.2M 还**对不齐**，
需要先统一。

→ 那个本该实现"拉近时 3D 淡入、远景装饰淡出"的 `terrain_3d_alpha` /
`paper_ornament_alpha` 预留，**目前是死代码**，没有任何视觉效果。

---

## §2 维多利亚 3 是怎么划分的

V3 的地图观感由**单一、恒定的美术方向**贯穿所有缩放层级：

- **远景**：一张**饱和的平面政治图**（画家风、略带立体倾斜），不是牛皮纸。
  国家用 wiki 官方色，对比清晰、一眼可辨。
- **近景**：同一套观感 + **3D 建筑模型在拉近时淡入**，地形法线/纹理细节随缩放增强。
- **关键**：**风格恒定，细节随缩放增长**——没有"纸↔照片"的硬跳变。
  V3 的招牌是"近景精致的 3D 建筑"（正是上一问讨论的对象），**不是**"远景牛皮纸"。
  远景在 V3 里就是一张好看的政治图而已。

对照 Thunder 现状：

| 维度 | Thunder 现状 | 维多利亚 3 |
|---|---|---|
| 远景身份 | 水彩牛皮纸（纸感主导） | 饱和平面政治图（地图感主导） |
| 远↔近过渡 | 纸↔照片二元跳变 | 风格连续、细节渐增 |
| 3D 元素 | 仅 billboard 军队/舰队；建筑无 3D；`terrain_3d_alpha` 预留但死 | 近景 3D 建筑淡入，是核心卖点 |
| 国家可读性 | 远景被纸色软化（水彩 0.88 乘入） | 远景饱和、对比强、一眼可读 |
| 风格一致性 | 缩放不同像"两个游戏" | 缩放不同仍是"同一个游戏" |

---

## §3 牛皮纸是否过度强调"纸"而非"地图/游戏感"？——是

证据与理由：

1. **它是二元风格跳变，不是连续观感。** 满远景陆地是纯纸（§1.3），满近景是纯照片。
   这是自创语汇，没有任何已发售 PDX 作品采用；缩放时像"换了款游戏"。
2. **纸身份被严重过度建设。** `PaperMapStyleConfig` 9 个硬编码纸参数 +
   整套 `PaperMapTransition` 模块 + 专门的渍/纤维/折痕/晕影/铜版阴影线/桃花心木画框/
   经纬网代码。投在"看起来像古地图"上的美术方向精力，远多于投在"棋盘可读性"
   （省界、国家对比、地形可辨性）上。
3. **偏离品类身份。** EU4/CK3/HoI4/V3 远景都是饱和平面政治图（V3 画家风但仍是
   平面且饱和，非牛皮纸）。"未漂白亚麻 + 茶渍 + 铁胆墨水 + 桃花心木画框"是 cosplay，
   不是战略游戏地图——它与"读图"竞争，而非辅助读图。
4. **国家色被纸色软化。** `watercolor_fill = aged_paper_lin * mix(1, country, 0.88)`
   把国家色乘进纸底，远景国家对比度下降；而这恰恰是战略地图最重要的功能。
5. **技术上还背着债。** 牛皮纸样式硬编码在 `world_map.frag` 字面量里，而一个本可集中
   调参的 `PaperMapStyleConfig` 却没接线——你连统一调它都做不到。

---

## §4 是否需要调整？——需要（具体建议）

与 Phase B 的"调整 A：收敛远/近跳变"一致，给出可落地步骤：

**A. 把牛皮纸降级为可关闭的"复古皮肤"（默认关）。**
- 在 `PaperMapStyleConfig` / 渲染设置里加 `paper_mode ∈ {off, retro}`。
- 默认 `off`：远景回归 PDX 式**饱和平面政治图**（国家色直接平铺 + 轻量画家风洗色，
  **不以纸为底**）。`paper_mode = retro` 时才走现有水彩牛皮纸路径。

**B. 让远↔近"风格连续、细节渐增"，去掉纸↔照片跳变。**
- 远景也保留地形可读性与饱和国色；牛皮纸只作为 `retro` 模式下的叠加洗色，而非基底。
- 这与 V3 一致，消除"两个游戏"的观感割裂。

**C. 真正接上（或删除）`PaperMapTransition`。**
- 若保留：让它成为**唯一的过渡因子来源**，把 `terrain_3d_alpha` /
  `paper_ornament_alpha` 作为 uniform 喂给着色器，实现"拉近时 3D 网格/树木/建筑淡入、
  远景罗盘/画框淡出"——这正是 V3 式近景 3D 的行为，且预留已写好只是没接线。
- 先统一阈值：把着色器 70k–1.2M 与 `PaperMapTransition` 150k–1M 对齐到同一组常量。
- 若判定为无价值，则删除该模块与 `VulkanWorldMap.cpp` 的 `(void)` 烟雾测试，避免误导。

**D. 提升远景国家可读性（无论哪种模式）。**
- 远景国家色用**饱和直铺 + 轻量洗色**，不再把国色乘进纸底；保留省/国界高对比墨线。

优先级：**A/C 高（解决"过度强调纸"与死代码债），B 高（风格一致性），D 中（可读性）。**

---

## §5 证据索引（file:line）

- 相机连续缩放、无离散模式：`StrategicCamera.cpp` clamp(77–83)、zoom_fraction(71–75)
- 着色器风格跳变：`world_map.vert:414-415`（closeFactor 70k–1.2M）、
  `world_map.frag:1401`（view_blend 0.18–0.75）、`world_map.frag:1548`（mix 合成）
- 远景纯纸基：`world_map.frag:1199-1224`（aged_paper）、
  `1479`（watercolor 0.88 乘入）、`1467-1541`（far_land 全以纸为底）、
  `1404`（远景不进 PBR）
- 远景装饰（画框/经纬网/晕影，远景专属）：`1556-1565`（graticule）、`1593-1614`（mahogany/brass frame）
- 平行未接线实现：`PaperMapTransition.hpp:40-41`（150k–1M）、
  `PaperMapTransition.hpp:27-33`（`terrain_3d_alpha`/`paper_ornament_alpha` 预留）、
  `VulkanWorldMap.cpp:124-128,161`（全部 `(void)` 烟雾测试，未喂 uniform）
- 品类对照结论：EU4/CK3/HoI4/V3 远景均为饱和平面政治图（品类共识，非本仓库代码）

---

## §6 与既有审计的关联

- 与 Phase B（`2026-09-05_map_rendering_vs_clausewitz.md`）的"调整 A：收敛远/近跳变"
  **完全一致**，本报告给出更具体的落地步骤与死代码定位。
- 与 "R6 双政治路径漂移" 同源：**又一处"设计好却没接线的平行实现"**
  （`PaperMapTransition` vs `world_map.frag` 自管阈值）。建议与 R6 一起做"接线或删除"清理，
  避免仓库里继续积累这类"看起来有、实际没用"的模块。
- 与上一问"V3 近景 3D 建筑"衔接：本报告 §1.4 指出 `terrain_3d_alpha` 预留正是近景 3D
  淡入的接线点——近景 3D 资产的落地，应先从这里接线，而非另写一套过渡逻辑。
