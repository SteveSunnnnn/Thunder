# 规格 02 · 脚本语言控制流与事件（P0-3 / P0-4 / P0-6 / P1-2 / P1-3）

> 锚点基线：2026-09-05。夹具：`../reference-content/examples/02_control_flow.core`、
> `03_random_list.core`、`04_event_onaction.core`。

## 1. 现状与差距

脚本效果的"词汇表"只有 Effect/ScriptCall/Scope/Iterator/SaveScope/变量/集合
（`ScriptProgram.hpp` ScopedEffectKind 枚举，约 150-170 行）；条件只有
all/any/not/iterator（ScopedConditionKind）。**效果里没有任何分支与加权随机**：

| Clausewitz 主流构造 | Thunder | 影响 |
|---|---|---|
| `if = { limit = {..} effect = {..} }` | ❌ | 内容作者无法表达条件行为 |
| `else_if` / `else` | ❌ | 同上 |
| `while`（有界循环） | ❌ | 只能展开写死 |
| `random_list` + `weight` | ❌（文档列为 outstanding） | 事件选项/随机效果的主干 |
| `limit = { ... }` 条件块 | ⚠️ **`limit` 被占用**：迭代器内作"数量上限"，仅接受 Number（`ScriptCompilerScoped.cpp:203,454`） | 同名不同义，Clausewitz 迁移地雷 |
| 事件选项权重/冷却/自动触发 | 部分（`ScriptedGameplay` 有 Event/Decision/Journal 骨架） | 见 §5 |

## 2. 设计决策

1. **`limit` 语义冲突立即解决**：迭代器数量上限改名 `count = N`；旧 `limit = N`
   （Number 形态）在**一个兼容期**内作为别名保留并告警，随后删除。`limit = { ... }`
   （Block 形态）从此专指条件块。语法形态（Number vs Block）使新旧语义互不歧义，
   存量内容 `limit = 5` 在兼容期仍工作。
2. **条件块统一模型**：任何需要条件的场合（if/while/event 触发）都复用现有
   ScopedConditionKind 编译产物，不另造条件语法。
3. **确定性预算**：`while` 必须带最大迭代上限（编译期字面量，非运行期计算），
   防止死循环进存档；`random_list` 权重一律走脚本化值并复用既有 callsite-salt
   随机流（`THUNDER_SCRIPT.md:153-162`）。
4. **on_action 总线化**：把散落的 OnActionRuntime 调度（`ThunderEngine.cpp:75`）
   升级为"发布-订阅"式跨系统事件总线（见 §5），事件驱动是 Vic3 玩法织体的骨架。

## 3. 新效果 IR（ScopedEffectKind 增量）

```cpp
enum class ScopedEffectKind : std::uint8_t {
    // ...既有...
    If,            // children[0]=limit(condition), children[1]=effect
    ElseIf,        // 与 If 同构，挂在 If 的 else 链
    Else,          // children[0]=effect
    While,         // config: max_iterations(编译期), limit, effect
    RandomList,    // children 为加权分支; config 存每支 weight(编译期或 scripted value id)
    Break,         // 退出最近 while（仅 while 体内合法）
};
// ScopedConditionKind 不变——if/while 复用现有条件编译
```

编译期静态保证（延续静态验证优势）：
- `limit` 块缺省视为 true（与现有空 trigger 语义一致）。
- `random_list` 权重求和为零或含负权重 ⇒ 链接期诊断。
- `while` 无 `max_iterations` ⇒ 拒绝；迭代与 VM work budget 双保险。
- `else_if`/`else` 不得脱离 `if` 独立存在。

## 4. 示例

```text
script stabilize {
    scope = country
    effect = {
        if = {
            limit = { treasury < 20 national_debt > 1000 }   # 条件块（新语义）
            effect = { issue_sovereign_bonds = 250 }
            else_if = {
                limit = { stability_milli < 0 }
                effect = { set_stability = 20 }
            }
            else = { add_treasury = 5 }
        }
        random_list = {
            50 = { set_tax_rate = 0.35 }     # 数字权重（字面量）
            30 = { set_tax_rate = 0.30 }
            20 = { set_tax_rate = 0.25 }
        }
        while = {
            max_iterations = 10              # 编译期上限
            limit = { has_institution = { name = health_system level < 3 } }
            effect = { upgrade_institution = health_system }
        }
    }
}

# 迭代器数量上限新语法（与 limit 条件块共存）
any_owned_province = {
    count = 3            # 原 limit = 3 的别名期写法
    order_by = pop_score
    descending = yes
}
```

## 5. 事件系统补全（P1-2）与 on_action 总线（P1-3）

### 5.1 事件定义扩展（`content/definition/` 下 ScriptedGameplay 族）

```text
event railway_reaches_town {
    scope = state
    trigger = { has_building_type = railroad }
    fire_only_once = yes
    cooldown = { years = 5 }          # Date/Duration 类型（规格 01 提供 DateCandidate）
    option = {                        # 现 option 骨架扩展
        name = "invest"
        weight = { value = 60 }       # 数值/脚本化值
        effect = { add_building = { type = railroad level = 1 } }
    }
    option = {
        weight = 40
        hidden = yes
    }
}
```

能力增量：`weight`（字面量或 scripted value）、`hidden`、`fire_only_once`、
`cooldown`、`triggered_only` + `on_action` 触发挂点、选项默认选中/清除逻辑。

### 5.2 on_action 发布-订阅总线

现况：`OnActionRuntime` 有调度调用（due_tick 队列）与 checksum 覆盖
（`SaveGameInternal.hpp` 已存在 `on_action_section_tag`），说明**持久化已通**；
缺的是"任何系统都能发/收"的注册面。

```cpp
// OnActionRuntime.hpp 增量
struct OnActionBinding {
    std::string_view event_key;   // 订阅的事件
    ScopeType scope;              // 订阅作用域
    SymbolId handler;             // 脚本 id
    int priority = 0;             // 同事件内稳定排序
};
void subscribe(std::string event_key, ScopeType scope, SymbolId handler, int priority = 0);
void publish(const World&, ScopeRef root, ScopeRef from, std::string_view event_key);
```

确定性约束：订阅注册顺序**不得**影响执行顺序——handlers 按
`(priority, script_id)` 稳定排序（沿用 ModManifest ReadyLess 的思路：
`ModManifest.cpp:884-897`）。

## 6. 存档与兼容

- 既有 `limit = N` 内容在兼容期内零改动可编译（告警）；`GCT1` 区段编码不变。
- 事件上下文新增字段（weight 缓存/冷却期）放**新 v5 区段**；v4 读档走默认值。
  区段 tag 惯例见 `SaveGameInternal.hpp:35-42`（`0x31..` 反读 ASCII）。

## 7. 验收测试清单（`tests/thunderscript_controlflow_tests.cpp`）

1. if/else_if/else 三态：命中 limit → 仅命中分支执行；无 limit → 恒真。
2. 嵌套 if + 集合迭代组合；调用帧隔离不泄漏（沿用既有 frame 测试模式）。
3. while 达上限退出且无副作用残留；死循环由编译期上限拒绝。
4. random_list：同种子多轮执行给出同一序列；权重和为 0/负 → 链接期诊断。
5. `count`/`limit`(别名告警)/`limit = {}`(条件块) 三者共存时按形态分派正确。
6. 事件选项：权重分布抽样统计落在 ±5% 内（确定性种子下做 10^5 次抽样）；
   cooldown 到期后恢复可选。
7. on_action 总线：两个 handler 按 (priority, id) 稳定排序执行；publish 参数化
   作用域解析正确；checksum 覆盖 handler 集合。
8. 既有 217 用例回归不破；存档 v4→v5 迁移用例（含随机抽取计数器）。

## 8. 工作量（粗估）

`limit` 重命名 0.5 周 + if/else/while/random_list 4–5 周 + 事件选项补全 2 周 +
on_action 总线 2 周：**合计约 8–10 人周**。
