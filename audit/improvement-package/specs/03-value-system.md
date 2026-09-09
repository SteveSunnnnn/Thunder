# 规格 03 · 嵌套算术 Script Value IR（P0-5）

> 锚点基线：2026-09-05。夹具：`../reference-content/examples/05_script_value.core`。

## 1. 现状与差距

脚本化值的表达式**只有一种形态**：`source * multiply + add`
（`docs/THUNDER_SCRIPT.md:122`，`ScriptCompiler.cpp:45-47` intern `source/multiply/add`）。
这意味着：

- 无法表达任何嵌套：`(人口×0.5 + GDP×0.3) / 基准` 这类平衡公式全部写不出；
- 无法引用其他脚本化值做运算、无法 min/max/clamp/round；
- Modifier 系统（HierarchicalModifierGraph 的 add/mult/min/max 桶）与脚本值是两套
  平行世界，无法互相消费。

对照：Vic3 的 script value 是任意嵌套算术树，是平衡与 AI 打分的通用语言。当前差距
属于"商用即用型"缺项——**几乎每一个真实的平衡/规则文件都会用到**。

## 2. 设计决策

1. **新增独立值 IR**，不复用效果字节码：`ValueExpr` 编译为扁平指令数组
   （对标现有 RPN 条件指令的紧凑风格），求值无递归、无逐节点分配。
2. **类型 = 语义层双轨的收敛点**：求值类型统一为 double（现状即如此），但
   **入口/出口提供定点互操作**：引用定点源（`treasury_milli` 等）时先除
   `economy_scale` 得规范 double；结果落定点槽位（modifier、脚本值返回）时乘
   `economy_scale` 并饱和。由此消除"double 与 int64 两套语义跨边界静默分歧"
   （见审计 §3.2）——**收敛规则集中在 ValueExpr 求值器一处**。
3. **确定性**：运算顺序 = 编译期固定（无优化重排）；NaN/Inf 在指令边界归一
   （沿用 `Hash.hpp:46-68` 的归一策略），随机不在值求值中出现。
4. **Modifier 集成**：ModifierEntry 的 value 允许引用命名脚本化值；Hierarchical
   ModifierGraph 对含值引用的条目做脏传播（revision 机制已具备，只需把"值版本"
   并入节点脏判定）。

## 3. IR 形态

```cpp
enum class ValueOp : std::uint8_t {
    PushConst,     // operand: double
    PushValue,     // operand: ValueGetterId（register_value 注册，见规格 04）
    PushParam,     // operand: 参数槽
    PushVar,       // operand: ScriptStableKey
    PushResult,    // 引用前置子表达式结果（堆栈复用）
    Add, Sub, Mul, Div,      // 二元（栈顶两操作数）
    Min, Max,      // n 元折叠（编译期 n 常量）
    Clamp,         // 三元
    Round, Floor, Ceil, Abs,
    Neg,
};
struct ValueInstruction {
    ValueOp op;
    double operand_f = 0.0;
    std::uint64_t operand_k = 0;   // getter/param/var 的键
};
```

源语言形态（与 Vic3 精神一致但保持静态可验证）：

```text
scripted_value gdp_weighted_sol {
    scope = country
    value = {
        div = {
            add = { pop_value sol_average 0.5 }   # 复合子值
            sub = { gdp_per_capita 4 }
        }
    }
}
```

静态检查：除数恒零拒绝（仅在字面量/常量传播可证时）；引用环检测复用现有
link-cycle 通道（`ScriptCompilerScoped.cpp` 调用环诊断）；所有操作数种类/作用域
在编译期核对。

## 4. 代码落点

| 变更 | 落点 | 内容 |
|---|---|---|
| 新文件 | `scripting/ScriptValueExpr.{hpp,cpp}` | ValueOp/ValueInstruction/求值器 |
| 编译 | `scripting/ScriptCompilerScoped.cpp` | `value = { ... }` 从"source*multiply+add"换到 ValueExpr 编译 |
| 注册 | `scripting/ScriptRegistry.hpp` | `register_value`（与规格 01 §5 共用） |
| 结果类型 | `scripting/ScriptValue.hpp` | ScriptArgument 不扩展；值求值结果转 Number 入参（避免污染 trivially-copyable 布局） |
| Modifier 集成 | `simulation/kernel/HierarchicalModifierGraph.{hpp,cpp}` | ModifierEntry 增加 `SymbolId value_ref`；求值时经 ScriptValueEvaluator 拉取，脏传播并入节点 revision |
| 定点收敛 | `simulation/economy/EconomicTypes.hpp` 语义文档 + ValueExpr 求值器 | scale 乘除集中在求值器边界 |

## 5. 示例（改前 / 改后）

改前（只能两因子）：

```text
scripted_value sol_target { scope = country
    source = sol_average
    multiply = 0.5
    add = 2
}
```

改后（任意嵌套，见夹具 `05_script_value.core`）：

```text
scripted_value intervention_threshold { scope = country
    value = {
        max = {
            30
            div = {
                add = { gdp_weighted_sol 5 }
                2
            }
        }
    }
}
script nationalization_trigger {
    scope = country
    trigger = { scripted_value:intervention_threshold < 25 }
    effect = { set_gdp = scripted_value:intervention_threshold }
}
```

## 6. 验收测试清单（`tests/thunderscript_value_expr_tests.cpp`）

1. 纯字面量表达式：运算符优先级/结合按编译产物固定；含除零（可证常量）拒绝。
2. 复合嵌套（add 套 sub 套 min/max/clamp/round）结果与手工计算一致（±0 ULP 内
   按 IEEE-754 顺序核对）。
3. 定点互操作：`source = treasury`（int64 milli 源）→ 规范 double → 落 modifier
   定点槽，数值与 C++ 直算路径一致；边界饱和不溢出。
4. 值引用 Modifier：改值 → 依赖节点 revision 增长 → 重算；不改值 → 无重算
   （脏传播断言沿用 HierarchicalModifierGraph 测试模式）。
5. 引用环（A→B→A）链接期诊断；作用域/种类不匹配诊断。
6. 确定性：同内容同种子两次求值逐位一致；NaN/Inf 路径归一。
7. 存量 `source*multiply+add` 内容在兼容期内照常编译（等价 ValueExpr 合成）。

## 7. 工作量（粗估）

值 IR + 求值器 + 定点收敛 + Modifier 集成：**5–6 人周**（Modifier 集成约占 2 周）。
