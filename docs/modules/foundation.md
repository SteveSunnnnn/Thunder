# Foundation 模块 — 最小接口用户文档

职责：零依赖地基——类型化 ID、稳定哈希、确定性随机、并行任务调度。
所有其他层只允许依赖本层，本层不依赖任何层。

## 最小接口（只该用这些头）

| 头文件 | 提供 | 典型调用 |
|---|---|---|
| `thunder/foundation/base/StrongId.hpp` | `StrongId<Tag, Generation>` 类型化句柄 | `GoodId{0}`、`CountryId{3}` |
| `thunder/foundation/base/Hash.hpp` | `economy_stable_key("key")` 稳定 64 位键、哈希组合 | 跨存档/跨进程稳定的实体键 |
| `thunder/foundation/base/DeterministicRng.hpp` | `DeterministicRng::keyed_u64 / keyed_unit` | 任何玩法随机 |
| `thunder/foundation/jobs/JobSystem.hpp` | worker 池、并行 for | simulation 分帧并行 |

## 最小示例

```cpp
#include "thunder/foundation/base/DeterministicRng.hpp"

// 随机必须带键派生：同 tick 同键 => 同结果（确定性契约）
std::uint64_t roll = DeterministicRng::keyed_u64(world_seed, "harvest", province_id);
```

## 禁用与契约

- 禁止 `std::unordered_map/set` 的遍历顺序影响玩法结果（只许做查找）。
- 禁止指针/地址当 sort 或 hash 键。
- worker id 不得进玩法 RNG 种子。
- 权威契约见 `docs/DETERMINISM.md`。

相关文档：`docs/ARCHITECTURE.md`（分层总纲）。
