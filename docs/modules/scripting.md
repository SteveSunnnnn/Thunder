# Scripting 模块 — 最小接口用户文档

职责：ThunderScript 内容语言——文本解析、符号表、字节码编译与 VM 执行。
语言语法权威文档是 `docs/THUNDER_SCRIPT.md`；内置 trigger/effect 清单的
唯一真源是 `ScriptRegistry::make_builtin`（代码即文档）。

## 最小接口（只该用这些头）

| 头文件 | 提供 | 说明 |
|---|---|---|
| `thunder/scripting/SymbolTable.hpp` | 符号 intern 表 | `intern("grain") -> SymbolId`，全引擎共享一份 |
| `thunder/scripting/ThunderScriptParser.hpp` | `ThunderScriptParser{symbols}` | `parse(text) -> ParsedScript`（含逐行诊断） |
| `thunder/scripting/ScriptRegistry.hpp` | `ScriptRegistry::make_builtin()` | 内置 trigger/effect 注册表（只读） |
| `thunder/scripting/ScriptCompiler.hpp` | 脚本 → 字节码 | 编译期静态验证（全库链接/调用环/类型签名） |
| `thunder/scripting/ScriptVm.hpp` | 字节码执行 | tick 内由各 runtime 驱动，宿主一般不直接调 |
| `thunder/scripting/Scope.hpp` / `ScopeResolver.hpp` | 作用域解析（Country/State/Province/Pop/Market） | 事件/脚本的目标选择 |

## 最小示例（解析 → 内容库 → 绑定，见 content 模块文档）

```cpp
SymbolTable symbols;
auto registry = ScriptRegistry::make_builtin();
ThunderScriptParser parser{symbols};
auto parsed = parser.parse(R"THUNDER(
    good grain { base_price_milli = 1000 category = staple }
)THUNDER");
assert(parsed.ok());
```

## 禁用与契约

- 不要绕过 parser 手填 `ParsedScript`；所有内容文本必须走 parse。
- 编译诊断（`ScriptCompileDiagnostic`）含行号，内容错误必须先清零再发布。
- 静态验证是差异化资产：新增脚本语法必须同步编译器与验证器。

相关文档：`docs/THUNDER_SCRIPT.md`、`docs/SCRIPT_FIRST_CONTENT.md`、`docs/MOD_RUNTIME.md`。
