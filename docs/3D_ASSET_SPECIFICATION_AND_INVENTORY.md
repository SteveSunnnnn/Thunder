# Thunder 引擎 3D 资产格式规范与完整资产清单（Victoria 3 大战略规格）

> **文档状态**：生产就绪标准（Production-Ready Spec）  
> **适用版本**：Thunder 1.0+ (Vulkan 1.3 / GPU-Driven Pipeline / 墨卡托 EPSG:3857)  
> **核心对标**：Victoria 3 风格 19 世纪（1836–1936）工业革命、大国博弈、金融深化与全球活体地图（Living Map）

---

## 目录
1. [技术架构与格式规范 (Why & How)](#1-技术架构与格式规范)
2. [几何与材质性能预算标准 (LOD 0–3)](#2-几何与材质性能预算标准)
3. [区域文化建筑风格套件清单 (Architecture Kits)](#3-区域文化建筑风格套件清单)
4. [重工业、资源采掘与农业资产清单](#4-重工业资源采掘与农业资产清单)
5. [交通运输与物流基础设施资产清单](#5-交通运输与物流基础设施资产清单)
6. [海运船队与海军战舰资产清单 (1836–1936)](#6-海运船队与海军战舰资产清单)
7. [陆军部队、火炮与战场模型清单](#7-陆军部队火炮与战场模型清单)
8. [历史奇观与全球地标模型清单 (World Wonders)](#8-历史奇观与全球地标模型清单)
9. [自然植被与环境装饰要素清单 (Biomes)](#9-自然植被与环境装饰要素清单)
10. [战略沙盘与桌面微缩战术道具清单 (Tabletop Props)](#10-战略沙盘与桌面微缩战术道具清单)

---

## 1. 技术架构与格式规范

大战略游戏（Grand Strategy Game）的 3D 资产渲染与传统 RPG/FPS 存在本质差异：
- **同屏实体极多**：同一视野下可能出现数千栋城市建筑、数百处农场矿井、几十列穿行火车与海上航运船队。
- **超大跨度平滑缩放**：相机必须在 800 km 轨道战略视角（远景平面饱和政治地图 + 嵌入式雕版衬线国名）至 1–60 km 近景战术地貌（3D 地形、微表面物理水体、PBR 建筑群、动态旗帜与蒸汽烟尘）之间无级平滑过渡。
- **墨卡托正角投影（Mercator EPSG:3857）**：Thunder 引擎采用正角墨卡托投影，确保局部几何形态在任何缩放级别下完全保角不失真，并消除非正角动态转换的二次渲染开销。

### 1.1 资产交付源格式：glTF 2.0 Binary (`.glb`)
美术团队统一采用 **glTF 2.0 Binary (`.glb`)** 作为标准 DCC 交付格式（Blender / Maya / 3ds Max / Houdini / Substance 导出）。
- **单文件自包含**：几何体、骨骼层级、动画关键帧、PBR 基础因子打包于单个二进制文件，避免相对路径材质丢失。
- **几何压缩**：源文件可启用 Draco 几何压缩或 Meshopt 优化管线，降低仓库存储体积。

### 1.2 运行时烘焙格式：`.thunderasset` (AssetPack) 与 `.architecture` (ArchitectureKit)
游戏发布时不直接解析 `.glb`，由离线工具链 `thunder_asset_cooker` 与 `thunder_architecture_cooker` 烘焙为 Thunder 引擎原生紧凑格式：
- **浮动原点与块级局部坐标（64 km Chunk Local）**：
  世界空间以 64 km 分块，模型顶点位置使用块内局部坐标存储，彻底解决全球尺度下 32 位浮点数抖动问题。
- **量化顶点属性（Quantized Vertex Layout）**：
  - `Position`: 3 × float32（局部坐标）或 3 × int16 量化。
  - `Normal & Tangent`: 八面体编码（Octahedral Normal Encoding, 2 × int8 或 10-10-10-2），将原本 24 字节的空间向量压缩至 4 字节。
  - `TexCoord (UV)`: 2 × float16 半精度浮点数，满足绝大多数 UV 精度同时节省 50% 显存。
- **网格簇（Meshlets / GPU-Driven）**：
  烘焙阶段将模型拆分为 64 顶点 / 126 三角形的网格簇，生成外接球包围盒与锥体朝向，供 Vulkan 1.3 Task/Mesh Shader 实现极致的 GPU 硬件级视锥剔除与微表面背面剔除。

#### 1.2.1 引擎烘焙工具链与清单规则 (Toolchain Pipeline)
Thunder 引擎提供三大标准烘焙工具，与 CI/CD 自动化美术管线对接：
1. **通用资产封包 (`thunder_asset_cooker`)**：
   - 命令：`thunder_asset_cooker <manifest.txt> <out.thunderasset>`
   - 清单格式：`<kind> <lod> <asset_key> <source_file>`
   - 示例：
     ```text
     mesh 0 arch_we_res_rich_01 models/architecture/we_res_rich_lod0.glb
     mesh 1 arch_we_res_rich_01 models/architecture/we_res_rich_lod1.glb
     texture 0 tex_we_brick_orm textures/we_brick_orm.ktx2
     material 0 mat_we_brick materials/we_brick.thundermat
     ```
2. **建筑套件语义数据库 (`thunder_architecture_cooker`)**：
   - 命令：`thunder_architecture_cooker <manifest.txt> <out.thunderarch>`
   - 清单格式：`<kind> <year_min> <year_max> <wealth_min_milli> <wealth_max_milli> <weight> <mesh_key> <material_key> <max_lod>`
   - 示例：
     ```text
     residential 1836 1936 0 20000 10 arch_we_res_poor_01 mat_we_brick 3
     residential 1836 1936 20000 70000 10 arch_we_res_mid_01 mat_we_stucco 3
     residential 1836 1936 70000 100000 5 arch_we_res_rich_01 mat_we_mansard 3
     factory 1850 1936 0 100000 8 ind_steel_mill_01 mat_steel_works 3
     port 1836 1936 0 100000 6 arch_we_port_01 mat_harbor_wood 3
     ```
3. **PBR 材质定义编译器 (`thunder_material_cooker`)**：
   - 命令：`thunder_material_cooker <material.ini> <out.thundermat>`
   - 包含 BaseColor、Roughness、Metallic、NormalScale 及环境雪层覆盖标记 (`ReceivesSnow`)。

### 1.3 纹理格式与压缩标准：KTX2 (Basis Universal)
- **跨硬件即时转码**：统一输出 KTX2 容器，采用 Basis Universal（UASTC 模式保证法线与高光质量），显卡加载时毫秒级无损转码为桌面级 `BC7`（Nvidia/AMD/Intel）或移动端 `ASTC`。
- **通道打包规范（ORM Texture）**：
  严禁为每个参数分配独立贴图。统一采用三合一 ORM 贴图：
  - **R 通道**：环境光遮蔽（Ambient Occlusion, AO）
  - **G 通道**：粗糙度（Roughness, 0.0=光滑镜面，1.0=极度粗糙）
  - **B 通道**：金属性（Metallic, 0.0=非金属绝缘体，1.0=纯金属导体）
  - **A 通道（可选）**：局部微自发光或细节遮罩（Emissive / Dirt Mask）
- **法线贴图（Normal Map）**：
  采用两通道切线空间法线（BC5 / RG8_SNORM），$Z$ 轴分量在着色器中通过 $\sqrt{1 - X^2 - Y^2}$ 动态还原，节省 50% 法线贴图显存并避免压缩伪影。

### 1.4 动画与海量实例机制 (Mass Instancing)
- **英雄与高精检视模型**：glTF 骨骼动画，单顶点最多受 4 根骨骼影响（uint8 索引 + uint8 权重）。
- **大军团与活体交通（士兵队列、铁路线路、海运商船）**：
  严禁对成千上万个同屏单位执行 CPU 逐帧骨骼矩阵变换。采用 **顶点动画纹理（Vertex Animation Texture, VAT）** 或 **GPU 实例矩阵缓冲区（Instanced Transform Ring Buffer）**，着色器直接根据实体时间偏移量在 GPU 顶点着色器中执行形变，实现 10,000+ 单位同屏稳定 120 FPS。

---

## 2. 几何与材质性能预算标准

| 资产层级 | 视距/高度 | 三角面预算 (Tris) | 贴图分辨率 (PBR) | 适用场景与技术细节 |
|---|---|---|---|---|
| **LOD 0** | 0 – 5 km (贴地特写) | 8,000 – 35,000 | 2048×2048 (ORM) | 地标奇观、主力战列舰、特写镜头检视模型、精细火车头 |
| **LOD 1** | 5 – 20 km (低空战术) | 2,000 – 8,000 | 1024×1024 (ORM) | 近景城市标准建筑群、工业厂房、近海巡洋舰、步兵方阵 |
| **LOD 2** | 20 – 80 km (区域中景) | 300 – 1,500 | 512×512 (合辑图集) | 区域级群落、大范围农庄网络、缩减至外轮廓的交通工具 |
| **LOD 3** | 80 km+ (全球战略) | 2 – 50 (广告牌/簇) | 128×128 或无网格 | 远景城市光斑/烟火微粒，平滑溶解至 2D 平面雕版地图标签 |

---

## 3. 区域文化建筑风格套件清单 (Architecture Kits)

每个文化套件包含完整的 6 类功能语义建筑（`Residential`, `Commercial`, `Factory`, `Farm`, `Mine`, `Port`），并细分 **贫困 (Poor)**、**中产 (Middle)**、**富裕 (Wealthy)** 三档财富外观，以及 **1836 初阶** 与 **1900+ 工业成熟期** 时代演进。

### 3.1 西欧/维多利亚古典套件 (Western European - Victorian)
- `arch_we_res_poor_01`: 伦敦东区/工业城排屋（红砖、无檐、煤烟熏黑砖墙、多烟囱排管）。
- `arch_we_res_mid_01`: 乔治亚/维多利亚半独立双拼住宅（白色灰泥门廊、铁艺栏杆、石板坡屋顶）。
- `arch_we_res_rich_01`: 伦敦皮卡迪利/巴黎奥斯曼风格豪华联排府邸（法式孟莎式屋顶、老虎窗、铸铁雕花阳台、爱奥尼柱式装饰）。
- `arch_we_com_bank_01`: 维多利亚帝国银行大楼（科林斯巨柱门廊、青铜浮雕双开大门、高耸钟楼、大理石阶梯）。
- `arch_we_fac_textile_01`: 曼彻斯特大型纺织机械厂（连排锯齿状采光天窗、高耸红砖排烟大烟囱、附带动力机房锅炉间）。
- `arch_we_train_station_01`: 圣潘克拉斯式火车总站（锻铁与拱形曲面玻璃铸造的天幕大厅、哥特复兴钟楼大门）。

### 3.2 东亚/大清与明治套件 (East Asian - Qing & Meiji)
- `arch_ea_res_poor_01`: 北方夯土灰瓦四合院 / 江南青砖黛瓦水乡民居（马头墙、木格花窗）。
- `arch_ea_res_mid_01`: 传统三进深宅大院 / 明治开埠式和洋折衷町屋（双层黑瓦、黑漆木梁架、前店后宅）。
- `arch_ea_res_rich_01`: 显赫官宦缙绅总督府第（飞檐斗拱、朱红立柱、彩画额枋、琉璃剪边屋脊）。
- `arch_ea_com_guild_01`: 传统钱庄票号与会馆（重檐歇山顶、精美砖雕照壁、高悬金字招牌金匾）。
- `arch_ea_fac_arsenal_01`: 洋务运动制造局 / 八幡制铁所早期厂房（中西合璧式灰砖承重墙、高耸铁烟囱、蒸汽机抽水泵站）。
- `arch_ea_port_customs_01`: 通商口岸海关大楼与码头货栈（英国殖民外廊式殖民券柱结合中式歇山顶）。

### 3.3 东欧/斯拉夫帝国套件 (Eastern European - Slavic Imperial)
- `arch_ee_res_poor_01`: 乡村原木雕花木屋（Izba，粗原木搭接、带雕花木窗框、铁皮/茅草顶）。
- `arch_ee_res_mid_01`: 莫斯科/圣彼得堡黄白粉刷古典联排住宅（石膏浮雕、古典门楣、带内院拱券）。
- `arch_ee_res_rich_01`: 俄皇贵族宫殿（巴洛克/帝国式黄色外墙、白色双立柱、双头鹰金饰浮雕）。
- `arch_ee_cathedral_01`: 正教会大教堂（金/青色洋葱头穹顶群、东正教三杠十字架、白色厚实石砌主殿）。
- `arch_ee_fac_foundry_01`: 乌拉尔重型铸造冶炼工场（重型石块基座、木制高架起重滑道、冒烟高炉）。

### 3.4 中东/奥斯曼与波斯套件 (Middle Eastern - Ottoman & Persian)
- `arch_me_res_poor_01`: 浅黄夯土平顶民居（泥草墙面、木制突出横梁、小采光通风孔）。
- `arch_me_res_mid_01`: 伊斯坦布尔博斯普鲁斯木构海滨大宅（Yalı，悬挑木质凸窗 Mashrabiya、红褐漆面）。
- `arch_me_res_rich_01`: 帕夏豪华总督宫邸（宏大白色大理石券廊、室内喷泉中庭、镀金雕花铁栅栏）。
- `arch_me_bazaar_01`: 传统带拱顶大巴扎市场（连绵多拱圆顶网络、马赛克花饰拱门入场通道）。
- `arch_me_mosque_01`: 帝国大清真寺（铅皮大半球主穹顶、四座尖细铅笔状宣礼塔、大理石回廊）。

### 3.5 北美/殖民与边疆开拓套件 (North American - Frontier & Metropolis)
- `arch_na_res_frontier_01`: 边疆伐木拓荒者木屋（粗砍原木搭砌、外挂石块烟囱、木栅栏）。
- `arch_na_res_mid_01`: 美式带回廊木结构独栋别墅（白色护墙板、前置宽大木游廊、摇椅与双坡顶）。
- `arch_na_res_rich_01`: 镀金时代纽约第五大道豪宅（布杂艺术 Beaux-Arts 风格、切石砌体、大理石浮雕）。
- `arch_na_com_skyscraper_01`: 芝加哥学派早期钢结构摩天大楼（外墙三段式石砌、平顶、大型玻璃采光窗网格）。
- `arch_na_grain_elevator_01`: 中西部铁路沿线木制巨型筒仓谷物提升机（红漆木板高耸结构、传送皮带悬桥）。

### 3.6 拉丁美洲/伊比利亚殖民套件 (Latin American - Colonial Baroque)
- `arch_la_res_mid_01`: 西班牙式庭院大宅（红陶筒瓦、粉刷灰泥黄白墙体、带喷泉中央绿化天井）。
- `arch_la_hacienda_01`: 大种植园主庄园主楼（带柱廊双层骑楼、马厩群、家族礼拜堂）。

---

## 4. 重工业、资源采掘与农业资产清单

Victoria 3 的核心魅力在于工业化进程的可视化呈现。

### 4.1 重工业厂房套件
- `ind_steel_mill_01`: 现代化钢铁冶炼厂（带 3 座热风炉高炉、铁水倒罐车间、冒出阵阵黑烟与赤红火光的烟囱）。
- `ind_foundry_machine_01`: 重型机械与机车制造厂（带天窗的大型钢架车间、户外龙门起重机轨道）。
- `ind_chemical_works_01`: 硫酸与化肥化工厂（连排铅室反应塔、密集管道网、冷凝玻璃蒸馏塔）。
- `ind_munitions_factory_01`: 皇家兵工厂与弹药厂（厚实防爆红砖护墙、高耸铁丝网、带火车站台）。
- `ind_glassworks_01`: 玻璃制造窑厂（巨大穹顶耐火砖窑室、高烟囱与装箱货场）。
- `ind_brewery_distillery_01`: 工业化啤酒与蒸馏酿酒厂（铜制发酵巨型圆罐露天排列、麦芽烘干塔）。

### 4.2 资源采掘与能源开采
- `mine_coal_headframe_01`: 煤矿竖井井架（带旋转钢缆提升轮的巨型 A 字形钢/木桁架井架、洗煤楼、煤渣矸石山）。
- `mine_iron_openpit_01`: 露天铁矿阶梯采掘场（螺旋下沉的梯田状红褐色采掘工作面、采矿轨道小推车）。
- `mine_gold_panning_01`: 淘金营地（木制导水洗金水槽水槽长龙、矿工简易帐篷群、水轮碎矿机）。
- `res_oil_derrick_wood_01`: 19 世纪早期木制钻油井架（宾夕法尼亚式四脚木塔、缆索冲击钻工具房）。
- `res_oil_pumpjack_01`: 经典抽油机“磕头机”（带旋转配重块与往复上下摆动的机械驴头，配带平滑循环动画）。
- `res_logging_camp_01`: 森林伐木营地与水力锯木厂（木料翻滚滑道、露天原木堆放垛、带水车动力的圆盘锯棚）。

### 4.3 农业、种植园与乡村
- `farm_wheat_windmill_01`: 传统麦田磨坊（带风帆旋转动画的四叶荷兰式风车、小麦捆垛打谷场）。
- `farm_cotton_plantation_01`: 棉花种植园（白色棉铃田垄、扎棉除籽机械棚房、大包棉花装车场）。
- `farm_sugarcane_mill_01`: 甘蔗种植园与压榨糖厂（带蒸汽压榨机的高烟囱小糖厂、糖蜜收集桶池）。
- `farm_cattle_ranch_01`: 大牧场牲畜围栏（木质圆形围栏 corral、牛群聚集饮水槽、干草风干棚）。
- `farm_vineyard_estate_01`: 葡萄庄园（沿等高线排列整齐的葡萄藤架长廊、地下酒窖石质入口拱门）。

---

## 5. 交通运输与物流基础设施资产清单

### 5.1 铁路机车与车辆 (Locomotives & Rolling Stock)
- `train_loco_early_440_01`: 1840–1860 早期 4-4-0“美国人”型蒸汽机车（带巨大的倒圆锥形火花熄灭烟囱、黄铜大型车前汽灯、木板制排障器 Cowcatcher、外露往复连杆曲柄）。
- `train_loco_mid_consolidation_01`: 1870–1890 经典 2-8-0 强力货运蒸汽机车（高压直筒细烟囱、圆柱形蒸汽室锅炉、黑色钢铁涂装、双侧脚踏走廊）。
- `train_loco_late_pacific_01`: 1900–1920 4-6-2“太平洋”高速客运蒸汽机车（流线型车头盖、巨大的 6 只主驱动轮、带两轴四轮煤水车）。
- `train_car_passenger_wood_01`: 19 世纪木质车厢（双坡顶采光通风窗小天窗、带有铁艺扶手的开放式两端露天乘降平台）。
- `train_car_boxcar_freight_01`: 木板封闭式货运车厢（两扇中开滑拉门、车顶制动手闸手轮、用于运输日用工业品）。
- `train_car_coal_hopper_01`: 漏斗煤炭散货车（露天斜壁车厢、满载有凹凸起伏的黑色无烟煤块）。
- `train_car_oil_tanker_01`: 早期铆接卧式圆柱形铁制油罐车。

### 5.2 铁道与桥梁工程
- `rail_track_ballast_spline`: 标准轨距有碴道床样条线段（深色防腐枕木、碎石道碴断面、金属双轨带微弱反光）。
- `rail_switch_turnout_01`: 铁道单开道岔模型（带带有黄色指示牌的机械手扳道岔连杆箱）。
- `rail_water_tower_01`: 蒸汽机车站加水塔（圆形杉木水桶、铁箍抱匝、带铰链加水水鹤软喉管道）。
- `bridge_stone_viaduct_01`: 宏伟多跨石砌连拱高架铁路桥（罗马式圆拱桥墩、粗面玄武岩琢石护壁）。
- `bridge_iron_truss_01`: 锻铁桁架钢梁铁路桥（普拉特 Pratt 简支桁架、精密铆钉板接头网格）。

### 5.3 航道与公路工程
- `infra_canal_lock_01`: 运河船闸闸室（花岗岩渠壁、带人字形可开合木制/铁制平衡梁双扇闸门、泄水闸阀控制转盘）。
- `infra_lighthouse_fresnel_01`: 经典海岸砖石灯塔（黑白相间螺旋条纹圆塔、顶部玻璃灯室、内置带旋转光束的菲涅耳透镜透镜灯机）。
- `infra_telegraph_pole_01`: 电报电柱单体（木质十字横担杆、带白色陶瓷绝缘子瓷瓶、悬挂极细铜导线）。

---

## 6. 海运船队与海军战舰资产清单 (1836–1936)

### 6.1 风帆时代的余晖 (1836–1850s)
- `ship_sail_line_1st_rate`: 一级风帆战列舰（100+ 门加农炮、三层完整火炮甲板、雄伟的三桅全帆装索具、雕梁画栋的巴洛克式舰尾金楼）。
- `ship_sail_frigate_36`: 快速巡航风帆巡防舰（36 门火炮、单层主炮甲板、流线型飞剪式瘦削船艏）。
- `ship_sail_clipper_merchant`: 著名运茶飞剪式商船（极度轻盈狭长的木质复合船体、云朵般的满帆索具、飞速劈波航行）。

### 6.2 铁甲舰与蒸汽过渡时代 (1860s–1880s)
- `ship_ironclad_casemate_01`: 装甲斜壁炮室铁甲舰（如 CSS Virginia / 勇士号 Warrior，重型斜面熟铁装甲盒、带蒸汽动力冲角艏、帆柱辅助索具）。
- `ship_ironclad_monitor_01`: 浅吃水双转塔浅水重炮舰（极低干舷几乎没入水中、甲板中央矗立两座旋转圆柱铁甲炮塔）。
- `ship_steam_paddle_frigate`: 明轮蒸汽巡防舰（舰体两侧带有巨大的半圆形木质/铁质明轮护罩、居中烟囱冒着白烟）。

### 6.3 前无畏舰与工业大舰队 (1890s–1905)
- `ship_predreadnought_battleship_01`: 经典前无畏战列舰（如英国“君权”级 / 日本“三笠”号，前后各一座双联装 12 英寸主炮塔、两舷排列多门二级副炮廊与小口径速射炮、前后双军用桅杆并列双烟囱）。
- `ship_cruiser_armored_01`: 一等装甲巡洋舰（修长高干舷、多达 4 根垂直细烟囱、用于全球贸易航线保护与破交袭击）。
- `ship_destroyer_early_01`: 早期鱼雷快艇驱逐舰（“龟背”式防浪艏甲板、露天鱼雷发射管、高速疾驰）。
- `ship_merchant_tramp_steamer`: 维多利亚晚期通用远洋货轮（“客货两用船”，四支起重吊杆、居中长烟囱、黑色船壳带红色防污漆底水线）。
- `ship_ocean_liner_atlantic_01`: 大西洋豪华客轮（黑白双色优雅船体、红黑并列四烟囱、多层甲板散步长廊）。

### 6.4 无畏舰与近现代海军黎明 (1906–1920s)
- `ship_dreadnought_dreadnought_01`: 划时代“无畏”号战列舰（All-Big-Gun 单一口径全重炮布局、5 座双联装 305mm 主炮、重型三脚桅杆）。
- `ship_battlecruiser_speed_01`: 战列巡洋舰（战列舰主炮口径结合巡洋舰高速修长船形、高航速尾流浪花）。
- `ship_submarine_ww1_u_boat`: 早期柴电双壳潜艇（小甲板炮、通气管、双轴螺旋桨推进器）。

---

## 7. 陆军部队、火炮与战场模型清单

陆军在大战略中既需要在区域战线（Frontline）显示精细交战方阵，又需要在俯瞰视角显示微缩棋子。

### 7.1 列强步兵军团 (Infantry Platoons with Cultural Uniforms)
- `unit_infantry_british_redcoat`: 英国红衫军近卫步兵（猩红色羊毛制服、深蓝立领与袖口、高耸黑色熊皮帽或白色热带遮阳髓盔、持恩菲尔德线膛步枪带刺刀）。
- `unit_infantry_french_line`: 法国线列步兵（经典深蓝双排扣制服大衣、红军裤、红色平顶圆筒军帽 Kepi、持夏斯波 Chassepot 栓式步枪）。
- `unit_infantry_prussian_pickelhaube`: 普鲁士/德意志陆军（普鲁士蓝/野战灰外套、铜顶针刺皮头盔 Pickelhaube、毛瑟 1871 / Gewehr 98 步枪）。
- `unit_infantry_qing_green_standard`: 大清绿营/淮军勇营步兵（对襟团花号衣、宽腿行裤、红缨凉帽/黑色头巾、手持前装鸟枪或抬枪）。
- `unit_infantry_russian_tsarist`: 沙俄哥萨克与野战步兵（深绿粗呢长罩袍大衣、黑色羔皮高帽 Papakha、持莫辛-纳甘 1891 步枪）。
- `unit_infantry_us_civil_war_union`: 美国内战联邦北方军步兵（深蓝色软呢军帽、浅天蓝军裤、斯普林菲尔德前装线膛枪）。

### 7.2 骑兵部队 (Cavalry Formations)
- `unit_cavalry_hussar_01`: 匈牙利/欧洲经典骠骑兵（绣花紧身骑兵裤、披在肩上的短斗篷 Pelisse、带羽饰圆筒军帽、挥舞弧形马刀）。
- `unit_cavalry_lancer_uhlan_01`: 乌兰枪骑兵（四角软帽 Czapka、双手握持挂有双色三角燕尾旗的长矛长枪）。
- `unit_cavalry_cuirassier_01`: 重装胸甲骑兵（抛光锃亮钢制前后双胸甲、重型黄铜头盔垂黑马鬃、直刃重骑兵佩剑）。

### 7.3 野战炮兵与防御工事 (Artillery & Fortifications)
- `unit_artillery_bronze_12pdr_01`: 拿破仑 12 磅青铜平射野战加农炮（黄铜光滑炮身、两轮木质辐条式大架车轮、配弹药前车与拖拽马匹）。
- `unit_artillery_armstrong_rifled_01`: 阿姆斯特朗后膛螺栓线膛炮（熟铁缠丝锻造炮管、后膛螺栓开闭机构）。
- `unit_artillery_krupp_siege_howitzer`: 克虏伯重型攻城大口径榴弹炮（钢铁厚壁炮管、带弧形驻退反后坐机）。
- `unit_defense_trench_sandbags_01`: 战壕堑壕防线模块（曲折挖掘的壕沟坑道、沙袋胸墙防线、木板铺地防泥泞垫板）。
- `unit_defense_machine_gun_nest`: 马克沁重机枪巢阵地（三脚架水冷式重机枪套筒、缠绕帆布弹药带、周边围绕刺铁丝网拒马）。

---

## 8. 历史奇观与全球地标模型清单 (World Wonders)

世界奇观地标在 3D 近景中作为地理锚点，极大提升国家认同感与历史沉浸感。

- `wonder_london_big_ben`: 英国·威斯敏斯特宫与伊丽莎白塔（大本钟），哥特复兴石砌雕花与四面镀金指针大钟。
- `wonder_paris_eiffel_tower`: 法国·埃菲尔铁塔，1889 世博会铸造的熟铁镂空埃菲尔拱形桁架塔身。
- `wonder_newyork_statue_of_liberty`: 美国·自由女神像，新古典主义铜板氧化铜绿外皮、手举金色火炬与花岗岩星形基座堡垒。
- `wonder_beijing_forbidden_city`: 中国·故宫紫禁城太和殿，三重汉白玉台基、金黄色重檐庑殿顶琉璃瓦。
- `wonder_moscow_st_basil`: 俄国·莫斯科红场圣瓦西里大教堂，9 座高低错落绚丽多彩螺旋纹样洋葱头穹顶。
- `wonder_berlin_brandenburg_gate`: 普鲁士·柏林勃兰登堡门，砂岩多立克柱廊与顶部屋脊胜利女神四马二轮战车。
- `wonder_agra_taj_mahal`: 印度·泰姬陵，纯白大理石巨型球根穹顶与四座微倾倒圆柱尖塔。
- `wonder_istanbul_hagia_sophia`: 奥斯曼·圣索菲亚大清真寺，红粉外墙巨大中央穹顶、阶梯式半穹顶辅托。
- `wonder_cairo_pyramids_giza`: 埃及·吉萨大金字塔与狮身人面像，风化砂岩阶梯巨石。
- `wonder_london_tower_bridge`: 英国·泰晤士河塔桥，花岗岩与波特兰石贴面双悬臂双塔吊桥。

---

## 9. 自然植被与环境装饰要素清单 (Biomes)

与 `ForestCanopyInstancer` 紧密协作的高效率植被模型簇（Cluster），支持季节性积雪与风力扰动。

- `veg_temperate_oak_cluster`: 温带落叶橡树/榉树簇（宽大圆润树冠、多枝干纵横交错、秋季带金红渐变叶）。
- `veg_boreal_pine_cluster`: 寒温带针叶云杉与落叶松簇（深绿尖锥状宝塔轮廓、耐寒笔直树干、高适应积雪着色）。
- `veg_tropical_rainforest_canopy`: 热带雨林冠层（带有巨大板状根的露生巨树、紧密簇拥的棕榈与蔓藤）。
- `veg_mediterranean_cypress_olive`: 地中海丝柏与油橄榄（柱状笔挺深绿丝柏与低矮银灰扭曲橄榄树丛）。
- `veg_savanna_acacia_cluster`: 热带稀树平顶金合欢（如伞状展开的高枝树冠、带干燥灌木丛基底）。
- `veg_desert_oasis_palms`: 沙漠绿洲椰枣棕榈（环绕水洼分布的羽状复叶棕榈、下覆芦苇与耐旱草丛）。

---

## 10. 战略沙盘与桌面微缩战术道具清单 (Tabletop Props)

当相机拉远或打开战局检视视窗时，地图展现维多利亚内阁战略沙盘（Tabletop Cabinet）的奢华质感。

- `prop_military_standee_brass`: 镀金抛光黄铜步兵立体棋子底座（内嵌珐琅国家双面旗帜）。
- `prop_naval_marker_wood`: 桃花心木雕刻微缩战舰战术推杆标记（带有手工刻制吃水刻线）。
- `prop_diplomatic_quill_inkstand`: 维多利亚大臣墨水台道具（雕花黑曜石底座、插有一支孔雀羽毛金笔尖鹅毛笔）。
- `prop_treaty_parchment_scroll`: 国际条约卷轴模型（边缘微卷的泛黄牛皮纸、带有深红丝带绑扎与双重皇家蜡封印章）。
- `prop_battle_crossed_sabres`: 战役交锋标记物（两柄交叉的骑兵钢刀、刀柄镀金护手、带微弱金属高光流转）。

---

## 11. 资产交付目录树结构规范

游戏工程资产仓库的目录布局遵循以下清晰结构：

```text
Thunder/assets/3d/
├── architecture/
│   ├── western_european/
│   │   ├── res_poor_01.glb
│   │   ├── res_mid_01.glb
│   │   ├── res_rich_01.glb
│   │   └── factory_textile_01.glb
│   ├── east_asian/
│   ├── eastern_european/
│   ├── middle_eastern/
│   └── north_american/
├── industry/
│   ├── steel_mill_01.glb
│   ├── coal_mine_headframe_01.glb
│   └── oil_derrick_01.glb
├── transport/
│   ├── loco_early_440.glb
│   ├── car_boxcar.glb
│   └── bridge_iron_truss.glb
├── naval/
│   ├── ship_sail_line_1st.glb
│   ├── ship_ironclad_casemate.glb
│   └── ship_dreadnought.glb
├── military/
│   ├── infantry_british_redcoat.glb
│   ├── cavalry_hussar.glb
│   └── artillery_bronze_12pdr.glb
├── wonders/
│   ├── wonder_big_ben.glb
│   └── wonder_eiffel_tower.glb
├── biomes/
│   ├── veg_temperate_oak.glb
│   └── veg_boreal_pine.glb
└── textures/
    ├── architecture/
    │   ├── we_brick_orm.ktx2
    │   └── we_brick_norm.ktx2
    └── naval/
        ├── ironclad_orm.ktx2
        └── ironclad_norm.ktx2
```

所有资产最终通过 `thunder_asset_cooker` 统一打包进 `.thunderasset` 索引封包，确保运行时拥有毫秒级局部流式加载与极致稳定的 60/120 FPS 渲染帧率。
