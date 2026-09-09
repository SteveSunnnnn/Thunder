# format_layer 原型（已编译运行验证）

**状态：可运行** — `g++ 15.2.0` 编译，内置样例与 `sample_v1.core` 均通过全部断言
（退出码 0，末行 `ROUNDTRIP_OK`）。本目录是 P0-1 / P0-2 / P0-7 / P1-7 的
**参考实现**，用于把规格 `../specs/01-format-layer.md` 的语法设计在引擎合并前降险。

## 构建与运行

```bash
g++ -std=c++23 -O2 -Wall -Wextra -o format_layer_proto format_layer_proto.cpp
./format_layer_proto               # 内置样例自检
./format_layer_proto sample_v1.core # 外部样例（含日期作键）
```

## 验证结果（2026-09-05，本机实测）

| 断言 | 结果 |
|---|---|
| `parse diagnostics == 0`（两种样例均零诊断） | ✅ |
| verbatim 写出 == 输入源逐字节（含注释/空白） | ✅ 472 / 1135 bytes |
| pretty 写出 → 重新解析 → AST 等价（语义 round-trip） | ✅ |
| 数组形态识别 `{ 128 64 32 }` → `[array]` | ✅ |
| 混合容器 `{ 5 "north_german" 2 }` | ✅ |
| 比较运算符 `treasury > 100` / `population >= 2e6` | ✅ |
| `==` + 引号文本（Text 与 Word 分离） | ✅ |
| DateCandidate `1836.1.1`（y/m/d 拆解） | ✅ |
| 日期作键 `1836.1.1 = { ... }`（历史条目形态） | ✅ |

## 与引擎代码的关系

| 原型 | 引擎现状（基线行号） | 生产合并要点 |
|---|---|---|
| 无符号表，字符串直存 | `SymbolTable` interning（`ThunderScriptParser.cpp:145`） | 键/Word 走 intern；Text/日期原文按需保留 |
| 全量 token 向量 | 流式 Lexer（单 token 前瞻） | 生产实现改为前瞻缓冲（peek 1），保持流式 |
| `Node.key` 字符串 | `SymbolId key`（`ThunderScriptParser.hpp:14`） | 增加 `DateCandidate`/`Text` 两个 Kind 分支 |
| `Op` 字段在 Node 上 | 无运算符概念 | `ThunderScriptParser.cpp:216` `accept(Equals)` 处扩展 |
| verbatim 整源直出 | 无写器 | 生产写器按顶层条目 span 切片，支持局部编辑 |

## 已知边界（原型有意简化）

- `?` 存在性运算符未实现（jomini 的 `key?`）；词法层按 `key? = no` 的 `?` 单独 token 设计。
- 错误恢复为启发式；生产诊断需沿用引擎的 depth/node 上限与 source-name 前缀。
- 顶层裸键值对（无对象头，如 defines 文件 `NGAME.X = 5`）暂未作为一等形态；对象头与
  键值对的统一已在语法层可行，生产由 schema 决定入口。
