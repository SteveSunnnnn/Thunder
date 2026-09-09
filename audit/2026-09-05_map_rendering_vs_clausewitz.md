# Thunder 地图渲染模式与架构 vs Paradox Clausewitz 引擎对比分析

> 审计日期：2026-09-05
> 范围：渲染管线 / 地形分层 / 地图投影 / 国界与省份着色 / 纹理与贴图处理
> 对标对象：Paradox Development Studio 的 Clausewitz / jomini 家族
> （欧陆风云 IV、十字军之王 III、维多利亚 3、钢铁雄心 IV）
> 配套文档：`audit/2026-09-05_map_layer_architecture.md`（地图分层架构拆解）

---

## 0. 结论摘要

Thunder 当前的世界地图渲染是一条**单一连续材质（one continuous material）**的
实例化补丁网格管线：顶点阶段用常驻金字塔（resident pyramid）做高度位移与斜视投影，
片元阶段在一个 uber‑shader 内完成地形 PBR 飞溅、海洋、国界、省份水彩着色、羊皮纸
底纹的全部分量，并随相机高度在「远距 2D 羊皮纸水彩」与「近距 3D 照片级 PBR」之间
交叉淡入。

从架构哲学看，Thunder 与 Clausewitz **在省份身份模型上高度同构**——都是
「省份 ID 栅格 + 每省份调色板查表 + 边界线」的三段式。事实上 `world_map.frag` 的
注释里反复出现 "Victoria 3 style / Victoria 3 wiki colors / 19th‑century
copperplate"，说明团队**已经在刻意对齐 V3 的视觉气质**。

真正拉开差距的不是大方向，而是三处分歧：

1. **管线形态**：Thunder 用「一个 uber‑shader 全包 + 远/近双模跳变」；Clausewitz 是
   「多通道、base→terrain→border→river→overlay 叠加、外观高度稳定」。Thunder 的
   远/近跳变（水彩纸 ↔ 照片 PBR）是 PDX 没有的强风格化，偏离了它们「细节渐进、
   身份恒定」的观感。
2. **地形与生态的数据来源**：Thunder 把山脉、沙漠、植被带**硬编码成经纬度地理带**
   （`alpine_mountain_relief` 里 14 条造山带、`subtropical_arid` 沙漠带）；Clausewitz
   的地形/生态是**内容驱动**的省份属性（plains/hills/mountains + 高度图 + 生物群系），
   由美术与剧本定义，不从经纬度算。
3. **贴图来源**：Thunder 几乎**全程序化**，没有美术贴图集；Clausewitz 的「P 社味」
   绝大部分来自**手绘地形贴图、3D 建筑/树木/单位模型、河流/道路矢量美术、统一调色
   与光照导演**。这是视觉质感差距的最大单一来源。

下面分五个维度逐项对比，并给出逼近 PDX 风格的关键调整（每项含目标 / 方向 / 优先级）。

---

## 1. 渲染管线（Rendering Pipeline）

### 1.1 Thunder 现状（证据）

- 地图通道是一条**连续材质**：`world_map.frag:3-8` 明确写道 "The map pass is one
  continuous material. The resident pyramids supply stable categorical geography;
  all paper, pigment, terrain splatting and water variation is sampled in world‑UV
  space"。
- 顶点阶段：实例化补丁网格，`world_map.vert:3-7` 说明每个 SSBO 元素是一块补丁，
  在顶点阶段做曲面细分，常驻金字塔承载近距 3D 起伏、远距安静的纸面；`GRID = 32`
  （`world_map.vert:58`）的 32×32 顶点网格提供平滑起伏；`world_map.vert:421-439`
  做连续斜视投影与高度位移。
- 片元阶段单通道内顺序合成（`world_map.frag` 的 `main()`，1177-1628）：
  ① 羊皮纸底（1199-1224）→ ② 海洋双模（1227-1304）→ ③ 地形 PBR 飞溅（1307-1548）
  → ④ 政治水彩 + 双模混合（1460-1548）→ ⑤ 海/陆合成（1550-1553）→ ⑥ 经纬网/国界/
  海岸（1556-1591）→ ⑦ 画框/大气雾霾/饱和度（1594-1627）。
- 另存在一条 **`PoliticalMapRenderPlan`**（`PoliticalMapRenderPlan.cpp:9-38`）：
  `political_map_upload` 传输通道 + `political_map_overlay` 图形通道，输入
  `terrain_depth`、输出 `terrain_hdr`，资源含 `province_id_cache`、`coast_distance_cache`、
  `province_records`、`country_colors`、`map_mode_words`、`vector_borders_vbo/ibo`、
  `parchment_texture`。**但这些资源并未被 `world_map.frag` 读取**——该 shader 只读
  `politicalPalette`(set0/b0)、`province_id_tex`(set1/b1)、`coast_sdf_tex`、
  `height_pyramid`。即存在**两套政治着色路径**，其中一套（overlay + 矢量边界 + 调色板
  缓冲）处于半退休/遗留状态，与架构审计报告 R6 一致。
- 还有独立的 `terrain.frag`（全屏，49-213）做「近距 PBR / 远距档案纸」双模，
  以及 `political_overlay.frag` 自述 "Purely synthetic — a stand‑in until a real
  world pack supplies province colors"（7-11），是占位原型。

### 1.2 Clausewitz 现状（行业公认）

- 2D 标题（EU4 / HoI4）：地图本质是**一张平铺的省份 ID 大纹理**，像素即省份；渲染为
  多层叠加：省份底色 → 地形/山纹 → 边界线 → 河流 → 单位/建筑精灵 → 各种地图模式重着色。
- 3D 倾斜标题（CK3 / V3）：在省份地图之上叠加**高度网格 + 地形纹理飞溅**，相机可斜视，
  但省份着色身份始终保持稳定，不因缩放而从「水彩纸」跳到「照片 PBR」。
- 关键特征：**多通道、各司其职、外观连续**。省份身份 = 颜色查表，恒定；缩放只增减
  细节密度，不改变「这是什么游戏」的观感。

### 1.3 差异分析

| 维度 | Thunder | Clausewitz |
|---|---|---|
| 通道数 | 单一 uber‑shader 全包（外加一套半退休 overlay） | 多通道分层叠加 |
| 缩放观感 | 远/近**双模跳变**（水彩 ↔ PBR） | 细节渐进、身份恒定 |
| 政治着色路径 | 两套（live inline + legacy overlay） | 单一连贯省份管线 |
| 高度/地形 | 程序化 fbm + 金字塔位移 | 内容/高度图驱动网格 + 飞溅 |

**结论**：管线形态上 Thunder 更接近 CK3/V3 的 3D 倾斜地图（这是正确的赛道），但
「uber‑shader 跳变 + 双政治路径」是两处需收敛的架构债。

---

## 2. 地形分层（Terrain Layering）

### 2.1 Thunder 现状

- 高度来自常驻金字塔：`sample_height01`/`sample_height_m`（`world_map.vert:96-109`、
  `world_map.frag:96-?`）按 `kWorldLevels`{40,28}/{20,14}/{10,7}/{5,4} 取四层金字塔。
- 真实海拔之上叠加**程序化 fbm 起伏**：`realistic_alpine_fbm`（`world_map.vert:195-267`）
  做山脊/河谷侵蚀；`terrainHeight`（`world_map.vert:380-392`）将烘焙高度与程序化起伏混合。
- **山脉是硬编码地理带**：`alpine_mountain_relief`（`world_map.vert:269-375`）以 14 条
  造山带（阿尔卑斯、比利牛斯、喀尔巴阡、喜马拉雅、落基、安第斯、华东丘陵、山东、撒哈拉
  高地、阿特拉斯、乌拉尔、阿尔泰…）的经纬度距离场决定「哪里是山」。
- **生态/干旱带也是硬编码**：`subtropical_arid`（`world_map.frag:482-489`、714-716）、
  撒哈拉/阿拉伯/戈壁/塔克拉玛干/澳洲/卡拉哈里/北美/阿塔卡马八条沙漠带
  （`world_map.frag:1493-1511`）用经纬度算。
- 材质飞溅是**程序化多层 PBR**：`calculate_pbr_terrain_splat`
  （`world_map.frag:687-`）、`terrain.frag:60-122` 按 height/slope/moisture 在
  沙滩/草原/森林/泥土/岩石/碎石/雪/冰之间混合，含季节雪偏移。

### 2.2 Clausewitz 现状

- 地形是**内容属性**：每个省份/地区有一个 terrain 类型（plains / hills / mountains /
  coast / marsh / forest …），由剧本与美术定义。
- 3D 标题用**高度图网格 + 手绘地形纹理飞溅**，生物群系来自数据而非经纬度。
- 山不是「按经纬度画出来的」，而是高度图与地形类型共同决定；美术可逐格雕刻。

### 2.3 差异分析

Thunder 的 PBR 飞溅（height/slope/moisture → 多材质）与 CK3/V3 的地形纹理飞溅**思路
一致且现代**；核心偏差是**山脉与生态的来源**——硬编码经纬度带既不可移植（换一张世界
就错位），也剥夺了内容团队的雕刻权。项目里 `GeographyStore.hpp` 已存在 EU5 式
Climate×Topography×Vegetation 三层模型，说明正确的数据底座已在，只是 shader 还没接上。

---

## 3. 地图投影（Map Projection）

### 3.1 Thunder 现状

- 投影是**等距圆柱（equirectangular）UV 展开 + 垂直位移**：`world_map.vert:421-439`
  把补丁 UV 当平面 NDC，按相机俯仰做斜视，`gl_Position.y` 减去 `elevation_h * tilt`，
  深度随山峰升高而减小——即「斜贴的纸面 + 垂直挤出」，并非球体。
- 经度**连续展开**（u 不 wrap），由 streamer 跨视界拼接，反子午线用 shortest‑arc /
  `fract` 在插值后处理（`world_map.vert:402-407`、`sample_coast_raw` 的
  `mod(...,px.x)` 环绕）。
- 经纬度可逆：`latitude_degrees`（`world_map.frag:681-684`）用反 Mercator 公式，
  但渲染面本身不弯曲。

### 3.2 Clausewitz 现状

- 同样以**等距圆柱 2D 平图**为主（圆柱包裹、左右环绕），新版标题做倾斜但**不塑成
  真实球体**。与 Thunder 的「平 UV + 挤出」几乎同构。

### 3.3 差异分析

投影层面 Thunder 与 PDX **基本一致**（都是扁平等距圆柱 + 环绕，而非真球）。唯一可改进
点是**环绕接缝观感**：`world_map.frag:1561-1563` 为经纬网在接缝处做了软化遮罩，说明
接缝仍有可见风险；PDX 的环绕更无感。优先级低。

---

## 4. 国界与省份着色（Borders & Province Coloring）

### 4.1 Thunder 现状（与 Clausewitz 最同构的部分）

- **省份身份 = ID 栅格 + 调色板查表**：`political_colour`（`world_map.frag:208-215`）
  按 `provinceId` 在 `politicalPalette` 上 `texelFetch` 取 RGBA——与 Clausewitz 的
  「省份 ID 图 → 颜色查表」完全同构。
- **国界 = 栅格等值面 + 子像素 AA**：`sovereign_border_smooth`（`world_map.frag:583-676`）
  取 2×2 角省份、用连续成员场 `field` 在 `0.5` 处做抗锯齿过渡，输出 `stroke`（1.8 px）
  与 `inner_ribbon`（16 px 水彩内辉光），着色用铁胆墨水色
  （`world_map.frag:1576`）。明确写 "Smooths 8km staircase steps into natural
  curving boundaries"。
- **水彩上釉 + 羊皮纸**：`world_map.frag:1460-1548` 用 `aged_paper_lin * country_lin`
  做减色水彩釉，内辉光带、沙漠釉、铜版山纹（hachure）一应俱全；去中心部落用淡象牙白
  （`is_decentralized`，1473-1484）。
- **地图模式**：`PoliticalMapRenderPlan` 含 `map_mode_words` 资源，调色由 `politicalPalette`
  与 word 叠加实现（具体重着色逻辑在另一通道，live 路径暂未接入）。

### 4.2 Clausewitz 现状

- 省份 ID 图 → 颜色查表；边界为线（2D 标题直接画在省份色图之上，3D 标题屏幕空间矢量）；
  地图模式即重着色省份；EU4/CK3 省份填充**饱和、平实、身份强**；V3 偏水彩但依旧稳定。

### 4.3 差异分析

**这是 Thunder 与 Clausewitz 最契合的维度**，概念几乎一一对应。差距在「成品抛光度」：

- PDX 边界权重**一致、干净、可强调**；Thunder 的 `inner_ribbon` 水彩内辉光 + 铁胆墨水
  是**自创风格**，且 `loc_stroke` 恒为 0（`world_map.frag:1578-1579`，注释说
  `*_border_smooth 已随 atlas 删除`，678-679），即**次级行政区边界被整体砍掉**——
  PDX 中 state/次级边界是与省份边界区分绘制的。
- 省份悬停/选中/高亮的交互层缺失（PDX 的核心体验）。
- 远距「羊皮纸水彩」偏离 EU4/CK3 那种饱和平实填充；若要贴近 PDX，应提供
  「制图平实 ↔ 复古水彩」开关，默认走平实。

---

## 5. 纹理与贴图处理（Texture & Map Processing）

### 5.1 Thunder 现状（几乎全程序化）

- 常驻**分类金字塔**（`WorldResidentLayout.hpp` 的 `kHeightPyramidW=3900`、
  `kPagePyramidW=7680`，四层共约 1490 页、~119 MiB，见架构审计报告 A2‑R）：
  高度、省份 ID、海岸 SDF。这些是**分类数据栅格**，不是美术贴图。
- 所有「质感」均在 world‑UV 空间**程序化生成**：羊皮纸纤维/霉斑（1199-1224）、
  海浪/菲涅尔/泡沫（1227-1304）、地形 PBR 飞溅、城市街区 90 m 块
  （`world_map.frag:929-1008`）、河流（`world_map.frag:875-924`）、农场格
  （727-767）。**没有任何手绘/扫描贴图集参与**。
- A2‑R 全常驻意味着**启动后零流式贴图传输**——美术贴图集也不在架构预期内。

### 5.2 Clausewitz 现状

- 「P 社味」的绝大部分来自**美术资产管线**：手绘省份地形贴图、3D 城市/建筑/树木/
  单位模型（V3 尤其重）、河流/道路矢量美术、法线/高光图、统一调色与光照导演。

### 5.3 差异分析

这是**质感差距的最大单一来源**。Thunder 的「全程序化」带来干净但「通用/塑料」的观感，
缺少美术导演的个性与细节密度。要逼近 PDX，必须在「程序化骨架」之上叠加「美术资产层」。

---

## 6. 关键调整建议（逼近 PDX 视觉风格）

每条含：**目标**（对标的 PDX 观感）、**方向**（具体改动）、**优先级**、**证据/落点**。

### A. 收敛远/近双模跳变 → 稳定观感【高】
- **目标**：像 CK3/V3 那样「细节渐进、身份恒定」，而非「水彩纸 ↔ 照片 PBR」硬跳变。
- **方向**：保留 3D 起伏与 PBR 飞溅，但让省份填充、边界、调色在远/近一致；把羊皮纸
  降为可选「复古」外观，默认走饱和平实填充。用 `closeFactor` 只调细节密度，不调风格。
- **证据**：`world_map.frag:1401-1405`（view_blend 双模）、`1460-1548`（水彩远模）。

### B. 地形/生态改数据驱动，删硬编码地理带【高】
- **目标**：山脉、沙漠、植被由内容定义，换世界不错位、可被美术雕刻。
- **方向**：`alpine_mountain_relief` 的 14 条造山带（vert 269-375）与 8 条沙漠带
  （frag 1493-1511）改为读取 `GeographyStore` 的 Topography/Vegetation/Climate 层；
  shader 只做「数据 → 飞溅权重」。
- **证据**：`GeographyStore.hpp`（EU5 三层模型已存在）、架构报告 R2。

### C. 引入美术贴图集与飞溅图【高】
- **目标**：获得 PDX 的手绘地形质感与细节密度。
- **方向**：为各生物群系制作平铺地形 albedo/法线/粗糙图集，用现有 PBR 飞溅框架
  （`calculate_pbr_terrain_splat`、`terrain.frag:60-122`）采样贴图而非纯色。
- **证据**：当前飞溅用的是 `c_grass_low` 等纯常量色（frag 700-710）。

### D. 近距 3D 道具层（建筑/树木/单位）【中高】
- **目标**：V3 式可辨识的城市、森林、单位模型，取代程序化 90 m 街区与噪声森林。
- **方向**：`world_map.frag:929-1008` 的 `city block` 程序化块改为实例化 3D 模型
  （建筑/树），由 `WorldMapLabels`/内容层驱动放置；河流/道路用矢量美术重绘。
- **证据**：`WorldMapLabels.hpp`、`living.frag`（建筑立面程序化，33 行，已是模型化雏形）。

### E. 省份交互层（悬停/选中/高亮）【中】
- **目标**：还原 PDX 核心体验——鼠标省份高亮、选区描边、地图模式平滑过渡。
- **方向**：在 `PoliticalMapState`/`MapModeStore` 之上加 selection/hover 描边通道，
  复用 `sovereign_border_smooth` 的等值面技术做高亮环。
- **证据**：`PoliticalMapState.hpp`、`MapModeStore.hpp`、`DirtySpanSet.hpp`。

### F. 统一政治着色管线，删遗留 overlay 双路径【中】
- **目标**：消除两套政治路径的漂移风险，单一连贯省份管线（对标 Clausewitz）。
- **方向**：明确 live `world_map.frag` 与 `PoliticalMapRenderPlan` 的 overlay 谁为
  真源；若 overlay（`vector_borders_vbo/ibo`、`country_colors`、`map_mode_words`）
  才是目标架构，把 `world_map.frag` 的政治/边界逻辑迁过去，避免双写（R6）。
- **证据**：`PoliticalMapRenderPlan.cpp:9-38`、`world_map.frag` 只读 palette+id。

### G. 美术导演：统一调色与光照【中】
- **目标**：PDX 那种「一眼就知道是同一款游戏」的色彩与光照一致性。
- **方向**：在既有 `tonemap → saturation → LUT → dither` 链
  （`world_map.frag:1624-1627`）补 LUT 与季节/时段光照导演；收敛各 shader 各自为政的
  `sun_dir`/天空色。
- **证据**：`tonemap.frag`、`world_map.frag` 多处硬编码 `sun_dir`。

### H. 投影与环绕接缝抛光【低】
- **目标**：环绕无缝、可选球面曲率。
- **方向**：强化 `world_map.frag:1561-1563` 的接缝遮罩，评估是否加轻度球面曲率以
  贴近 V3 的「微球」观感。
- **证据**：wrap seam mask 已存在，说明风险在。

---

## 7. 优先级路线图

| 优先级 | 调整 | 对标观感收益 | 风险/成本 |
|---|---|---|---|
| 高 | B 数据驱动地形/生态 | 内容可控、跨世界一致 | 中（接 GeographyStore） |
| 高 | C 美术贴图集 + 飞溅 | 质感跃升最大单项 | 高（美术资产生产） |
| 高 | A 收敛远/近跳变 | 观感稳定、更像 PDX | 低（调参+默认外观） |
| 中高 | D 近距 3D 道具 | 城市/森林可辨识 | 高（模型+放置系统） |
| 中 | F 统一政治管线 | 架构债清除 | 中（双路径合并） |
| 中 | E 省份交互层 | 核心体验还原 | 中 |
| 中 | G 美术导演/调色 | 一致性与品牌感 | 低-中 |
| 低 | H 投影/接缝 | 边缘抛光 | 低 |

---

## 8. 证据索引（file:line）

- `shaders/world_map.frag:3-8` 连续材质定义
- `shaders/world_map.frag:19-23` palette / province_id / coast_sdf 绑定
- `shaders/world_map.frag:33-67` 视觉调试位掩码（含 V3 引用）
- `shaders/world_map.frag:96-?` `sample_height01`；`:116-?` `sample_coast_raw`
- `shaders/world_map.frag:208-215` `political_colour`（ID→调色板 texelFetch）
- `shaders/world_map.frag:583-676` `sovereign_border_smooth`（栅格等值面边界）
- `shaders/world_map.frag:687-` `calculate_pbr_terrain_splat`（PBR 飞溅）
- `shaders/world_map.frag:875-924` 程序化河流；`:929-1008` 程序化城市街区
- `shaders/world_map.frag:1177-1628` `main()` 七段合成
- `shaders/world_map.frag:1493-1511` 硬编码沙漠带；`alpine_mountain_relief` 硬编码造山带
- `shaders/world_map.vert:3-7,58,96-109,156-160,269-375,380-439,421-439` 顶点/投影/山脉
- `shaders/terrain.frag:49-213` 独立 PBR/纸双模
- `shaders/political_overlay.frag:7-11` 合成占位（synthetic stand‑in）
- `shaders/ocean.frag`、`shaders/living.frag` 海洋/建筑独立 shader
- `src/.../render/map/PoliticalMapRenderPlan.cpp:9-38` 双政治路径资源与通道
- `src/.../render/map/WorldResidentLayout.hpp` `kWorldLevels` / `kHeightPyramidW` / `kPagePyramidW`（A2‑R）
- `src/.../simulation/world/GeographyStore.hpp` EU5 Climate×Topography×Vegetation 三层模型

---

## 9. 与既有审计/记忆的衔接

- 架构审计报告（`2026-09-05_map_layer_architecture.md`）R6「GeographyStore vs
  PoliticalMapState 双写无门禁」、R2「kWorldLevels 布局常量钉死世界尺寸」在本报告中
  得到渲染侧印证：双政治路径（live inline + legacy overlay）与硬编码世界地理带，
  正是上述架构债在表现层的投影。
- MEMORY 已记录 A2‑R 全常驻金字塔（≈119 MiB GPU、零流式），本报告第 5 节据此指出
  「美术贴图集不在当前架构预期内」，故调整 C 需先扩展资产管线而非仅改 shader。
- 下一可交付：若采纳 B+C，建议先做「`GeographyStore` 三层 → 地形飞溅权重」的接入 PoC，
  并补一张手绘地形贴图集样张验证飞溅框架。
