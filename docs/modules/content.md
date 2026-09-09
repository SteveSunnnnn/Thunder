# Content 模块 — 最小接口用户文档

职责：把脚本文本与世界包变成引擎可用的内容定义——经济定义、本地化、资产包。
内容是"一次装载、多 tick 只读"的数据，进入模拟前必须通过 ingest 校验。

## 最小接口（只该用这些头）

| 头文件 | 提供 | 典型调用 |
|---|---|---|
| `thunder/content/definition/DefinitionDatabase.hpp` | 内容定义库 | `ingest(parsed, diagnostics)` → `bind_economy/bind_research/bind_notifications/bind_on_actions` |
| `thunder/runtime/engine/GameContentRuntime.hpp` | 内容运行时（单快照安装） | 游戏启动时统一安装脚本内容 |
| `thunder/content/worldpack/WorldPack.hpp` | `.thunderworld` 世界包读取 | 地理/省份数据装载 |
| `thunder/content/assets/AssetPack.hpp` | 资产包（THUNDRAS） | 按键取资产字节 |
| `thunder/content/assets/ArchitectureKit.hpp` | 建筑变体选择 | 按地区×年代×财富选变体 |
| `thunder/content/localization/LocalizationStore.hpp` | 本地化键值 | UI 文本 |

## 脚本可定义的内容类型（ingest 接受的顶层对象）

```
good <key> { base_price_milli = N  category = staple|luxury|military|industrial|currency }
building_type <key> { workers_per_level = N  input = { good quantity_milli }  output = { ... } }
production_method <key> { building_type = <key>  throughput_ppm = N  input/output = { ... } }
need_profile <key> { need = { good quantity_milli } }
```

未知字段/未知品类/引用不存在的 good 都会在 ingest 产生带行号的诊断并失败。
所有商品链均可通过 `.thunder` 内容脚本进行数据驱动声明与热重载。

## 最小示例

```cpp
DefinitionDatabase definitions{symbols, registry};
std::vector<ScriptCompileDiagnostic> diagnostics;
assert(definitions.ingest(parsed, diagnostics));
EconomyDefinitions economy;
assert(definitions.bind_economy(economy, diagnostics));   // 原子绑定：失败不留半成品
```

## 契约

- 内容先过 ingest 校验再绑定；绑定失败时 staging 丢弃，目标库保持原状（原子性）。
- 经济定义进入 checksum 域，字段增删必须同步存档 round-trip。
- 权威格式文档：`docs/SCRIPT_FIRST_CONTENT.md`。

相关文档：`docs/MOD_RUNTIME.md`、`docs/WORLD_COMPILER.md`。
