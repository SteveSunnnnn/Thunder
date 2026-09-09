# 规格 05 · 内容生态：宽容解析 / lint / 本地化 / 热重载 / 存档可读（P1-1 · P1-5 · P1-6 · P1-8 · P1-7）

> 锚点基线：2026-09-05。夹具：`../reference-content/examples/07_lenient_diagnostics.core`
>（故意含未知字段与告警级问题）。

## 1. 宽容解析与诊断分级（P1-1）

### 1.1 现状

内容装载对未知字段是**硬错误**（`content/definition/DefinitionDatabase.cpp:399-405`；
`docs/SCRIPT_FIRST_CONTENT.md:44` "Unknown fields are errors"）。jomini 信条相反
（未知键照常产出，交上层决定）。商业现实：mod 兼容靠"旧内容在新版本少报错地跑"。

### 1.2 设计

```cpp
enum class DiagnosticLevel : std::uint8_t { Info = 0, Warning = 1, Error = 2 };

// DefinitionDatabase / ContentLoader / ModManifest 共用
struct ContentDiagnostic {
    DiagnosticLevel level = DiagnosticLevel::Error;
    std::string category;      // unknown_field | bad_value | scope_mismatch | ...
    std::string message;
    std::string file;          // logical VFS 路径
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

// 装载门禁配置
struct ContentValidationPolicy {
    DiagnosticLevel unknown_field = DiagnosticLevel::Warning; // 默认宽容
    bool strict = false;          // --strict / 一测内容：升为 Error
    bool reject_all_warnings = false; // CI 全绿门禁用
};
```

**分级语义**（重要——不是放弃静态验证，而是给"格式/未知项"与"语义/类型"分层）：
- 未知字段 / 未使用键        → Warning（宽容，默认不阻塞）
- 语义类：类型/作用域/调用环   → Error（永远阻塞，保留 Thunder 差异化优势）
- `--strict`（一测内容、CI）   → 未知字段也升 Error

### 1.3 落点

`DefinitionDatabase.cpp:399` 的 `diagnostics.push_back(...); object_ok = false;`
改为按 policy 分流：Warning 记入结构保留（GenericDefinitionSchema 已有
`Replace/Patch/Extend/Remove` 语义，见 `DefinitionDatabase.hpp:138-148`，可落
`extra_fields_`），Error 照旧拒绝。`MOD_RUNTIME.md:140-149` 接受门禁改为
"strict 或 reject_all_warnings 时拒绝，否则允许带 Warning 通过并记入握手清单"。

## 2. 内容 lint CLI（P1-6）

新工具 `src/apps/thunder_content_lint.cpp`（沿 `thunder_add_tool()` 约定）：

```
thunder_content_lint --content <vfs-root> [--mod <id>...] [--strict]
                    [--format text|json] [--out report.txt|report.json]
                    [--emit-markdown]            # 生成 mod 诊断报告（P4 卖点）
退出码：0 = 无 Error；1 = 有 Error；2 = 用法/IO 错误
```

- 复用 `ScriptProgramDatabase::validate_links` 的启动等价路径（
  `docs/THUNDER_SCRIPT.md:172-175`），不启动游戏即完成解析+编译+链接+策略校验。
- JSON 报告含 file:line:column、category、level、建议文案，供编辑器/CI 消费。
- 测试：`tests/content_lint_tests.cpp`（夹具 `07_lenient_diagnostics.core`）。

## 3. 本地化补全（P1-8）

### 3.1 复数形式

现况：键值/插值/语言切换/回退具备（`LocalizationStore.cpp:22-41`），复数缺失。

```cpp
// LocalizationStore.hpp 增量
enum class PluralClass : std::uint8_t { Zero, One, Two, Few, Many, Other };
using PluralRule = PluralClass (*)(double count);
void register_plural_rule(std::string_view lang, PluralRule rule); // zh/en/ru/de/fr/...
// 模板串语法：{0} 插值沿用；复数选择：
//   "{count} {0[one]=item|other=items}"   -> 按当前语言规则选择
std::string interpolate(const std::string& key, std::span<const std::string> args,
                        double count = 0.0, const char* plural_slot = nullptr);
```

- 内置规则：en/de/fr `one|other`；ru `one|few|many|other`；zh 一律 `other`（无词形）。
- 与既有 `[scope]`/`[color:]` 富文本标记共存（`LocalizationStore.cpp:98-207`）。

### 3.2 `l_<lang>` 文件约定

在 VFS 下引入 Clausewitz 风格组织，**同时保留**现 `.core` 摄入路径：

```text
content/localization/l_english/01_techs.core    # 每文件顶层 localization 对象：
localization {
    lang = "l_english"
    tech_rifling:0 "Rifling"                     # 复数槽：key:index "default"
    building_factory "Factory"
}
```

装载顺序 = 文件名字典序（确定性），同名 key 后载覆盖先载（沿用 VFS overlay 的
`insert_or_assign` 语义，`VirtualFileSystem.cpp:55-87`）。

## 4. 热重载（P1-5）

现况：ScriptedGui Blueprint 由活跃 mod 栈重建并自带 `checksum()`（`SCRIPTED_GUI.md:23-27`），
这是天然热重载锚点；缺的是内容事务层。

```cpp
// ContentLoader.hpp 增量 —— 内容事务
class ContentTransaction {
public:
    bool begin();                       // 快照当前 definitions/blueprints/本地化
    bool commit();                      // 校验通过则原子切换；失败回滚
    void rollback();
    [[nodiscard]] std::vector<ContentDiagnostic> staged_diagnostics() const;
};
```

- 仅 editor/dev 构建暴露；`thunder_editor` 持 VFS 监视线程（文件变更 → 事务重载 →
  校验失败回滚并回报诊断）。
- 与 `.thundercache`（`MOD_RUNTIME.md:160-166` 列为 Remaining）解耦：热重载先做
  源码级，缓存优化后行。

## 5. 存档可读导出（P1-7 · 补充 "二进制不可读"）

现况：存档为二进制 + zstd + 强校验（`SaveGame.cpp:458`），无文本形态。**不改存档
格式**（二进制是正确选择），而是加导出工具把调试面补上：

新工具 `src/apps/thunder_save_dump.cpp`：

```
thunder_save_dump --save <save.thundersav> --world <world.thunderworld>
                 [--out dump.txt] [--section GCT1|GLB1|ALL]
```

- 复用 SaveGame 解码（只读）；按 tagged section 输出人类可读文本
  （字段名 = 注册的 store 列名；Store 需暴露 `debug_dump(fmt)` 挂点，从既有
  checksum 遍历的同一路径取字段，避免维护两套字段表）。
- 价值：desync 排查（与 engine_checksum 二分配合，见规格 07 §3）、存档审阅、
  测试夹具生成（v1→v5 迁移用文本基线做 golden file）。

## 6. 验收测试清单

1. 宽容：含未知字段的 mod 默认装载成功（Warning 清单可查）；`--strict` 拒绝；
   Error 级语义错误永远拒绝。
2. lint：对 `07_lenient_diagnostics.core` 输出零 Error（strict 下按预期拒绝），
   JSON 与 markdown 两种格式字段齐全；退出码契约正确。
3. 本地化：en/ru/zh 复数选择在 `{count}` 下正确；`l_<lang>` 文件约定装载顺序
   确定性；富文本与插值共存不回归。
4. 热重载：事务 begin→改文件→commit 后 checksum 变化且运行态切换；注入坏文件
   → 回滚 + 诊断，运行态不变。
5. save_dump：dump 文本与 checksum 遍历字段一致（golden file）；GCT1 段能展开
   事件上下文。

## 7. 工作量（粗估）

宽容解析 2 周 + lint CLI 1 周 + 本地化复数/约定 1.5 周 + 热重载 3 周 + save_dump
1.5 周：**合计约 9 人周**。
