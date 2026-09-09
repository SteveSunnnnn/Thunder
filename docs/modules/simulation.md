# Simulation 模块 — 最小接口用户文档

职责：权威模拟状态与逐 tick 推进——经济、POP、国家、市场、货币、研究、
战争、大战略、AI。所有状态按确定性契约推进并进入 checksum。

## 最小接口（宿主视角）

宿主**不直接**驱动各 Store，一律通过 runtime 层的 `ThunderEngine`
（见 `docs/modules/runtime.md`）。模块内部结构：

| 子域 | 关键头 | 说明 |
|---|---|---|
| kernel | `simulation/kernel/World.hpp` | 权威聚合：各 Store + GameClock + 地图层级 |
| economy | `simulation/economy/EconomySystem.hpp` | 逐 tick 经济阶段；`EconomyDefinitions` 内容绑定 |
| economy 货币 | `simulation/economy/CurrencyStore.hpp` | 本位制/平价/FX（下表） |
| economy 市场 | `simulation/economy/MarketStore.hpp` | 供需定价、跨市场贸易 |
| population | `simulation/economy/PopStore.hpp` | POP 状态与需求消费 |
| world | `simulation/world/WorldBootstrap.hpp` | 世界包 → 拓扑 → 标签（map_labels） |
| research / warfare / grand_strategy / ai / gameplay / living | 各自目录 | 独立子系统 |

## 货币与外汇（CurrencyStore 最小调用面）

```cpp
// 本位制：GoldStandard / SilverStandard / Bimetallism / FiatFloating
store.register_currency(key, "name", MonetaryStandard::GoldStandard, /*gold_mg*/1000, /*silver_mg*/15500);

// 双结算路径（设计见 audit/2026-09-05_monetary_standard_fx_design.md）：
store.convert(amount_milli, from, to);            // 货币路径：面值汇率
store.coin_commodity_value_milli(coin, gold_price_ppm, silver_price_ppm);
                                                  // 商品路径：熔毁价值（金/银/复/法币）
store.record_fx_flow(sold, bought, volume);       // 贸易差额 → agio
store.update_exchange_rates(gold_price_ppm, silver_price_ppm, &countries);
                                                  // 平价 + 金点 + Gresham，每 tick 一次
```

## 契约（违反即 desync）

- 进 `checksum()` 的字段，存档必须 round-trip（CurrencyStore 的 FX 流累积器在内）。
- 所有随机走 `DeterministicRng`（见 foundation 模块文档）。
- 经济定义（goods/buildings/need_profiles）只从内容 ingest，运行期不改。

相关文档：`docs/ECONOMY_ARCHITECTURE.md`、`docs/FINANCE_BANKING.md`、
`audit/2026-09-05_monetary_standard_fx_design.md`。
