# Thunder 地图模块与地图内容文件要求

> 本文以当前源码为准，回答两个问题：引擎有哪些地图模块；要准备什么源文件，才能生成运行时可接受的地图内容。
> 顶层模块边界仍以 `ARCHITECTURE.md` 为准，二进制容器细节以 `WORLD_COMPILER.md` 为准。

## 1. 结论

Thunder 的运行时地图不是一组 GeoJSON、Shapefile 或图片，而是一个离线生成的、可随机访问的
`world.thunderworld` 二进制包。生产链路分为两段：

```text
带 CRS 的 GIS / DEM / 历史与资源表
  -> tools/thunder_gis_compile.py
  -> manifest.txt + 已编码的二进制块 + compile_report.json
  -> thunder_world_compiler
  -> world.thunderworld
  -> WorldTopology / WorldBootstrap / WorldMapPageStreamer / LivingMap
```

游戏发布时只需要携带 `.thunderworld` 和脚本化游戏内容；GeoJSON、Shapefile、GeoPackage、DEM、CSV
以及 Python GIS 依赖都属于离线生产工具链，不应进入游戏运行时。

## 2. 引擎地图模块清单

| 模块 | 当前职责 | 消费的数据 |
|---|---|---|
| 离线 GIS 生成器 | 修复几何、投影、栅格化、生成 SDF、邻接、层级、放置点和静态矢量块 | 作者源文件 |
| `WorldPack` | `.thunderworld` 容器、索引、压缩、校验和、build hash、随机读取 | manifest 与二进制块 |
| `WorldPackMetadata` | 世界范围、投影、环绕、分页、数量契约 | `metadata` |
| `WorldTopology` | 加载不可变地理身份、Area/Trade Province/Location 层级、邻接、静态层和空间放置 | 定义及拓扑块 |
| `WorldBootstrap` | 把不可变拓扑组装进可变模拟世界 | `WorldTopology` + 经济定义 |
| `WorldContentBinder` | 用脚本内容补全国家经济值和历史效果 | 国家 tag + ThunderScript 内容 |
| 政治地图与拾取 | 用 R16 省份 ID 页查询省份，用间接表处理归属、颜色和地图模式 | province/coast 页 |
| 地形 | 多级 clipmap、高程页、浮动原点和 GPU 页面驻留 | height + spatial/lake mask |
| 静态地图层 | 河流、道路/铁路、建筑风格区和州资源容量 | RIV1/PTR1/ARC1/RDS1 |
| Living Map | 城市、农场、矿山、港口、植被等可视实例的确定性放置与分块更新 | PLC1/ANC1 |
| 地图层级与寻路 | Area -> Trade Province -> Location，以及对称 CSR 省份邻接 | ARA2/MVP2/LOC2 + adjacency |
| 编辑器与 Vulkan 后端 | 编辑/查看已编译地图，按相机请求页面并上传 GPU | `.thunderworld`；不解析源 GIS |

地图包只负责稳定的地理身份和静态空间事实。人口、GDP、国库、税率、商品、建筑类型、POP 等
游戏规则数据不应固化在地图包里；`CountryDefinitions` 当前只保存 tag，经济内容由脚本运行时绑定。

## 3. 作者源文件要求

### 3.1 最小可生成输入

唯一命令行必填的地图源是 `--provinces`。它必须是 GeoPandas/Fiona 能读取的矢量数据集，并满足：

- 数据集非空且声明 CRS；生成器会统一转成 WGS84，再投影为 EPSG:3857。
- 有效几何必须是 `Polygon` 或 `MultiPolygon`；空几何会移除，其余几何先执行 `make_valid()`。
- 必填字段为 `province_key`、`state_key`，两者在陆地记录上不得为空。
- `province_key` 在陆地、海域、湖泊三类中全局唯一；总数不得超过 65,534。
- 所有 key 以 UTF-8 编码后不得超过 65,535 字节。
- 跨越 +/-180 度的多边形应采用合理的经度环；生成器会尝试拆分日期变更线几何。

最小输入可以生成技术上完整的包，但没有 DEM 时陆地高度为 0；没有作者化附加层时，生成器会写入
默认或空的河流、交通、建筑风格、资源和放置数据。因此“能编译”不等于“内容达到生产质量”。

### 3.2 省份图层字段

| 字段 | 要求 | 用途或默认值 |
|---|---|---|
| `province_key` | 必填、非空、全局唯一 | 稳定叶级/raster 身份 |
| `state_key` | 必填、非空 | 所属州身份 |
| `country_tag` | 可选 | 初始历史归属的便捷来源 |
| `market_key` | 可选 | 初始市场归属的便捷来源 |
| `location_key` | 可选 | 当前生成器缺省回退为 `province_key` |
| `trade_province_key` | 可选 | 缺省回退为 `state_key`，再回退为 Location key |
| `area_key` | 可选 | 缺省回退为 `country_tag`，再回退为 `area.neutral` |
| `coastal` | 可选布尔 | 海岸属性，默认 false |
| `impassable` | 可选布尔 | 不可通行属性，默认 false |
| `state_capital` | 可选布尔 | 一个州恰有一个标记时用作首府 |
| `constraint_flags` | 可选整数 | Location 约束；陆地为 0 时补默认陆地位 |

`country_tag` 和 `market_key` 只用于历史初始化/身份关联，不应承载经济数值。

### 3.3 可选源文件

| 参数 | 文件/几何要求 | 关键字段与校验 |
|---|---|---|
| `--states` | 带 CRS 的 Polygon/MultiPolygon | `state_key` 必须存在；可含 `country_tag`、`market_key`、`capital_province`、`coastal`、`impassable_provinces` |
| `--seas` | 带 CRS 的 Polygon/MultiPolygon | 可用 `province_key` 或 `sea_key`；可含 `sea_start`；水平环绕世界必须显式提供 |
| `--lakes` | 带 CRS 的 Polygon/MultiPolygon | 可用 `province_key` 或 `lake_key`；湖泊是可拾取的真实 ProvinceId，但不生成陆地导航邻接 |
| `--rivers` | 带 CRS 的 LineString/MultiLineString | 同时生成河流矢量块，并给被河流穿越的既有邻接边添加 river flag |
| `--roads`, `--rails` | 带 CRS 的 LineString/MultiLineString | 分别作为 transport variant 分块；没有输入时仍生成一个空 transport 块 |
| `--straits` | UTF-8 CSV | 端点列接受 `from/to`、`from_province/to_province`、`a/b` 或 `province_a/province_b`；可含 `flags/type` 与 `base_cost/cost` |
| `--hubs` / `--settlements` | 带 CRS 的 Point/MultiPoint | 可含 `province_key`、`state_key`、`hub_kind/kind/type`、`key/name`、`importance`；缺省类型为 city |
| `--architecture-regions` | 带 CRS 的 Polygon/MultiPolygon | `region_key/key` 和 `architecture_family/family` 必须非空；每个陆地省份最终得到一个 assignment |
| `--history` | CSV/TSV，或 GeoJSON/GPKG/SHP 的属性表；也可给目录 | 识别 `state_key/state`、`province_key/province`、`country_tag/owner/...`、`market_key/market`；目录优先找 `history_1836.*` |
| `--resources` | JSON 或 CSV/TSV | 每个已知州必须恰有一条；资源容量必须有限且 >= 0；JSON 使用 `states` 数组及每州 `resources` 对象 |
| `--dem` | Rasterio 可读的栅格 DEM，带可转换坐标信息 | 重投影至 EPSG:3857；陆地页不得完全无有效样本；局部 nodata 用最近有效样本填充 |

作者化 hub 的类型当前识别 `city`、`farm`、`mine`、`wood`、`port`；其他值回退为 Buildable。
未提供 hub 时，生成器会为每个州确定性补出这五类点。这能保证技术链路完整，但生产地图仍应提供经过审核的点位。

## 4. 世界范围、投影和分页要求

- 引擎与 `thunder_gis_compile.py` 统一默认采用墨卡托投影 (`mercator`, EPSG:3857)，并在 metadata 中写入 `mercator`。改用墨卡托投影确保了整个地图从世界包、分页系统、拾取到 GPU 顶点的完全正角（conformal）映射，避免因动态非正角投影转换而引入多次动态渲染通道与性能损耗。
- Mercator 纬度必须严格位于 +/-85.051129 度内；`--allow-polar-clip` 只会裁到该极限。
- `--bounds` 格式是 `min_lon,min_lat,max_lon,max_lat`；不提供时按全部多边形范围加小边距。
- `--horizontal-wrap` 要求显式 bounds 的经度跨度精确为 360 度，并要求显式海域层。
- 运行时页面固定为 128x128；默认每个 level-0 页面覆盖 64,000 米。
- `--page-world-size-m` 必须为正有限数；`--levels` 当前限制为 1..16。
- 每升一级，页面世界尺寸乘 2，页面数量按向上取整减半；每一级的整个矩形页族必须齐全。
- `page_origin_x/y` 当前生成器写 0；页坐标的实际世界原点由 `bounds_world_m` 决定。

若 `page-world-size-m` 大于 65,535 米，当前 16 位局部坐标格式无法表达 Living Map 放置坐标；生成器会
明确写入空的 PLC1/ANC1，而不会截断坐标。这种包可渲染地图，但没有可用的作者化 Living Map 放置数据。

## 5. 生成器必须产出的页面与二进制约定

每个 `(level, x, y, variant=0)` 必须同时存在以下四类页面：

| 块 | 固定尺寸 | 编码 |
|---|---:|---|
| `province_coast` | 65,536 B | 128x128 little-endian uint16 Province ID + 128x128 little-endian int16 coast SDF |
| `height` / `terrain_height` | 8,450 B | 65x65 little-endian uint16；`height_m = -12000 + q * 0.5` |
| `lake_mask` | 16,384 B | 128x128 uint8，值只能是 0 或 1 |
| `spatial_mask` | 16,384 B | 128x128 uint8，值只能是 0 或 1 |

省份页的编码 0 表示无 Province/水底，运行时 Province N 编码为 N+1。海域和湖泊多边形本身仍可拥有
非零 ProvinceId。海岸 SDF 负值为水、正值为陆地，量化单位 0.5 米，范围约 +/-16.38 km。

原始 AI 图片、带抗锯齿的边界图或 JPEG 不能直接作为 Province ID 图。必须先做离散 ID 量化、轮廓修复/
矢量化，再从确定的陆地几何烘焙距离场；当前官方生成器从多边形直接栅格化并用 EDT 生成 coast SDF。

## 6. `.thunderworld` 接受门槛

`thunder_world_compiler` 当前会强制验证：

1. manifest 至少含一个静态 `metadata 0 0 0 0` 块，且 header 与 metadata 的 `horizontal_wrap` 一致。
2. 必须出现这些块类型：国家、市场、州、省份、历史设置、邻接 offsets/neighbors、Area、Trade Province、
   Location、province/coast、height、lake mask、spatial mask、settlement anchors、河流、交通、建筑风格和资源分布。
3. metadata 声明的每一级、每个 x/y 页面都必须有完整的四件页面族，并且每个页面实际可解码。
4. Area/Trade Province/Location 三块要么全部存在，要么全部缺失；当前编译器要求全部存在。
5. 邻接 offsets 数量必须等于 `province_count + 1`，neighbor 引用有效，图必须对称；neighbor 总数有 2,000 万安全上限。
6. 资源分布必须覆盖所有州且州不可重复；建筑风格 assignment 的 Province/State 引用和 hash 必须有效。
7. 河流和交通块必须使用 level 0，并满足各自 RIV1/PTR1 payload 契约。
8. metadata 的国家/州/省份/海域/湖泊数量必须与定义块一致；Province/State/Country 上限受 16 位身份约束。
9. manifest chunk key `(type, level, x, y, variant)` 不得重复；索引排序、边界、checksum、build hash 和块尺寸受容器层校验。
10. 单个解压后 chunk 最大 64 MiB；需要 Zstd 的包只能由带 Zstd 支持的 Thunder 读取。

`placement_candidates` 目前不是 `thunder_world_compiler` 的硬性 required type，但官方 GIS 生成器一定会写入
PLC1（有记录或空记录）。生产地图应把它视为 Living Map 必需内容，而不是利用这个校验空隙省略。
`StringTable`、`BuildingDefinitions`、`PopDefinitions`、`ResourceAnchors` 当前属于已预留但不由官方 GIS
生成链路要求的 chunk 类型。

## 7. 推荐生成与验收流程

示例仅表达调用顺序，源文件名按实际项目替换：

```powershell
python tools/thunder_gis_compile.py `
  --provinces source/provinces.gpkg `
  --states source/states.gpkg `
  --seas source/seas.gpkg `
  --lakes source/lakes.gpkg `
  --rivers source/rivers.gpkg `
  --straits source/straits.csv `
  --hubs source/hubs.gpkg `
  --roads source/roads.gpkg `
  --rails source/rails.gpkg `
  --architecture-regions source/architecture.gpkg `
  --history source/history_1836.csv `
  --resources source/resources.json `
  --dem source/elevation.tif `
  --out build/world-source

build/dev-headless/thunder_world_compiler.exe `
  build/world-source/manifest.txt `
  build/world.thunderworld

build/dev-headless/thunder_world_inspect.exe build/world.thunderworld
```

验收应分四层记录，不能互相替代：

| 层级 | 最低证据 |
|---|---|
| 源数据 QA | CRS、几何有效性、key 唯一性、引用完整性、DEM nodata、海岸/日期变更线人工检查 |
| 生成器 QA | `compile_report.json` 数量合理，manifest 页数与 levels/bounds 一致，无默认数据意外混入生产包 |
| 包级 QA | `thunder_world_compiler` 成功回读验证，`thunder_world_inspect` 的 chunk 统计与 build hash 已保存 |
| 运行时/视觉 QA | 实际 bootstrap、相机跨页/缩放、水平环绕、拾取、海岸线、湖泊、高程、河流/交通及 Living Map 点位均实机检查 |

## 8. 当前生产缺口

- Python 工具依赖 `geopandas`、`numpy`、`pandas`、`rasterio`、`scipy`、`shapely`，仓库当前没有锁定的
  requirements/pyproject/conda 环境文件；可复现地图构建还需要补依赖锁定。
- 官方生成器当前只支持 Mercator；运行时的 Gall Stereographic 能力尚无对应官方产出链路。
- 缺省资源、自动 hub、零高程和空矢量块只保证格式完整，不能作为生产内容质量的证明。
- 包级校验没有把 `placement_candidates` 列为硬性 required type；生产检查应额外拒绝缺失或意外为空的放置数据。
- `thunder_world_inspect` 只展示容器和 chunk 统计，不替代 `WorldTopology` bootstrap 与实际渲染检查。

