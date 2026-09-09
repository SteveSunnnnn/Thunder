# Thunder 模块审查与真实度缺口清单（2026-09-07）

**范围**：全模块检查（economy / kernel / grand_strategy / warfare / foundation / save /
scripting / presentation 抽查），基线双配置 28/28 绿。按用户指示：bug 直接修复并回归；
真实度缺口只列出、不改动，等待审批。不含音频/多人维度。

**用户裁定（同日）**：R1–R12 全部实施；就业保持地块级、州内不允许通勤（R9 不实施，
与现状一致）；建筑倒闭功能补充。实施状态见 §4。

## §1 本轮修复的 bug（已落地，28/28 回归通过）

| # | 严重度 | 问题 | 位置 | 修法 |
|---|---|---|---|---|
| 1 | P1 经济闭环 | **工资按错误主体/费率结算**：POP 分拆到多个建筑时，全部人头按"最后雇主"（最低工资）的费率从最后雇主的现金支付；先雇佣的高薪建筑得到免费劳动力却照常产出，最后雇主为他未雇的人付幻影工资。扭曲单建筑盈利、MRP 工资锚定与投资池扩张择向 | `EconomyPopulationPhases.cpp` employment/consumption | 工资结算移入 employment 相位，按雇佣段结算：每段从本建筑现金扣（受信贷线钳制）、按本建筑报价计入 POP 收入 |
| 2 | P1 金融正确性 | **信用评级/借贷额度比较基准错了 52 倍**：`gdp_` 是周增加值流量，评级阈值（AAA<30%…CCC≥250%）与 2.5× 借贷上限是年度比率概念。存量债务对一周 GDP → 任何有意义的主权债务都读作 CCC，AAA 可借上限仅 ≈4.8% 年 GDP | `CountryStore.cpp:260,314` | 两处统一按 52 周年化（与周付息 `yield/52` 口径一致） |
| 3 | P2 健壮性 | **人口增长 int32 溢出**：`growth_progress += clamp(weekly)` 的 `+=` 本身可在超大 POP 下溢出 int32（UB）；population 写入 uint32 列无上界钳制 | `EconomyPopulationPhases.cpp:86` | int64 累加器 + 人口钳制到 `[1, UINT32_MAX]`，饱和而非 UB |
| 4 | P2 确定性卫生 | **AddTreasury 命令走 double 路径**：`llround(treasury_double*1000)` 重导 milli，大额库藏丢 milli 精度 | `CommandQueue.cpp:44` | 边界处一次转换后直接 `add_treasury_milli`（钳制 ±9e15 防 llround 越界） |

新回归测试：`test_wages_paid_per_building_at_own_offer`（逐建筑精确扣款 + POP 分段收入求和）、
`test_population_growth_huge_pop_saturation`（4e9 人口饱和增长精确值）。
既有测试更新：主权债务测试改为显式年度 GDP 语义（"180% 年 GDP → BB"，原断言在错误口径下恰好通过）。

验证：`dev-headless` 与 `build-vulkan` 双配置 28/28 绿，含多 worker 确定性、货币/物资双闭环守恒套件。

## §2 真实度缺口（待审批，未改动）

按对"北极星 = 经济增长与生活水平"闭环的影响排序：

| # | 缺口 | 证据 | 与 V3/现实的差距 | 建议改动 | 影响面 |
|---|---|---|---|---|---|
| R1 | **金银货币锚价格恒定** | `EconomySystem.cpp:245` `update_exchange_rates(1'000'000, 64'516, …)` 每周硬编码 | 矿业繁荣不引发通胀/通缩；19 世纪银本位崩溃、金矿冲击（加州/澳洲淘金热的价格革命）无法涌现 | 金/银注册为市场商品，金属价由供需决定并喂给汇率更新 | 货币层 + 商品定义 + 金本位测试 |
| R2 | **科技扩散量级失衡** | `GrandStrategyWeekly.cpp:461-464` 25% 概率 × +150ppm/周 ≈ 期望 37.5ppm/周 → 单项扩散约 512 年 | V3 量级为几年；当前等于没有扩散 | 提高单次进度（如 +2000ppm）或将速率参数化给内容 | 数值平衡，影响节奏（建议审批时定量） |
| R3 | **MAPI 算了但没人用** | `GrandStrategyWeekly.cpp:533-535` 每周写入 `state_market_access_ppm`；全经济价格相位从不读取 | V3 的市场接入度把州价与市场价按 MAPI 混合；当前州基建投资对物价零影响 | 价格相位按 MAPI 混合州级价格与市场价 | update_prices + 测试 |
| R4 | **海上封锁零经济效果** | `sea_zone_blockade_level` 仅 DebugConsole 消费 | 封锁不削航线容量/关税收入 | trade() 按封锁 ppm 削减对应航线容量 | trade 相位 |
| R5 | **建设不消耗实物商品** | `ConstructionStore::tick_weekly` 只扣钱（纯货币漏出） | V3 建设部门吃建材需求（铁/工具/木），是经济乘数的主引擎之一 | 建设项目按进度产生商品需求 | ConstructionStore + 定义格式 |
| R6 | **投资池自动扩张瞬时完成** | `EconomyMarketPhases.cpp:672-691` 当周直接 level+1 | 绕过建设队列，资本形成没有时间成本 | 自动扩张改为入队 ConstructionStore 项目 | construction 相位 |
| R7 | **迁移抹平 POP 身份、无自动迁移流** | `update_migration_flows`（GrandStrategyStore.cpp:661-663）把人数并进目的省第一个 POP；`calculate_province_attraction` 无引擎内调用者 | 移民不保留文化/宗教/职业；吸引力评分是死资产；V3 的"淘金潮"不会出现 | 迁移匹配/新建同身份 POP；周期性吸引力扫描自动生成迁移流 | grand_strategy 迁移 + PopStore |
| R8 | **主权债务无到期/重组** | CountryStore 债务永续、利息服务+手动偿还；违约只累加 default_weeks | 无减记、无市场禁入期，违约无真实代价 | 违约触发重组（减记比例+禁借期），银行债券同步减记 | CountryStore + BankStore |
| R9 | **就业限本省，无州内通勤** | employment province-bucket（EconomyPopulationPhases.cpp:320-329） | V3 就业为州级；本省下失业与同省市场内缺工可同时存在 | 候选扩到同州/同市场建筑（通勤半径） | employment 候选构建 |
| R10 | **建筑无固定运营成本、永不倒闭** | settlement 只结算可变成本；亏损建筑靠 50 万信贷线无限续命 | 无破产出清 → 僵尸产能永不退出，供给过剩行业无法出清 | 现金长期低于阈值→缩编/拆除 | settlement + 建筑生命周期 |
| R11 | **公司是壳** | `companys()` 只有 cash/productivity，不持股建筑、不参与贸易 | V3 公司是资本集中的特色机制 | 公司持有建筑所有权份额、参与分红与投资决策 | grand_strategy + settlement |
| R12 | **库存无主、无资金占用** | 市场库存匿名，无持有者、无仓储成本（超出容量即腐败销毁） | 无贸易商/囤货行为；价格投机不存在 | 低优先；可作为贸易公司玩法的前置 | 市场层，改动大 |

## §4 实施状态（用户批准后落地）

- **R9 明确不实施**（用户裁定：就业按地块为单位，州内不允许通勤——与现有
  province-bucket 设计一致，无改动）。
- **R1–R8、R10–R12 全部落地**，回归测试见 CHANGELOG 对应条目，双配置 28/28 绿：
  - R1：`EconomyDefinitions.monetary_metal_key` + `find_good_by_monetary_metal` +
    内容 `monetary_metal` 字段/绑定；`EconomySystem::run_weekly` 以市场均价喂
    `update_exchange_rates`，常量仅作无金银回退。
  - R2：`GrandStrategyWeekly.cpp` 扩散步进 150 → 8000 ppm/成功。
  - R3：settlement 建筑循环按州 MAPI 计算 ±25% 买卖 wedge（`mapi_sell/buy_ppm`）。
  - R4：`EconomySystem::country_blockade_ppm_` 周暂存 + `apply_blockade_scale`，
    贸易 `ship_leg` 对跨境段削减。
  - R5：`EconomyDefinitions.set_construction_inputs` + 内容 `construction_goods`
    块；`ConstructionStore::tick_weekly(world, basket)` 按点耗料、向 clearing
    付款、Lieontief 限速、逆序退款；承包商托管优先出资。
  - R6：`EconomySystem::construction` 改为托管+入队（`enqueue_expansion` 去重）。
  - R7：`update_migration_flows` 身份保留结算（合并/建新 POP、1 人残留）；
    新增 `generate_autonomous_migration_flows` 并接入引擎周任务。
  - R8：结算债务服务连续 4 周拖欠触发 30% 减记（`write_down_sovereign_bonds` +
    pool 吸收 + 52 周禁入）。
  - R10：settlement 收集 `bankrupt_scratch_`，串行缩编/关停（贷款状态门控：
    `building_loan_status`），clearing 吸收残值，POP 雇主清理。
  - R11：`companys_mut`/`company_cash`/`add_company_cash`；settlement 按 stake
    CSR 分红并串行归集；公司自有扩张入队。
  - R12：`update_prices` 按结算库存收 0.1%/周入 clearing；
    `inventory_carry_cost_milli` 进入审计恒等式。
- 实施中捕获并修复：`test_money_closed_loop_conservation` 遍历已倒闭建筑槽位
  触发 `dead BuildingId`（checked 访问器语义正确，测试改为跳过死槽）。

## §3 工程观察（非真实度，供参考，未改动）

- `prestige_`/`population_`/`gdp_` 仍为 double 列（treasury 已收敛 milli 权威）。IEEE 基本运算
  跨平台一致，风险低；若未来要严格跨平台 lockstep 需整数化。
- `run_state_resistance_weekly` 每周按状态数分配 6 个临时向量；`run_warfare_weekly` 的战区破坏
  为 O(前线×POP) 全表扫描——大规模世界的性能隐患，非正确性问题。
- `build/` 目录有上轮调试残留物（`dbg_growth2.*`、`debug_journal.*`、`debug_vfs.*`），
  建议删除（build 目录为生成物，本次未动）。
- 抽查确认无以下问题：`std::rand`/`random_device`/`time()` 未出现在 `src/thunder`；
  模拟路径无无序容器迭代序泄漏；SlotPool 世代/位图恢复校验完备；TickScheduler 拓扑波次确定性；
  JobSystem dispatch 生命周期（active_background 两阶段等待）无竞态；GameClock 日期/边界数学正确。
