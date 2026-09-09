# 规格 01 · 格式层扩展（P0-1 / P0-2 / P0-7 / P1-7）

> 锚点基线：2026-09-05 源码审计。原型：`../prototypes/format_layer/`（已运行验证）。
> 验收夹具：`../reference-content/examples/01_format_layer.core`。

## 1. 现状与差距

`ThunderScriptParser` 的语法是**强约束单形态**：顶层只能 `type name { }`，块内每条
必须是 `key = value`，值只有 Number/Symbol/Block 三种（`ThunderScriptParser.hpp:11-23`），
词法层只认识 `=`（`ThunderScriptParser.cpp:17`）。由此带来五个不可达：

| 能力 | Clausewitz 内容里的角色 | 当前状态 |
|---|---|---|
| 数组 `{ 1 2 3 }` / `{ a b c }` | 颜色、文化组、科技列表、地块 id 集合——无处不在 | 报 "expected property name" |
| 混合容器 `{ 1 "x" a = 2 }` | 通用自描述格式的核心 | 不可达 |
| 比较运算符 `> >= < <= == !=` | trigger 的通用语义 | 词法层缺失，只能靠 C++ 原语补 |
| 日期字面量 `1836.1.1` | 历史条目、到期、冷却 | 退化为无类型符号 |
| 文本回写 | 编辑器保存、内容迁移、lint 修复 | 全仓无序列化器 |

## 2. 设计决策

1. **格式层保持类型中立**（同 jomini 哲学）：解析器只识别形态（数组/对象/混合/
   DateCandidate），不解释语义。`1836.1.1` 是否成为 `GameDate`、`treasury > 100`
   是否成立，一律由**编译层的类型期望**决定。这保证格式层可复用、可无损。
2. **值种类新增三个分支**：`Text`（引号文本，与 Word 分离）、`DateCandidate`、
   Block 内部允许**裸值子项**（数组/混合）。
3. **运算符进入 AST**：Node 携带 `op` 字段；`key op value` 统一表示。编译层把
   "普通赋值"与"比较"分流到不同 IR（见规格 02/03）。
4. **写器双模**：`verbatim`（未编辑文档逐字节还原，注释保留）与 `pretty`
   （确定性排版、可再解析）。生产按顶层条目 span 切片支持局部编辑。
5. **兼容优先**：旧语法（键值对单形态）是新的子集；不加任何前缀/开关即平滑升级。

## 3. 语法（增量 EBNF）

```text
document        := { object | entry }
object          := type name "{" { entry | value }* "}"
entry           := key op value
value           := number | word | string | date | container
container       := "{" { entry | value }* "}"        (* 数组/对象/混合由内容判定 *)
key             := word | date                        (* 日期作键 = 历史条目 *)
op              := "=" | "==" | "!=" | ">" | ">=" | "<" | "<="
date            := digit{3,4} "." digit{1,2} "." digit{1,2}
word            := 现词法字符集（: . _ - / 保持合法）
```

形态判定（纯结构，不依赖语义）：
- container 内全部子项无 key ⇒ `array`；全部有 key ⇒ `object`；混合 ⇒ `mixed`。
- 顶层三连 `word word {` ⇒ 对象头；否则按 entry/value 解析。

## 4. 代码落点

| 变更 | 落点（基线） | 内容 |
|---|---|---|
| 值种类枚举 | `scripting/ThunderScriptParser.hpp:11` | `+Text +DateCandidate`；`ScriptValueKind` 扩展 |
| Node 结构 | `ThunderScriptParser.hpp:13-23` | `+std::string text; +bool date_valid; int ymd[3]; +Op op; +bool keyless`（key==invalid 表示裸值） |
| 运算符 token | `scripting/ThunderScriptParser.cpp:17,36-46` | TokenKind `+OpT`；词法 `== != > >= < <=`；`?` 单独留位 |
| 块解析 | `ThunderScriptParser.cpp:191-243` | `parse_block` 拆出 `parse_entry/parse_value`，支持裸值与日期键 |
| 值解析 | 同上 | Number/Word/Text/Date/Container 五路 |
| 符号化 | `ScriptValue.hpp:22` | `ScriptArgumentKind` 保持 Number/SymbolHash/Boolean/Scope（格式层扩展不渗透到参数层，除非新类型进入签名） |
| 新文件 | `scripting/ScriptWriter.{hpp,cpp}` | 写器：verbatim + pretty + span 切片 |
| 新文件 | `scripting/ScriptOp.hpp` | `enum class ScriptOp`（与 Node 共用） |

## 5. 编译层分流契约

```cpp
// ScriptCompiler.cpp 现有 dispatch（基线 ~454 行 all/any/not）新增：
//   赋值形态 key = value  -> 既有 trigger/effect/变量路径（不变）
//   比较形态 key op value -> 新指令 CompareValue { ValueGetter key, Op op, arg }
//   ValueGetter 经 register_value(name, scope, getter) 注册（见规格 04）
```

新注册 API（消除 `*_above` 重复原语的根因）：

```cpp
// ScriptRegistry.hpp 追加
using ValueGetter = double (*)(const World&, ScopeRef);
ValuePrimitiveId register_value(std::string name, ScopeType scope, ValueGetter getter);
```

`population_above`/`treasury_above` 等 16 个既有 trigger **保留不删**（兼容），
新增 generic 原语由编译器从 `key op value` 合成，`key` 在 value 注册表内查得。

## 6. 示例（改前 / 改后）

改前（不可能通过解析）：

```text
trigger = { treasury > 100 }        # 词法层无 '>'
techs = { nationalism romanticism } # 块内无 key 项 -> error
```

改后（同 `reference-content/examples/01_format_layer.core`）：

```text
country UNI {
    color = { 128 64 32 }                        # array
    trigger = {
        treasury > 100                           # 比较
        population >= 2000000
        has_law == "universal_suffrage"          # 引号文本 + ==
        founding_date = 1836.1.1                 # DateCandidate
        culture_set = { 5 "north_german" 2 }     # mixed container
    }
    reform_history = {                           # date-as-key（历史条目）
        1836.1.1 = { reform = "slavery_abolished" value = 1 }
    }
}
```

## 7. 迁移路径

- 无破坏性变更：旧文件全部通过（旧语法是新语法子集）。
- 引号文本语义变化提示：过去 `"x"` 与 `x` 同化为 Symbol；升级后 `Text` 与 `Word`
  分流。凡内容里用引号表示 key 的地方（如 `has_law = "x"`）语义仍由编译层按目标
  类型解释——**Key 类参数照旧 intern，Text 只进需要字符串的槽位**。
- 兼容期保留旧的诊断文案断言；`thunderscript_runtime_tests` 中所有既有用例须零改动通过。

## 8. 验收测试清单（新增到 `tests/thunderscript_format_tests.cpp`）

1. 纯数组/纯对象/混合容器分别解析为对应形态（`is_array_like` 断言）。
2. 六种比较运算符 + 赋值全部解析，`op` 字段正确。
3. 引号文本保留原样（含 `\"` `\\` `\n` 转义往返）；Word 与 Text 互不混淆。
4. 日期 `1836.1.1` 拆出 y/m/d；非日期点分词（`2.4.1` 首段 1 位、`version`）不误判。
5. 日期作键（历史条目）解析正确。
6. verbatim：输入含注释/多空白时逐字节还原。
7. pretty → 重解析 → AST 等价（对全部示例夹具）。
8. 顶层对象头与顶层键值对两种入口均可解析（defines 兼容形态）。
9. 既有 217 用例回归不破；超深/超量上限诊断仍触发。

## 9. 工作量（粗估）

格式层扩展 + 写器 + 测试：**4–5 人周**（含原型已降险部分）。
