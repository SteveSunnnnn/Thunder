# Runtime 模块 — 最小接口用户文档

职责：宿主（游戏外壳）唯一需要认识的层——引擎门面、内容安装、存档、调试台、编辑器。

## 最小接口（宿主只需要这一个头）

`thunder/runtime/engine/ThunderEngine.hpp` — pimpl 门面，完整生命周期：

```cpp
ThunderEngine engine;

// 1) 装配世界（新游戏）
engine.set_new_game_content_hash(content_hash);
engine.set_world_pack_hash(world_pack_hash);
engine.set_world_topology(adjacency, /*...*/);      // 世界包解码产物
engine.set_world_static_layers(static_layers);
engine.initialize_economy();                        // 内容已由 GameContentRuntime 安装

// 2) 逐 tick 推进（模拟+经济+AI，确定性）
engine.advance_tick(&profile);                      // 或 advance_ticks(n)

// 3) 玩家命令（异步队列，tick 内按序消费）
auto cmd = engine.queue_command(CommandType::X, country_id, value);

// 4) 存档
engine.restore(save_bytes);                         // round-trip + checksum 校验
```

| 头文件 | 提供 | 说明 |
|---|---|---|
| `runtime/engine/ThunderEngine.hpp` | 上述门面 | 宿主唯一入口 |
| `runtime/engine/GameContentRuntime.hpp` | 脚本内容单快照安装 | bootstrap 时调用 |
| `runtime/save/SaveGame.hpp` | 存档序列化/恢复 | magic `THUNDRSV`/`THUNDRZS` |
| `runtime/editor/MapEditorSystem.hpp` | 地图编辑器 | 工具链，非游戏路径 |
| `runtime/engine/DebugConsole.hpp` | 调试台 | 开发期诊断 |

## 契约

- 宿主不得跳过引擎直接改 Store；一切玩法输入走 `queue_command`。
- 存档恢复后必须通过 checksum 验证（含 CurrencyStore FX 流累积器）。
- 内容哈希与世界包哈希进新游戏指纹，防止内容漂移。

相关文档：`docs/ARCHITECTURE.md`（含"已知未接通"第 8 节）、`docs/DETERMINISM.md`、
`docs/modules/simulation.md`、`docs/modules/presentation.md`。
