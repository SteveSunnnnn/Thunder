# 货币体系设计：贵金属独立货币类 + 外汇货币/商品属性

日期：2026-09-05 | 引擎：Thunder (C++23 确定性大战略)

## 0. 现状盘点（已证实）

- `CurrencyStore` 已有 `MonetaryStandard` 枚举：Gold / Silver / Bimetallism / FiatFloating。
- 每个 `CurrencyRecord` 带 `gold_parity_mg` / `silver_parity_mg`（每货币单位的纯金属毫克数）、
  `exchange_rate_ppm`、`target_rate_ppm`、`specie_export/import_mg`、`convertibility_suspended`、seigniorage。
- FX 已按**货币模型**实现：`record_fx_flow` 累加供需、`update_exchange_rates` 施加金点/格莱欣法则/贵金属输送（休谟 specie-flow）、
  `convert()` / `convert_price()` 按汇率换值。
- `MarketStore` 每个市场持有一个 `CurrencyKey` + `clearing_cash`，即**国内市场结算货币**已是 `CurrencyKey`。
- 贵金属**不是货币**：只作为 national currency 上的 `gold_parity_mg`/`silver_parity_mg` 字面量存在，
  既不能持有黄金余额、也不能与黄金直接 `convert`、更不能在市场上作商品流通。

## 1. 外汇：作为货币，而非商品（结论）

**外汇应作为货币流通，不应作为商品。**

推理：

1. **外汇的本质是两种记账单位的比价关系**，不是被消费的物理货物。持有外币余额、按汇率兑换，
   这正是“货币”（交换媒介/记账单位/价值储藏）的定义；商品是有自身价格的物理品。
2. **引擎已正确建模货币模型**：汇率由 `exchange_rate_ppm` 承载，`convert()` 做跨币换算，
   `record_fx_flow` 由贸易收支驱动——这是汇率机制，不是某市场的“英镑订单簿”。把英镑做成 `good`
   会强制每个币对都进市场清算，且所有以美元计价货物要再经英镑市场二次跳价，单位-of-account 被破坏。
3. **与 Clausewitz 系大战略一致**：外币是余额+汇率，不是货物。

## 2. 贵金属的双重性（关键洞察）

在金属本位下，贵金属**既是货币、又是商品**：

- **作为货币（Metal 类）**：用于铸币平价、结算、充当 FX 名义锚（“和黄金一样好”）。
- **作为商品（bullion 货物）**：原料金/银作为 `good` 进入市场，受珠宝/工业/囤积需求、加工溢价定价。

因此优雅解法是：**贵金属作为独立 `CurrencyClass::Metal` 货币存在（货币用途），
同时可选择注册为 `good`（bullion，商品用途）。** 金属货币的汇率是货币锚，
bullion 货物的市场价是商品价（含加工/工业需求）；可兑换时由 specie-flow 套利把二者拉到平价附近。

→ **结论落地**：
- 外国法币 → 作为**货币**流通（保留现有 FX 模型）。
- 贵金属 → 作为**独立 Metal 货币类**（结算/锚）同时存在；可选再注册为 **bullion 货物**（商品流通）。
- **外汇本身永远是货币关系，永不作为商品。**

## 3. 贵金属独立货币类：实施方案

### 3.1 `CurrencyClass` 枚举
`EconomicTypes.hpp` 新增：
```cpp
enum class CurrencyClass : std::uint8_t { National = 0, Metal = 1 };
inline constexpr CurrencyKey metal_gold_key   = economy_stable_key("core.metal.gold");
inline constexpr CurrencyKey metal_silver_key = economy_stable_key("core.metal.silver");
```

### 3.2 类由 key 派生（零存档破坏）
**不新增磁盘字段**：`currency_class(key)` 对两个 canonical metal key 返回 `Metal`，否则 `National`。
理由：存档格式 `encode_fx_section` 是定长逐字段写；新增字段会使旧 `.save` fixture 读偏、破坏 round-trip。
派生法完全兼容旧档，且确定性由稳定 key 哈希保证。`checksum()` 无需改动。

### 3.3 引导注册（bootstrap）
`CurrencyStore` 构造与 `clear()` 在 `default_currency` 之后注册：
```
core.metal.gold   -> cls=Metal, standard=FiatFloating, rate=1'000'000  (1000mg 金 = 1.0 基准)
core.metal.silver -> cls=Metal, standard=FiatFloating, rate=64'516    (15500mg 银)
```
（parity mg 仍填 1000/15500 以通过 `validate()`。）

### 3.4 `update_exchange_rates` 重构
- 引入 `gold_ref = gold_price_ppm`、`silver_ref = silver_price_ppm`（即金属价输入）。
- 循环内对 `Metal` 类记录：`exchange_rate_ppm = ref`，记历史后 `continue`（不做铸币平价/输送/贸易压）。
- national 货币的铸币平价改用 `gold_ref`/`silver_ref` 替代原 `safe_gold_price`/`safe_silver_price`。
- 行为对既有测试保持数值一致（默认金 1'000'000 与旧 param 相同）。

### 3.5 能力解锁
- `convert(national, metal_gold_key)` 现在合法：可持有黄金余额、按金价兑换。
- `EconomySystem::update_exchange_rates(1'000'000, 64'516, ...)` 的 param 现在同时是金属货币价输入与 FX 锚。
- 未来扩展：bullion `good` 的工业/珠宝需求可反推 `gold_ref`（让金价由商品市场驱动），本方案已预留接口位（param 即锚）。

### 3.6 确定性 / 校验
- 金属货币在 vector 中固定顺序注册（default, gold, silver, ...），`index_` map 查表，位置无关。
- `evaluate_monetary_sovereignty` 对金属无 sovereign（无国家以金属为 primary），安全。
- `validate()` 对金属（FiatFloating、mg>0）通过。

## 4. 待办（后续 PR，需 CI ctest 门禁）

1. **bullion 货物**：在 `08_consumer_goods.core` 风格示例里把 gold/silver 注册为 `good`（category=industrial/luxury），
   让商品市场价与金属货币价形成套利闭环（specie-flow 已在 `update_exchange_rates` 就位）。
2. **金属价由市场驱动**：把 `update_exchange_rates` 的 `gold_ref` 来源从 param 改为 bullion 货物市价（需 MarketStore 联动）。
3. **UI/diagnostics**：暴露金属货币汇率、各国黄金储备（已有 `foreign_reserves_milli`）。
4. 本轮未触碰 `convertibility_suspended` 无恢复路径的已知缺口（见 MEMORY.md）。
