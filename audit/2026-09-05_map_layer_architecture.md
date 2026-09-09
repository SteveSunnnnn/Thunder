# Thunder 地图层面架构拆解

日期：2026-09-05 ｜ 依据：当前源码（file:line 均可复核）+ docs/ 架构文档
范围：整体分层与组件划分、职责边界与依赖、地图维度数据流转、关键设计理念与决策考量、风险与文档债。

---

## 0. 结论摘要

1. Thunder 的地图不是"几何体集合"，而是一条**离线编译 → 二进制容器 → 不可变拓扑 → 可变模拟 → 虚拟页面呈现**的六级管道；`.thunderworld` 是运行时地图唯一权威来源（`docs/ARCHITECTURE.md:36-37` 明文废除全图 atlas 路径）。
2. 架构上最核心的一手棋是**身份与呈现解耦**：省份身份固化在 R16 光栅页里，归属/颜色/地图模式全部是间接表，"征服一个省 = 改 8 字节记录"（`PoliticalMapState.hpp:33-37, 52-54`）。
3. 实现已从文档所述的"有界 planner/cache 流式驻留"演化为 **A2-R 全驻留金字塔**（`WorldResidentLayout.hpp:7-28`、`WorldMapPageStreamer.hpp:39-47`）：1490 页启动后台解码，之后零流式。这是一次正确的决策，但三份文档尚未跟上（详见 §5 R1/R2）。
4. 与 jomini/Clausewitz 对照：Thunder 在"静态地理身份、确定性、页面原子性"上达到甚至超过对标水位；差距仍在**作者化内容链路**（无随仓库世界包、GIS 依赖未锁定，`MAP_CONTENT_REQUIREMENTS.md §8`）。

---

## 1. 整体分层：从作者意图到像素的六级管道

```text
L0  离线 GIS 生产层（引擎外，Python/QGIS/GDAL）
      thunder_gis_compile.py → manifest + 二进制块 → thunder_world_compiler
      ↓ 产出唯一运行时输入
L1  容器层      thunder/content/worldpack
      WorldPackWriter/Reader/DecodeScratch + WorldPackMetadata
      ↓ read(chunk key) → bytes
L2  拓扑解码层  thunder/simulation/world
      WorldTopology::load —— 纯解码，不知道 World 存在
      ↓ move 进组合器
L3  组合层      WorldBootstrap → WorldBootstrapResult{World + 拓扑视图}
      ↓ 桥接：RenderSnapshotBuilder（唯一模拟→呈现通道）
L4  呈现地图层  thunder/presentation/render/map|terrain
      页面契约 / 政治状态 / 地图模式 / 拾取 / 矢量边界 / clipmap / LivingMap CPU 侧
      ↓ 后端中立 plan + 紧凑 payload
L5  GPU 后端    presentation/render/vulkan/VulkanWorldMap
      驻留金字塔纹理、staging ring、间接实例绘制
```

关键约束（`docs/ARCHITECTURE.md` 十法中的地图相关三条）：

- 法 1：模拟不依赖渲染，渲染不得改写世界 —— 后端只接受紧凑 payload（`submit_ui / submit_living_instances / submit_map_overlay / set_world_political_state`，`ARCHITECTURE.md:24`）。
- 法 8：昂贵 GIS 离线做完 —— 运行期不解析 shp/GeoJSON（`ARCHITECTURE.md:31`）。
- 边界守卫：`src/thunder/**` 出现 `#include "game/..."` 直接编译失败；引擎仓库不含任何 authored 世界内容。

---

## 2. 核心组件划分与职责边界

### 2.1 L1 容器层（content/worldpack）

| 组件 | 职责 | 证据 |
|---|---|---|
| `WorldChunkKey` | 五元组身份 `(type, level, x, y, variant)` | `WorldPack.hpp:60-69` |
| `WorldPackWriter` | 追加写入 + 排序索引 + build hash，O(1) 拒绝重复 key | `WorldPack.hpp:109-136` |
| `WorldPackReader` | 随机读取：二分索引 + `RandomAccessFile`（POSIX `pread`，多 worker 并发读同一 fd） | `WorldPack.hpp:140-159`；`WORLD_COMPILER.md:71-77` |
| `WorldPackDecodeScratch` | 每 worker 一个：复用解压上下文与缓冲，热身后零临时分配 | `WorldPack.hpp:164-182` |
| `WorldPackMetadata` | 坐标契约：投影、bounds、水平环绕、128² 页、64 km level-0 页、层级数、各类计数 | `WorldPackMetadata.hpp:16-42` |
| 26 种 `WorldChunkType` | 页面族（province_coast/height/mask）+ 定义块（CNT2/STS2/PRV2/HIS1/ARA2/MVP2/LOC2）+ 静态层（RIV1/PTR1/ARC1/RDS1/PLC1/ANC1） | `WorldPack.hpp:21-48`；魔数见 `WorldTopology.cpp:13-19` |

边界：容器只管"字节放哪、怎么校验"，不知道任何语义。build hash 由排序后的 chunk key + 原始尺寸 + 校验和推导，与 manifest 行序无关，可直接用于 OOS 诊断与缓存失效（`WORLD_COMPILER.md:85`）。

### 2.2 L2 拓扑解码层（simulation/world）

`WorldTopology`（`WorldTopology.hpp:34-50`）是纯值聚合，装载顺序固定（`WorldTopology.cpp:87-363`）：

1. metadata（header↔metadata horizontal_wrap 一致性检查，`:91-96`）
2. CountryDefinitions（CNT2 tag-only / 旧 CNT1 双格式兼容，`:98-118`）
3. MarketDefinitions（仅 owner 身份，`:120-129`）
4. StateDefinitions → `GeographyStore.create_state`（STS2 带保留位校验，`:131-150`）
5. ProvinceDefinitions（PRV2：kind/flags，`:152-177`）
6. metadata 计数与 bootstrap 对账（国家/州/省/海/湖计数，`:179-193`）
7. HistoricalSetup（HIS1：州/省归属回填 + sea_starts，`:195-234`）
8. 市场 owner 确定性回填（省 owner → 无主市场，`:237-245`）
9. `GeographyStore::validate` 全图校验（`:247-248`）
10. Area/TradeProvince/Location 三块要么全有要么走 `build_default` 兼容回退（`:249-316`）
11. `scope_index`/`state_regions` 重建、`spatial_placement` 装载并对账 authored_hub_count（`:317-323`）
12. AdjacencyOffsets/Neighbors CSR 装载 + 对称性强制（2000 万边上限，`:325-357`）；最后静态层与标签（`:359-361`）

八个子组件各自的领地：

| 组件 | 领地 | 关键形状 |
|---|---|---|
| `GeographyStore` | 省份/州的可变事实（key、归属、中心坐标、kind、海岸、不可通行、三层正交地形 Climate×Topography×Vegetation） | SoA 列存储，`GeographyStore.hpp:385-485`；气候→地力/损耗/战斗宽度全部 constexpr（`:117-207`） |
| `GeographyScopeIndex` | 国家→州→省只读 CSR 视图 | `GeographyStore.hpp:487-498` |
| `ProvinceAdjacencyGraph` | 不可变邻接：`offsets[N+1]` + 8 字节 `ProvinceNeighbor`（flags + Q8 定点成本） | `ProvinceAdjacencyGraph.hpp:29-60`；装载后强制 `is_symmetric()` |
| `WorldMapHierarchy` | Area→TradeProvince→Location 三级空间层级；Location 指回光栅 ProvinceId；只拥有空间身份，不拥有政治归属 | `WorldMapHierarchy.hpp:15-52, 96-100`（raster_location_lookup_ CSR） |
| `StateRegionIndex` | 州之上的只读地理分组，今天 1:1，为"一区多州"预留 | `StateRegionIndex.hpp:15-18` |
| `SpatialPlacementDatabase` | 64 km chunk 的放置点/聚落锚点，省→类→范围三级 CSR | `SpatialPlacement.hpp:87-119`，chunk_size_m=64'000（`:89`） |
| `WorldStaticLayers` | 河流/交通 chunk key 目录 + 建筑风格区 + 州资源容量；"既非模拟状态也非图形资源" | `WorldStaticLayers.hpp:26-39` |
| `WorldMapLabels` | 六类标签（国家/海洋/区/省/Location/地理），带 spine 折线与缩放窗口 | `WorldMapLabels.hpp:13-37` |
| `HierarchicalPathfinder` | 消费邻接 + 州划分，构建 HPA* 门户图 + 供给网络求解 | `HierarchicalPathfinder.hpp:53-116` |

### 2.3 L3 组合层

`WorldBootstrap::load`（`WorldBootstrap.cpp:9-39`，全文 41 行）：`WorldTopology::load(pack)` → 把八个拓扑成员 move 进 `WorldBootstrapResult`，国家/市场进 `World`，`geography` 与 `map_hierarchy` 挂到 `World` 上。它是"不可变世界 → 可变模拟世界"的**唯一窄口**，职责仅组合，不做解码。

### 2.4 L4 呈现地图层（presentation/render/map + terrain）

四个子域：

**(a) 页面契约四件套**（引擎与渲染的公共语言，`ARCHITECTURE.md:159-160` 点名四分）：

- `WorldMapPageKey`：`(x, y, level)` 虚拟页身份，"属于地图流而非地形几何"（`WorldMapPageKey.hpp:8-18`）。
- `WorldMapPage`：解码后 CPU payload——province 32 KiB + coast SDF 32 KiB + height 8,450 B + lake/spatial mask 各 16 KiB，无文件句柄无图形 API（`WorldMapPage.hpp:20-29`）。
- `WorldMapPageSource`：唯一 CPU 读取器，损坏页重置为安全海面值而非返回脏数据（`WorldMapPageSource.hpp:33-35`）。
- `WorldMapPageStreamer`：解码并发（worker 池 + 背压 1024 上限）、驻留位图、视锥补丁构建（`WorldMapPageStreamer.hpp:48-129`）。
- `WorldResidentLayout`：A2-R 驻留布局常量——三张全局纹理、四层手工烘焙页网格（`WorldResidentLayout.hpp:7-66`）。

**(b) 政治状态三元组**：`PoliticalMapState`（8 B/省 + 4 B/国 + 4 B/省模式值 + 三条 DirtySpanSet，`PoliticalMapState.hpp:54-87`）；`MapModeStore`（mode-major 16-bit 紧凑字 + `MapModeGpuView` 小 uniform，`MapModeStore.hpp:52-83`）；`DirtySpanSet`（稀疏→稠密 1/32 交叉自动切换，`DirtySpanSet.hpp:62-73`）。

**(c) 矢量与装饰**：`VectorMapSystem`（共享边重建、三档 LOD 边界 polyline、屏幕空间缓存、古地图风金边/晕滃线/罗盘线/斜线占领带，`VectorMapPipeline.hpp:77-169`）+ `VectorMapTypography` + `PaperMapTransition` + `MapDecorationRenderer` + `BorderMesh3D`。

**(d) 地形 clipmap 族**：`TerrainClipmap`（8 级、400 patch 上限、浮动原点）、`TerrainHeightPage`（65² u16，绝对量化 −12000 m 起 0.5 m 步进，`TerrainHeightPage.hpp:10-29`）、`TerrainPageCache`/`TerrainStreamingPlanner`/`StreamingBudget`（帧时自适应字节预算）。

**Living Map CPU 侧**：`SpatialPlacementDatabase` + 语义类→候选类回退链（Factory→Industrial→Urban→Buildable…）+ 64 km chunk 版本化更新（`LIVING_MAP_ARCHITECTURE.md` 全篇）；CPU 不拥有 Vulkan 对象，只产出 `LivingMapRenderPlan`。

### 2.5 L5 GPU 后端

`VulkanWorldMap.cpp` 是后端窄面的世界地图部分：只记录有界 atlas 上传 + 页面补丁实例缓冲 + 屏障；打包、页准入、CPU 解码全部留在 `WorldMapPageStreamer`（`VULKAN_BACKEND.md:78-83` 契约与代码一致）。

---

## 3. 地图维度的数据流转机制

### 3.1 编译期：GIS → `.thunderworld`

每个 `(level,x,y,variant=0)` 页面必须同时存在四件套（`MAP_CONTENT_REQUIREMENTS.md §5`）：

| 块 | 尺寸 | 编码 |
|---|---:|---|
| province_coast | 65,536 B | 128² uint16 省份 ID（0=水，运行时 N 编码为 N+1）+ 128² int16 海岸 SDF（0.5 m 量化，±16.38 km） |
| height | 8,450 B | 65² uint16，`height_m = −12000 + q×0.5` |
| lake_mask | 16,384 B | 128² uint8 ∈ {0,1} |
| spatial_mask | 16,384 B | 128² uint8 ∈ {0,1} |

编译器十项硬校验（计数对账、页面族完整、邻接对称、资源全覆盖、chunk key 唯一、单块 64 MiB 上限等，`MAP_CONTENT_REQUIREMENTS.md §6`）。AI 生成的连续 RGB 图严禁直接进管线——必须量化→矢量化→EDT SDF 烘焙（`TERRAIN_ARCHITECTURE.md §9`）。

### 3.2 启动期：pack → 可变 World

§2.2 的十二步顺序即数据流：字节 → wire 魔数分派 → SoA 列 → CSR 索引 → 计数对账 → `World`。两个值得注意的细节：

- 旧 CNT1（含经济值）与新 CNT2（tag-only）双格式读取，经济值只作为"独立包检视的遗留列"，游戏内容一律由脚本运行时绑定（`WorldTopology.hpp:19-23` 注释，`WORLD_COMPILER.md:67-69`）。
- 邻接 CSR 装载后立刻 `is_symmetric()` 强检，不对称即抛异常（`WorldTopology.cpp:355-356`）——把几何修复责任留在线下。

### 3.3 运行期：页流（A2-R 全驻留）

这是当前最活跃、也最值得讲清的一条流（`WorldMapPageStreamer.cpp` + `VulkanWorldMap.cpp`）：

1. **准入**：`enqueue_all_pages` 一次性把 4 层共 1490 页入队——粗层优先、同层按到视中心的最短弧距离排序（日期变更线取 `du -= std::round(du)`，`WorldMapPageStreamer.cpp:138-157`）。
2. **解码**：3~4 个 worker 各持独立 `WorldPackReader + DecodeScratch`，背压上限 1024 页，帧线程永不碰文件 IO（`:40-74`）。
3. **上载**：`pop_decoded(frame, 128)` 每帧最多 128 页进入上载窗口（≈9.5 MiB/帧封顶：128×73,988 B），staging ring 按 `[height | province | sdf]` 分区，`vkCmdCopyBufferToImage` 写入三张驻留金字塔（`VulkanWorldMap.cpp:660-733`）。GPU 常驻 ≈119 MiB（height 3900×1820 + 两张 7680×3584 的 u16 平面，`WorldResidentLayout.hpp:47-50`）。
4. **绘制**：`build_world_patches` 按分数 LOD 选层（整数部分取细层，小数部分向粗层 morph），未到位页沿 mip 链回退到最近粗层祖先（金字塔同一地址空间，无需 UV 手术），新到页 0.15 s 淡入；水平包裹页故意两侧过覆盖一页防缝隙（`WorldMapPageStreamer.cpp:225-307`）。
5. **驻留登记只发生在帧线程**（`pop_decoded` 内，`:178-194`），worker 永远只碰请求/结果双端队列——单一写者原则。

### 3.4 运行期：玩法状态流（横向桥）

```text
World (GeographyStore.province_owners / 经济 store)
  → RenderSnapshotBuilder（唯一模拟桥，ARCHITECTURE.md:140）
  → SnapshotExchange 三槽 SPSC 三缓冲（生产者不等消费者，读中的槽不动；SnapshotExchange.hpp:11-85）
  → PoliticalMapState.set_owner / MapModeStore.set_scalar
  → DirtySpanSet（1/32 密度交叉：稀疏 span 排序合并 vs 整段全量上传）
  → 后端紧凑 buffer 上载
```

量化事实（docs + 代码一致）：8000 省世界，owner+map value 全集 ≈96 KiB；100 次连续征服只上载 ≈800 字节；10 万省图 1 万次随机改会自动转 ~1.2 MiB 全量上传而不是排序几千个小 span（`POLITICAL_MAP_ARCHITECTURE.md §4`；`DirtySpanSet.hpp:62-73`）。地图模式切换 = 换一个小 uniform（kind+offset+range+generation），永不触发 GIS/栅格/归属重建（`MAP_MODE_ARCHITECTURE.md` 性能规则）。

### 3.5 运行期：拾取流

`WorldMapPicker::open` 用包 metadata 配置 `ProvincePickingCache`（页世界尺寸、层级数、原点，`WorldMapPicker.cpp:18-24`）；`pick_uv` 先处理水平/垂直包裹与 v 翻转，再逐级降层尝试：确保页 → CPU 镜像取 texel → `location_for_raster` → 州/区解析（`WorldMapPicker.cpp:59-94`）。全程无 GPU 回读、无 fence 等待。

---

## 4. 关键设计理念与架构决策考量

| # | 决策 | 理由与权衡 |
|---|---|---|
| D1 | **间接表而非几何**：省份 ID 光栅永久不动，归属/颜色/模式全在紧凑间接表 | 征服只改 8 字节；国家换色是 4 字节调色板编辑（`PoliticalMapState.hpp:52-54`）。代价：着色器多一次间接查表；收益：任何玩法变更 O(1)。这是整张地图的"第一性原理"。 |
| D2 | **页面家族原子性**：province+coast 合成一个 64 KiB bundle，一起解码一起上载 | 严禁着色器看到第 N 代的省页配第 N−1 代的岸页（`POLITICAL_MAP_ARCHITECTURE.md §3`、`PoliticalMapPageBundle.hpp:12-30` 的 `static_assert(64 KiB)`）。用物理布局换掉世代协议。 |
| D3 | **A2-R 全驻留取代有界流式** | 1490 页启动 0.4 s 内 3 worker 解码完，之后零流式、零逐出、补丁生成变成纯算术。动机链：高度金字塔需要一个"真 mip 链表达不了"的手工布局（level 3 底行只有半页在域内，`WorldResidentLayout.hpp:25-28`）→ 与其维护双驻留体系，不如全驻留。代价：世界尺寸被布局常量钉死（见 R2）。 |
| D4 | **64 B chunk 对齐** | 曾试 4 KiB：高压缩地图页缩到几百字节时对齐洞主导包体。64 B 后仍兼容普通缓冲 `pread`，DirectStorage 未来可加，不改逻辑 key（`WORLD_COMPILER.md:90-99`）。 |
| D5 | **同一地理、两种表示**：渲染用稠密光栅采样，寻路/前线/贸易用 CSR 邻接图 | "Rendering wants dense image sampling; pathfinding wants contiguous neighbor scans"（`POLITICAL_MAP_ARCHITECTURE.md §7`）。两者刻意互不拥有对方：邻接扫描 = 两次 offset 载入 + 连续 span，零哈希零堆追踪（`ProvinceAdjacencyGraph.hpp:38-41`）。 |
| D6 | **双精度 CPU 坐标 + 浮点原点 GPU** | lat/lon → double 投影米 → 每帧把浮点原点吸附到相机附近（256 m snap）→ 相对 float 下 GPU。解决"相机离投影原点数千公里时的抖动"，同时保住紧凑顶点格式（`TERRAIN_ARCHITECTURE.md §1`）。 |
| D7 | **高程绝对量化** | 每 tile min/max 量化会让相邻 tile 对同一共享边解出不同高度；全局单一 scale 换 ~0.25 m 误差上限（`TERRAIN_ARCHITECTURE.md §4`）——正确性优先于局部精度。 |
| D8 | **地图模式 = 数据不变、视图切换** | 7 种模式 × 8000 省 = 112 KiB 常驻 GPU；16-bit + log1p 防"少数极端省吃掉整个视觉量程"；兼容设备可上载时扩 32 位而不动模拟格式（`MAP_MODE_STORE.hpp:48-51` 注释、`MAP_MODE_ARCHITECTURE.md`）。 |
| D9 | **拾取走 CPU 镜像** | 64 页 ≈2 MiB、生产 512 页 ≈16 MiB；省掉 `vkCmdCopyImageToBuffer` + fence + 一帧输入延迟（`POLITICAL_MAP_ARCHITECTURE.md §5`）。 |
| D10 | **地形静态网格复用 + 距离分级 LOD** | 不为每个 tile 上传唯一网格：65²/33²/17² 三档静态网格 × 400 patch 实例 ≈84 万三角形，比全 65² 省 74%；patch 载荷 ≤40 B、共 16 KiB（`TERRAIN_ARCHITECTURE.md §2-3`）。 |
| D11 | **确定性贯穿地图** | build hash 排序后序无关（OOS 可用）；空间放置用稳定 ID/种子加权选择；GPU 档位只改画质不改模拟规则（`GPU_TIERS.md` 性能规则）；`SpatialPlacementDatabase`/`WorldMapHierarchy` 等全部有 `checksum()`。 |

---

## 5. 风险、缺口与文档债（诚实清单）

| # | 发现 | 证据 |
|---|---|---|
| R1 | **文档滞后于实现**：`TERRAIN_ARCHITECTURE.md`、`POLITICAL_MAP_ARCHITECTURE.md`、`ARCHITECTURE.md:109` 仍以 "planner/cache 有界流式" 为叙述主线，而世界地图 live 路径已是 A2-R 全驻留。`WorldMapPageStreamer` 的 doc 注释已自述新契约（"A2-R residency owner"） | `WorldMapPageStreamer.hpp:39-47` vs `docs/ARCHITECTURE.md:109` |
| R2 | **布局常量钉死世界**：`kWorldLevels` 硬编码 40×28 base grid；`open()` 拒绝页网格不匹配的包（抛异常）。引擎层常量与"一个特定世界内容"耦合，换世界需重烘焙头文件并重编——"引擎不含 authored 内容"的边界被侵蚀 | `WorldResidentLayout.hpp:39-44, 61-62`；`WorldMapPageStreamer.cpp:83-94` |
| R3 | **lake/spatial mask 未进 GPU 金字塔**：只留在 CPU payload 供拾取/逻辑用；着色器水判定目前靠省 ID==0 与 SDF 符号 | `WorldMapPage.hpp:16-19` 注释 |
| R4 | **半退役组件**：`PoliticalMapStreamingPlanner`/`TerrainPageCache`/`ProvincePickingCache` 在运行时主路径无消费者（仅 `thunder_cli`、`thunder_world_validate` 工具引用），却仍是三份文档的主角 | 全仓 grep：runtime/ 下零命中 |
| R5 | **页 Y 翻转约定三处重复**：`(rows-1)-y` 分别出现在 enqueue、resident、GPU 上载三处，靠人肉保持一致 | `WorldMapPageStreamer.cpp:147, 206`；`VulkanWorldMap.cpp:701` |
| R6 | **归属双写无强制**：`GeographyStore.province_owners`（模拟权威）与 `PoliticalMapState`（呈现权威）之间的同步责任全在桥接层约定，编译器/测试不拦截漏同步 | `GeographyStore.hpp:424` vs `PoliticalMapState.hpp:59` |
| R7 | **生产链路缺口**（继承自 MAP_CONTENT_REQUIREMENTS §8）：无随仓库世界包、Python GIS 依赖未锁定、官方生成器仅 Mercator（Gall 运行时可读但无产出链路）、`placement_candidates` 不在硬性 required type 之列 | `MAP_CONTENT_REQUIREMENTS.md §4, §6, §8` |

---

## 6. 与 Clausewitz/jomini 的对照视角

| 维度 | Clausewitz/jomini | Thunder | 评价 |
|---|---|---|---|
| 省份身份 | `provinces.bmp`/`definition.csv`（运行时载入位图） | R16 光栅页 + 编码偏移 1 + 65,534 上限，随包分发、多分辨率全烘焙 | 对齐且更强（多级 LOD 离线烘焙） |
| 归属呈现 | province ownership → 程序化 color map，部分作品整图重生成 | 8 字节间接记录 + 4 字节调色板 + dirty span | Thunder 显式规避了"repaint world texture"这一 PDX 曾有的开销 |
| 地图流式 | CK3/Vic3 的 tile/chunk 驻留 + 帧预算 | 曾同为有界流式；现 A2-R 全驻留（内容规模当前远小于 PDX 世界） | 当前规模下是合理取舍；世界变大需重新评估（R2 是前置债） |
| 拾取 | 位图取样（CPU 端同样可行） | CPU 镜像 + 层级回退 + 包裹处理 | 同水位，契约更明确 |
| 内容语言 | 地理与玩法同仓（game/ 目录） | 引擎库禁止 authored 内容；`.thunderworld` + 脚本运行时绑定 | 更干净的引擎/游戏分离；代价是内容链路尚未闭环 |

---

## 7. 建议（按优先级）

1. **P0｜修 R2**：把 `kWorldLevels` 等驻留布局常量从编译期常量改为"由 pack metadata 校验驱动、从包内 metadata 块构建"的运行期结构；保留当前 40×28 作为默认特例。否则"引擎"事实上只服务一个世界。
2. **P0｜修 R1**：三份文档各加一节 "A2-R 现状"，标注 planner/cache 一节为 editor/legacy 路径，防止后来者按文档接错。
3. **P1**：为"模拟归属 → PoliticalMapState"同步写一个引擎级一致性测试（对账 `GeographyStore.province_owners` vs `PoliticalMapState.province_records`），把 R6 从约定变成门禁。
4. **P1**：`(rows-1)-y` 收敛为 `WorldLevelLayout` 上的一个 `texel_y(key)` / `pack_y(key)` 内联函数（R5）。
5. **P2**：`PoliticalMapStreamingPlanner`/`TerrainPageCache` 要么标注 `[[deprecated]]` 并在文档移出主线，要么把 `thunder_cli` 的用法补进文档作为其唯一存在理由（R4）。
6. **P2**：lake/spatial mask 上 GPU（顺带修 shallow-water 在湖泊上的判定），或正式声明它们是 CPU-only 契约并写进 `WorldMapPage.hpp` 注释（R3）。

---

## 附：证据文件清单

- 容器：`src/thunder/content/worldpack/WorldPack.hpp`、`WorldPackMetadata.hpp`
- 拓扑：`src/thunder/simulation/world/WorldTopology.{hpp,cpp}`、`WorldBootstrap.{hpp,cpp}`、`GeographyStore.hpp`、`ProvinceAdjacencyGraph.hpp`、`WorldMapHierarchy.hpp`、`StateRegionIndex.hpp`、`SpatialPlacement.hpp`、`WorldStaticLayers.hpp`、`WorldMapLabels.hpp`、`HierarchicalPathfinder.hpp`
- 呈现：`src/thunder/presentation/render/map/`（WorldMapPage/PageKey/PageSource/PageStreamer/ResidentLayout/ProvinceRasterPage/CoastDistancePage/PoliticalMapState/MapModeStore/DirtySpanSet/PickingCache/Picker/VectorMapPipeline/PoliticalMapPageBundle）、`terrain/TerrainHeightPage.hpp`、`SnapshotExchange.hpp`、`RenderSnapshotData.hpp`
- GPU：`src/thunder/presentation/render/vulkan/VulkanWorldMap.cpp`
- 文档：`docs/ARCHITECTURE.md`、`WORLD_COMPILER.md`、`POLITICAL_MAP_ARCHITECTURE.md`、`LIVING_MAP_ARCHITECTURE.md`、`MAP_MODE_ARCHITECTURE.md`、`TERRAIN_ARCHITECTURE.md`、`VULKAN_BACKEND.md`、`GPU_TIERS.md`、`MAP_CONTENT_REQUIREMENTS.md`
