# 货币本位制与外汇流通设计

`audit/2026-09-05_monetary_standard_fx_design.md`
日期：2026-09-05 深夜
背景：用户要求①贵金属作为独立货币类支撑本位制；②外汇的流通逻辑想清楚——
作为货币还是作为商品。

---

## §1 现状盘点（本次设计前的地基）

**货币层（CurrencyStore，已存在且完整）：**
- `MonetaryStandard`：金本位 / 银本位 / 复本位 / 法定浮动（四类本位制）。
- 金属平价：`gold_parity_mg` / `silver_parity_mg`（每货币单位的含金/含银毫克数）。
- **贵金属 numeraire**：`core.metal.gold` / `core.metal.silver` 是一等货币
  （`CurrencyClass::Metal`），作为 FX 定价锚。
- 汇率机制：铸币平价推导（含金量 × 市场金属价）+ 贸易差额压力漂移（agio）
  + 休谟 specie-flow（金点套利：汇率偏离平价超过运费点 → 实物金银运输清账）
  + 复本位 Gresham（法定比率 vs 市场比率，**流通取较便宜的一腿**）。
- FX 流累积 `record_fx_flow`、兑换暂停（`convertibility_suspended`）、
  铸币税（seigniorage 归宗主国）、货币主权评估。

**商品层（缺口）：** 金银作为可开采、可交易的商品此前不存在——
市场里的金属价格没有来源，`update_exchange_rates` 的金属价参数只能外部灌入。

**结论：货币层的"贵金属独立类"已在；缺的是商品侧的对应类与两者间的显式联动。**

---

## §2 核心设计决策：外汇 = 按商品结算的货币（双路径模型）

**决策：外国货币在贸易结算中"作为商品流通"——按其金属含量 × 市场金属价计价，
叠加贸易差额 agio；本币只作记账单位。** 不引入独立的外汇纸面市场。

论证：
1. **史实吻合（1836 起点）**：19 世纪跨国结算本就走"硬币按重量/成色"路线
   （墨西哥银元在中国流通、拿破仑金拿破仑按含金量计价）。"外汇"不是一个抽象
   兑换市场，而是外币铸币作为金属商品被接受。
2. **机制自洽**：`update_exchange_rates` 的汇率本来就由
   `金属含量 × 市场金属价` 推导（金点机制），贸易差额产生 agio，金点封顶。
   外汇"作为商品"与"作为货币"不是二选一，而是**同一条链的两个端点**：
   - 商品价值（melt value）= 平价含量 × 市场金属价 —— 熔毁套利的价格下限；
   - 货币价值（面值汇率）= 商品价值 ± agio —— 受金点约束收敛回平价。
   两者之差正是驱动 specie-flow 的套利空间，机制闭环。
3. **确定性**：金属价来自市场（GoodCategory::Currency 商品的成交价），
   平价是内容数据，agio 是贸易流量函数——全程无浮点、无随机、可 checksum。
4. **本位制的角色**：本位制决定"哪一种金属定义平价"。金本位锚金、银本位锚银、
   复本位双锚（Gresham：流通腿取便宜者、熔毁腿取贵者）、法币无金属锚
   （纯信用，汇率纯由贸易差额与主权信用驱动）。

### 双路径结算表

| 场景 | 路径 | 函数 |
|---|---|---|
| 有银行/外汇市场的正常贸易 | 货币路径：按汇率兑换 | `convert()` / `convert_price()` |
| 无银行设施、汇率不可信、或金属套利 | 商品路径：按铸币金属含量计价 | `coin_commodity_value_milli()`（本轮新增） |
| 复本位流通 | 法律面值钉较便宜金属（Gresham 流通腿） | `update_exchange_rates` 取 min |
| 复本位熔毁套利 | 取较贵金属的熔毁价值（Gresham 熔毁腿） | `coin_commodity_value_milli` 取 max |
| 法币跨境 | 只有货币路径（无金属商品价值） | `convert()`；商品价值 = 0 |

---

## §3 数据流（贵金属从矿山到汇率）

```
金矿/银矿（good, category = currency）
   → 市场供需定价（MarketStore 成交价 = 金属价）
   → update_exchange_rates(gold_price_ppm, silver_price_ppm)
       ├─ Metal numeraire 汇率 = 金属价（锚）
       ├─ 各币 target_rate = 平价含量 × 金属价（铸币平价）
       ├─ agio：贸易差额压力漂移（record_fx_flow 累积）
       └─ 金点：偏离超运费 → specie 出进口 → 收敛
   → 结算：
       convert()            —— 货币路径（面值）
       coin_commodity_value —— 商品路径（熔毁价值）
   两价差 = 套利空间 → 反馈回 specie-flow
```

---

## §4 本轮实现

1. **`GoodCategory::Currency = 4`**（`Count=5`）：贵金属/铸币成为独立商品类，
   脚本 `category = currency`。语义：该类商品的市场价即货币金属价，
   喂给 `update_exchange_rates` 作为锚。
2. **`CurrencyStore::coin_commodity_value_milli(coin, gold_price_ppm, silver_price_ppm)`**：
   外国铸币的商品侧估值（熔毁价值），按本位制分派：
   金本位=含金量×金价；银本位=含银量×银价；复本位=max(两腿)；
   法币=0。与 `convert()` 构成双路径。
3. **测试**：
   - thunder_tests：`category = currency` 解析/绑定/断言（gold_bullion）。
   - economy_tests：`test_coin_commodity_value_milli`——金/银/复/法币四类铸币
     的商品价值与平价一致性（15.5:1 基线下金银同值）、复本位 max 语义、
     法币零值、未知币安全。
4. 全部 g++ -fsyntax-only 通过；语义回归由 CI ctest 承担。

---

## §5 待办（按优先级）

1. **货币脚本铸造**：`currency <key> { standard = gold gold_parity_mg = 1000 ... }`
   内容定义（现在货币只从存档/代码注册）；接 DefinitionDatabase → CurrencyStore。
2. **锚绑定**：`GoodCategory::Currency` 商品的 key 与 `metal_gold_key/metal_silver_key`
   numeraire 的映射表（市场金属价喂 FX 的自动管道）。
3. 复本位的 Gresham 流通腿可视化与国策切换本位制（变平价 → 货币重铸事件）。
4. 外汇 UI：汇率历史曲线（`history_rates_ppm` 已存）+ agio/金点显示。
5. 不可兑换纸币（convertibility_suspended）下的商品路径行为细化
   （纸币贴水交易、劣币驱逐）。
