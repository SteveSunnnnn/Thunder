# 1836 地图构建与验收记录

本记录区分已实现的引擎能力、当前预览数据和未完成的历史内容。当前产物是
`generated/world_map/world-1836-wiki-germany.thunderworld`，不是已完成历史核定的发布版。

## 当前资料约束

- 1836 版图和视觉只参考用户提供的 V3 Wiki 国家图与本机 Victoria 3；开局事实读取其 `common/history/states/00_states.txt`
  与 `map_data/state_regions`，参考安装 build ID 为 `25081502`。
- 两张本机 V3 截图的游戏日期分别是 1840 和 1879，仅作美术对照，不用于认定 1836 归属。
- Core 归属种子已撤下。`scenario_1836_rules.json` 是重新编写的规则，
  `ownership_1836.json` 由这些规则与公共 GIS 标识解析生成；旧 `update_ownership.py` 已替换为新入口。
- 构建脚本不读取 V3 安装目录，不导入其省份图、地形、贴图、模型或字体。
  几何和高程来自独立下载的 Natural Earth、GEBCO 及 BKG/geoBoundaries 数据，来源、许可与 SHA-256 保存在
  `data/geography/sources.lock.json` 和 `data/geography/germany_detail.lock.json`，并嵌入世界包元数据。

## 当前可验证的结果

预览包括 4,448 个陆地 Location、1 个海洋单位、1,355 个湖泊单位，四级共 1,490 组虚拟页。
87 条区域规则、国家默认规则及四个区域的独立细分配置表达 113 个国家身份，
2,956 个陆地 Location 已赋值；1,492 个仍未确定归属。
未确定地区显示为中性色，不能解读为真实的统一政治实体。

同名地名必须核对地理范围：阿根廷 Formosa 不等于 V3 的台湾 Formosa；阿根廷 Santa Cruz、
利比里亚 Maryland、津巴布韦 Midlands 以及现代贝宁国名也不能直接套用游戏中的同名州。
规则编译器对已识别的冲突执行范围校验，相关反例已加入 GIS 回归。

数据规则使用现代行政标识选择公共几何；这些标识不决定国家归属。混合归属地区通过
`explicit_unassigned` 阻止回落到整个现代国家的默认归属。已知仍有现代行政区整体赋值的
近似规则，具体限制记录在规则文件及 `scenario_report.json`，因此不能称为精确 1836 国界。

引擎和离线工具已接通：

- 双主权面约束切分、陆水裁剪、共边表、保护约束边的 Chaikin 平滑，以及真实城镇锚点校验。
  这些能力已在合成输入上端到端打包验证；完整全球 1836 约束面尚未制作。
- 物理岸线 JFA+1 距离场，128×128 int16 页，0.5 m/单位。
- 远景制图岸线距离层：128×128 int16，32 m/单位，约 1,048 km 范围。
  它由公共 GIS 栅格离线计算，专供海岸刻线，不改变物理海岸、拾取或通行数据。
- 国家颜色与整数 Country_ID 分离。同色国家仍可绘制边界；占领标志修改后下一次绘制可出现条纹。
- CPU 拾取在加载时准备最细级虚拟页；鼠标路径只有目录和像素查表，不解码文件、不做 GPU 回读。
  纯色页用一个目录项表示，当前预览的常驻拾取数据约 19.8 MiB。
- 原生 Vulkan 预览默认读取包内历史归属。诊断用现代州配色必须显式传入 `--synthetic-ownership`。
- 远景使用独立政治色、暖色海面、羽化边界、地形明暗、刻线和 MSDF 衬线国名。
  国名支持按主轴旋转、碰撞避让及指定本土标签区域。

## 二进制兼容说明

新增可选 chunk：

| 类型 | 数值 | 内容 |
|---|---:|---|
| `CountryPresentation` | 27 | `CPL1` + uint32 国家数量 + 按国家 ID 排列的 RGBA8 |
| `CartographicCoastPage` | 28 | 每页 32 KiB，little-endian int16，32 m/单位 |

旧包缺少制图距离层时使用保守回退；已包含该层的包必须具备完整页族，缺页或尺寸错误会被拒绝。
原有物理岸线量化保持不变。DEM 保留现有引擎编码：海平面码值 24000、0.5 m/单位，
可表示 −12000 至 20767.5 m；用户已授权解决原始 6144 码值无法覆盖海沟的冲突。

## 重建与检查

在仓库根目录运行：

```powershell
python scripts/world_map/fetch_public_sources.py
python scripts/world_map/prepare_world_geography.py
python scripts/world_map/build_scenario_rules.py
```

地理页已存在时，归属和文字调整不需要重烘焙地形：

```powershell
python scripts/world_map/compile_scenario.py --source generated/world_map/source --scenario data/geography/ownership_1836.json --base-chunks generated/world_map/preview_chunks --out generated/world_map/1836_chunks
build-vulkan/thunder_world_compiler.exe generated/world_map/1836_chunks/manifest.txt generated/world_map/world-1836-preview.thunderworld
```

首次生成制图距离层时，在地理页目录运行，再重新编译场景：

```powershell
python scripts/world_map/bake_cartography.py --chunks generated/world_map/preview_chunks
```

合成输入与回归：

```powershell
python -m unittest discover -s scripts/world_map -p test_world_geometry.py -v
python scripts/world_map/test_compile_pipeline.py --tools build
python -m cmake --build build
ctest --test-dir build --output-on-failure
```

原生截图和采样：

```powershell
build-vulkan/thunder_world_bench.exe --world generated/world_map/world-1836-preview.thunderworld --shaders build-vulkan/thunder-shaders --out generated/world_map/v3_style_europe --frames 240 --altitude 3000000 --u 0.525 --v 0.46 --validation
```

每个输出目录含 `map.bmp`、`timing.json`、`device.txt`；截图不计入定时区间。
`--exercise-politics` 会额外保存归属/占领改变后的下一帧截图。
`--shading-debug 10485760` 是 DEM 诊断，不能作为正常画面或性能验收结果。

## 尚未达到的验收项

1. 全部地区的 V3 1836 归属仍未核对完；德国小邦、印度土邦、外满洲北界、殖民地与原住民
   边界等需要独立历史约束几何，不能继续用整个现代行政区代替。
2. 当前全球 GPU 布局仍固定为 40×28 基础页。预览页约 8 km/像素，不能承载最终城市级近景。
   对预览包运行完整代表点验证曾报告 529 个拾取不匹配；这不是查表延迟问题，而是地块小于像素
   或代表点被粗栅格改变。严格约束编译已禁止静默丢弃最细栅格中的 Location。
3. 6 角分 GEBCO 是预览高程。更细 DEM、地形材质页、坡度/曲率资产与城市/港口实体仍需推进。
4. 目前海洋只有一个单位，尚未完成战略海区细分、完整港口连接和通航图。
5. `timing.json` 是当前负载下短时固定视角采样。平均帧率、GPU 时间和 CPU 录制提交时间分别记录，
   不能据此宣称完整内容、全视角、长期稳定的 120–144 FPS 已通过验收。

后续核验应保持三层证据分开：编译/链接，C++/GIS 回归，实机画面与帧时间。

## 2026-09-09 实测记录

Headless 和 Vulkan 构建中的 28 项 C++ 测试均通过；19 项 GIS/规则测试通过。
合成双国界案例通过原生打包和世界验证：4 个 Location、5 个页面、4 个代表点拾取正确。

当前 V3 对照预览在 RTX 4060 Laptop、2560×1440、High/4×MSAA、开启 Vulkan 验证时，
每个固定视角采样 240 帧，结果如下。CPU 提交列排除了等待呈现和帧栅栏的时间。

| 视角 | 平均 FPS | GPU 平均 ms | CPU 录制提交平均 ms | 帧间隔 P95 ms |
|---|---:|---:|---:|---:|
| 全球 | 179.23 | 3.74 | 0.263 | 7.63 |
| 欧洲 | 238.86 | 2.51 | 0.227 | 5.00 |

两次运行 Vulkan 验证错误均为 0。证据位于 `generated/world_map/v3_style_global/` 与
`generated/world_map/v3_style_europe/`。近/中景的上一组固定视角采样平均约 133/125 FPS，
但 P95 帧间隔仍超过 8.3 ms，不能据此宣称全视角稳定高刷验收完成。

当前截图及地图文件仍为预览。剩余 1,481 个未确定归属地块、历史边界精确切分、
更细的城市级地形与海运拓扑均未完成。

## Wiki 参考地块修订（2026-09-09）

采用用户提供的 [Victoria 3 Wiki](https://vic3.paradoxwikis.com/Victoria_3_Wiki) 世界国家图作为版图参考。
该图只用于人工核对，不作为纹理、像素蒙版或商业地图几何进入构建。

本批将德国西南部原有的 `location.deu.1573` 拆为 25 个城市及腹地 Location，陆地总数由
4,351 增至 4,375。新边线来自独立分组的公开 BKG/geoBoundaries 区县多边形。
重复区名记录包含岛屿和飞地，现先合并所有同名记录再裁剪，避免只保留最后一条记录而丢失大陆部分。
拆分保留原区域外边界；输出覆盖差和重叠面积均为 0 m²，浮点计算容差为 0.001 m²。

这仍是地区级历史边界近似。巴登、符腾堡和霍亨索伦的归属已单独表达，但霍亨索伦等边界仍需继续细切。
Main-Tauber、Schwarzwald-Baar 和 Bodensee 的混合归属暂不强行赋值。
不能将本批结果称为完整全球 1836 历史约束面。

构建实际重写 Location 定义、层级、CSR 邻接与全部 LOD 页。新地块的 56 条内部共边，以及
原区域与外围的 13 组有效邻接全部在 CSR 中双向存在。坐标投影往返造成的微小裂隙/重叠现通过
5 cm 的邻接判定容差处理；只接触一个点、超过容差的间隙与明显重叠不会被当作陆地共边。
对应回归涵盖重复记录、遗漏边角、重复分配、失效归属键和邻接数值误差。

公开区县来源及许可、校验值见 `data/geography/germany_detail.lock.json`。
署名：© GeoBasis-DE / BKG (2021)，经 geoBoundaries gbOpen 提供，dl-de/by-2-0。
独立的 Location 分组与近似归属见 `data/geography/wiki_southwest_germany.json`。

本批可复现命令：

```powershell
python scripts/world_map/fetch_germany_detail.py
python scripts/world_map/build_scenario_rules.py
python scripts/world_map/refine_wiki_geography.py
python scripts/world_map/thunder_gis_compile.py --provinces generated/world_map/wiki_source/locations.gpkg --states generated/world_map/wiki_source/provinces.gpkg --seas generated/world_map/wiki_source/seas.gpkg --lakes generated/world_map/wiki_source/lakes.gpkg --rivers generated/world_map/wiki_source/rivers.gpkg --settlements generated/world_map/wiki_source/settlements.gpkg --roads generated/world_map/wiki_source/roads.gpkg --rails generated/world_map/wiki_source/railroads.gpkg --architecture-regions generated/world_map/wiki_source/architecture_regions.gpkg --dem generated/world_map/wiki_source/elevation.tif --source-catalog data/geography/sources.lock.json --bounds=-180,-60,180,85 --horizontal-wrap --allow-polar-clip --page-world-size-m 1024000 --levels 4 --out generated/world_map/wiki_chunks
python scripts/world_map/bake_cartography.py --chunks generated/world_map/wiki_chunks
python scripts/world_map/compile_scenario.py --source generated/world_map/wiki_source --scenario data/geography/ownership_1836.json --base-chunks generated/world_map/wiki_chunks --location-overrides generated/world_map/wiki_source/ownership_overrides.json --out generated/world_map/wiki_1836_chunks
build-vulkan/thunder_world_compiler.exe generated/world_map/wiki_1836_chunks/manifest.txt generated/world_map/world-1836-wiki.thunderworld
python scripts/world_map/audit_wiki_refinement.py
build-vulkan/thunder_world_validate.exe generated/world_map/world-1836-wiki.thunderworld --location-prefix loc_deu_
```

验证器的 `--location-prefix` 只限定代表点拾取的检查范围，仍检查全包层级、CSR 对称性与每页解码；
报告会明确写出范围。默认无筛选时仍检查所有 Location，不能用区域通过结果代替全球拾取验收。

本批最终产物：`generated/world_map/world-1836-wiki.thunderworld`，234,483,200 字节，
build hash `0x8ed4cf13b9b11fc3`。包内含 107 个国家标识、4,375 个陆地 Location、
1 个海洋单位、1,355 个湖泊，共 5,731 个 Location、1,490 页、30,408 条有向邻接。
已赋值 2,892 个陆地 Location，未确定归属 1,483 个；后者较旧包增加 2，是因为原先一个未知区域
细分后仍有三个混合地区未确定归属，不能据此误解为历史范围已经覆盖全部国家。

验证结果：

- 28 项 C++ 测试、25 项 GIS 测试通过；合成约束案例原生打包与完整拾取验证通过。
- 新包全页解码、全包层级和 CSR 对称性通过，25 个新 Location 的原生代表点拾取全部通过。
- 全局完整代表点检查仍失败 529 处；直接统计旧包与新包 L0 栅格，两者均有 314 个微小单位未占据像素。
  本批没有新增消失的西南德国地块，也没有解决全球粗栅格问题。
- 国名轴线现在裁剪到所属国家内部的连续线段，修复了符腾堡文字跨越霍亨索伦的问题。

同机 2560×1440、High/4×MSAA、开启 Vulkan 验证，固定视角各采样 240 帧：

| 视角 | 平均 FPS | GPU 平均 ms | CPU 录制提交平均 ms | 帧间隔 P95 ms |
|---|---:|---:|---:|---:|
| 全球 | 173.93 | 3.80 | 0.282 | 7.71 |
| 德国西南部 | 309.46 | 1.49 | 0.221 | 3.97 |

两次运行 Vulkan 验证错误为 0；仍各有 3 条未使用顶点输出接口的警告。
这些是固定视角短时采样，不代表全内容高刷验收完成。
实际截图与原始计时分别在 `generated/world_map/wiki_global_render/`、
`generated/world_map/wiki_southwest_render/`，几何前后对照和 CSR 审计在 `generated/world_map/wiki_review/`。

## 德国北部与中部扩展（2026-09-09）

继西南部之后，将下萨克森、黑森和莱茵兰—普法尔茨加入同一批构建。原有四个粗 Location
现在由 101 个独立多边形覆盖，增加 97 个陆地单位；上一批 25 个西南部单位包含在这 101 个中。

| 原区域 | 新 Location 数 | 暂未定归属数 |
|---|---:|---:|
| 巴登—符腾堡 | 25 | 3 |
| 下萨克森 | 31 | 5 |
| 黑森 | 22 | 5 |
| 莱茵兰—普法尔茨 | 23 | 1 |

新增单独归属包括奥尔登堡、不伦瑞克、法兰克福、黑森—达姆施塔特、黑森—卡塞尔和拿骚。
普法尔茨部分作为巴伐利亚领土单独表达，美因茨和沃尔姆斯表达黑森归属。
归属范围仍为公共区县边界近似；Waldeck-Frankenberg、Marburg-Biedenkopf、Lahn-Dill、
Pyrmont、Schaumburg、Birkenfeld 等混合地区没有强行按现代整个区县赋给一国。

进一步修复：

- 细分地块重新按真实海洋岸线计算沿海属性，避免汉诺威、不伦瑞克等内陆地块继承父区域的沿海标记。
  101 个地块的 GIS 沿海值与运行时 Province/Location 定义均已对照。
- 细分单位的几何代表点使用最大内部净空位置，计算精度为 100 m；该点用于地块聚焦，独立于真实城镇坐标。
  德尔门霍斯特的原默认代表点受 8 km 栅格采样边缘影响，改用内部代表点后拾取正确。
  没有缩小、吞并或删除这个地块。全球原有 529 处代表点失败仍照常报告。
- 场景编译会同步生成几何代表点，检查 Area/Province 父级定义未改变，再复用已经重建的栅格。

最终包为 `generated/world_map/world-1836-wiki-germany.thunderworld`，234,514,432 字节，
build hash `0xbb885eb2c6b1e5ea`。共 5,804 个 Location、273 个状态定义、274 个交易省份、
113 个国家身份和 30,932 条有向邻接。

验证：28 项 C++、27 项 GIS 回归通过，合成端到端约束案例通过。
写出再读取的四个区域覆盖差仍为 0 m²；243 条细分地块共边、41 组原区域邻接关系通过双向检查。
最终包全页解码和层级检查通过，101 个细分地块的原生拾取全部通过。
全局完整代表点检查仍有 529 处失败，最细栅格仍未表达 314 个微小单位，全球验收尚未完成。

当前两张原生截图在 `generated/world_map/wiki_germany_render/` 和
`generated/world_map/wiki_hesse_render/`。2560×1440、High/4×MSAA、Vulkan 验证开启，各 240 帧：

| 视角 | 平均 FPS | GPU 平均 ms | CPU 录制提交平均 ms | 帧间隔 P95 ms |
|---|---:|---:|---:|---:|
| 德国区域 | 272.88 | 1.70 | 0.224 | 4.74 |
| 黑森 | 296.93 | 1.51 | 0.206 | 4.26 |

两次验证错误均为 0，仍有前述未使用顶点输出警告。性能仅为这些固定视角的短时样本。
最新几何对照与拓扑审计位于 `generated/world_map/wiki_germany_review/`。

重建时使用四个配置，并输出到本批目录：

```powershell
python scripts/world_map/build_scenario_rules.py
python scripts/world_map/refine_wiki_geography.py --spec data/geography/wiki_southwest_germany.json data/geography/wiki_lower_saxony.json data/geography/wiki_hesse.json data/geography/wiki_rhineland_palatinate.json --out generated/world_map/wiki_germany_source
```

之后按上一节完整 GIS 命令，将输入目录替换为 `wiki_germany_source`、地理页输出替换为
`wiki_germany_chunks`，烘焙制图岸线，再执行：

```powershell
python scripts/world_map/compile_scenario.py --source generated/world_map/wiki_germany_source --scenario data/geography/ownership_1836.json --base-chunks generated/world_map/wiki_germany_chunks --location-overrides generated/world_map/wiki_germany_source/ownership_overrides.json --out generated/world_map/wiki_germany_1836_chunks
build-vulkan/thunder_world_compiler.exe generated/world_map/wiki_germany_1836_chunks/manifest.txt generated/world_map/world-1836-wiki-germany.thunderworld
build-vulkan/thunder_world_validate.exe generated/world_map/world-1836-wiki-germany.thunderworld --location-prefix loc_deu_
python scripts/world_map/audit_wiki_refinement.py --source generated/world_map/wiki_germany_source --chunks generated/world_map/wiki_germany_chunks --out generated/world_map/wiki_germany_review
```
