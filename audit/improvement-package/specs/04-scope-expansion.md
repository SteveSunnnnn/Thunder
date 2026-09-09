# 规格 04 · 作用域域扩张（P0-8）

> 锚点基线：2026-09-05。夹具：`../reference-content/examples/06_scope_domains.core`。

## 1. 现状与差距

可脚本化作用域只有 5 个：`None, Country, State, Province, Pop, Market`
（`scripting/Scope.hpp:7`）。但 `GrandStrategyStore` 已经实现条约/陆军/海军/海战/
海区/前线/会战/殖民地/舰船设计/投资池/利益集团/政党/强权集团/外交博弈/迁徙/政府/
立法（`GrandStrategyStore.hpp` 记录族），`BuildingStore` 有建筑。**全部无法被脚本
访问**——数据存在而 modder 摸不到，是当前最大的"写了等于白写"。

## 2. 扩张原则

1. **ScopeType 是稠密枚举，加值 = 追加末尾**（现有存档用 `uint8_t` 序列化 scope，
   追加不破坏历史值）。
2. 每个新域必须**成组交付**，缺一不可（一次 PR 一域）：
   a. `ScopeType` 枚举追加；
   b. `ScopeRef` 不变（type + raw_id 已够）；
   c. `ScopeResolver` 导航（owner/parent/children 与既有域的桥）；
   d. 一组 typed trigger/effect/value 注册（注册表开放，`ScriptRegistry.hpp:47-54`）；
   e. 若域状态持久化，纳入对应 Store checksum（沿用 `World::checksum` 聚合模式）；
   f. 校验/边界测试（含存档 round-trip 的 scope 引用验证）。
3. **先 Domain 后 Script**：先保证该域的 C++ 机制能回答"我要什么"，再挂脚本原语。
   对只有数据结构、无机制的域（如殖民、海战），**不提前挂原语**，避免把半成品
   暴露成内容契约——这正是战争/AI 归属决策的落点（见规格 07 §4）。
4. **原语命名纪律**：小写下划线、动词或数值语义结尾；`register_value` 收敛
   比较类需求（规格 01 §5），每个数值量**只注册 getter，不注册 N 个 _above**。

## 3. 优先级与首批交付

| 优先级 | 域 | 现网数据 | 首批原语（草案） |
|---|---|---|---|
| P0 | Building | `BuildingStore.hpp` | value: level/employment/cash_milli；effect: set_level/add_level/set_production_method/unlock_pm；trigger: has_pm/type_is |
| P1 | Army / Navy | ArmyRecord/NavyRecord | value: manpower/sailors/org/strength；effect: set_location/mobilize/demobilize/assign_mission；trigger: at_location/org_below |
| P1 | War / Front | FrontRecord/BattleRecord | effect: declare_war/add_war_goal；trigger: is_at_war/is_winning |
| P2 | InterestGroup / Institution | InterestGroupRecord/InstitutionRecord | value: clout/approval/level；effect: add_clout/set_institution_level |
| P2 | Treaty | TreatyRecord/Article/Participant | trigger: has_treaty_with/kind_is；effect: form_treaty/terminate |
| P3 | Character（需先建 CharacterStore） | 无 | — 需新域设计，不在本规格展开 |

Building 优先理由：它是经济链的杠杆点，与既有 Country/State/Pop 导航天然相连
（building → owner country → state → market），可立刻让"国家管建筑、建筑反哺
经济"的内容闭环成立，风险最低、收益最直接。

## 4. 导航契约（Building 示例）

```cpp
// ScopeResolver.cpp 增量（现有 owner/market/state/province 分支旁）
case ScopeType::Building: {
    const BuildingId bid{s.raw_id};
    // building -> owner country（BuildingStore 需暴露 owner 列，若缺则加）
    // building -> state（经 GeographyStore 建筑所在 state 反查或存列）
    // building -> market（BuildingHotData.markets 已存在，PopStore 同型）
}
// country -> any_owned_building / every_owned_building 迭代目标注册为
//   ScopeIteratorSource::Children 的既有通道，只是目标类型换成 Building
```

## 5. 语义示例（夹具 `06_scope_domains.core`）

```text
script managed_economy {
    scope = country
    effect = {
        every_owned_building = {
            limit = { building_type = factory pm_has = automated }
            effect = {
                if = {
                    limit = { employment < 0.5 }        # 该域 value getter
                    effect = { hire_pop = 100 }
                }
            }
        }
        any_owned_building = { limit = { level >= 10 } effect = { award_bonus = yes } }
    }
}
```

## 6. 回归与规模护栏

- `TriggerPrimitiveId`/`EffectPrimitiveId` 为 `StrongId<uint16_t>`（65535 上限），
  各域原语量级为几十到几百，容量无忧（`ScriptRegistry.hpp:19-20`）。
- 每新增一域同步把 `ScopeType` 加进：存档 scope 引用校验（SaveGameScriptSections）、
  checksum 归一（Hash.hpp 里对 scope 做 FNV 时 type 已参与）、
  ScriptedGameplay 事件目标合法性、诊断文案。
- 域原语注册计数进 `export_schema_json`/`export_markdown_docs`（既有输出
  `ScriptRegistry.cpp:74-75`），保证文档自动跟随。

## 7. 验收测试清单（`tests/scope_domain_building_tests.cpp` 等，每域一套）

1. 新域 scope 在 trigger/effect/event target/save_scope_as 四路径均可绑定与还原。
2. 导航桥（building↔country↔state↔market↔province）各向解析正确；非法桥（如
   building → pop）编译期拒绝。
3. 迭代：`any_/every_/random_/ordered_ owned_building` 结果与 C++ 直查一致；
   有序迭代 tie-break 稳定。
4. 存档 round-trip 后新域 scope 引用校验通过；v4 旧档无新域引用（无需迁移）。
5. 每域原语 + 既有 217 用例回归不破；schema 导出文档含新域条目。

## 8. 工作量（粗估）

Building 域整组 **2–3 人周**；Army/Navy 或 War/Front 各 **3–4 人周**（含机制缺口
评估）；InterestGroup/Institution/Treaty 各 **1.5–2 人周**。
