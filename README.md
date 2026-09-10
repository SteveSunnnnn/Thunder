# Thunder

C++23 确定性大战略引擎。固定步长模拟 + SoA 数据布局 + ThunderScript VM + 数据驱动内容，
面向"全球尺度、十万级模拟群体、可连续缩放三维地图"这一类产品。

Thunder 只提供引擎能力，**不认识任何一款具体游戏**。游戏侧代码（组合层、 authored
内容、平台宿主）不属于本仓库。

## 目录结构

```text
src/thunder/    引擎库，唯一可分发产物（thunder_runtime）
src/apps/       引擎命令行工具（编译器、烘焙器、检视器、编辑器）
shaders/        GLSL 源码，由 glslc 编译为 SPIR-V
scripts/        GIS、资产烘焙、着色器、诊断与平台脚本
cmake/          共享 CMake 模块
tests/          28 个测试套件
bench/          性能基准
docs/           架构与子系统文档
assets/         字体源与已烘焙引擎资产
thirdparty/     Vulkan 头文件、SDL3、msdf-atlas-gen
```

### 引擎分层

`src/thunder` 按依赖层级划分，依赖只能自顶向下：

| 层 | 目录 | 职责 |
|---|---|---|
| L0 | `foundation/` | `base` `memory` `jobs` `io` `profiling` `geo`，无引擎内部依赖 |
| L1 | `scripting/` | ThunderScript 词法/语法/编译/字节码/VM/profiler |
| L2 | `content/` | `definition` `localization` `assets` `worldpack` |
| L3 | `simulation/` | `kernel` `world` `economy` `grand_strategy` `warfare` `research` `ai` `living` `gameplay` |
| L4 | `presentation/` | `render`（含 `vulkan` 后端）`ui` |
| L5 | `runtime/` | `engine` `save` `editor` |

## 构建

需要 CMake ≥ 3.25、支持 C++23 的编译器、Ninja。

```bash
cmake --preset dev-headless          # Debug
cmake --preset release-headless      # Release
cmake --build build/dev-headless
ctest --preset dev-headless --output-on-failure
```

Windows/MinGW 注意事项：

- 若 PATH 里的 `cmake` 静默失败（退出 127/无输出），换用任一可用安装，例如
  pip 的 `python -m cmake`，或 `C:/Users/<you>/AppData/.../site-packages/cmake/.../bin/cmake.exe`。
- MinGW 构建会把编译器自带的 `libstdc++-6.dll` / `libgcc_s_seh-1.dll` /
  `libwinpthread-1.dll` 复制到每个可执行文件旁（`thunder_bundle_runtime_dlls`），
  避免 Git Bash 等环境里旧版运行库经 PATH 遮蔽导致测试以 `0xc0000139`
  （STATUS_ENTRYPOINT_NOT_FOUND）加载失败。

可选能力由 CMake 选项控制：

| 选项 | 默认 | 说明 |
|---|---|---|
| `THUNDER_BUILD_TOOLS` | `ON` | 命令行工具 |
| `THUNDER_BUILD_TESTS` | 顶层时 `ON` | 测试与基准 |
| `THUNDER_BUILD_VULKAN` | `OFF` | Vulkan/SDL3 渲染后端（需 Vulkan SDK） |
| `THUNDER_WARNINGS_AS_ERRORS` | `OFF` | 警告即错误 |

推荐的可选依赖：`libzstd`（`.thunderworld` 压缩）、`libxxhash`（校验和）。

## 架构法则

1. 模拟不依赖渲染，渲染不得改写世界
2. 玩家与 UI 行为一律变成确定性 Command
3. 运行时数据面向数据布局，强类型 ID 索引稠密 SoA
4. 内容是数据不是 C++——引擎里出现国家硬编码视为缺陷
5. 昂贵派生状态反应式重算
6. 模拟工作表达为依赖 DAG
7. 确定性是功能，不是测试项
8. 昂贵 GIS 离线做完，运行期不解析 shp/GeoJSON
9. Mod 是一等公民
10. 引擎不认识某一款游戏
11. **北极星是经济与金融模拟**：产品的最终目标函数是经济增长与生活水平提高
    （POP 生活水平驱动人口与需求增长，再反哺经济）。不制作角色/朝代/统治类
    玩法系统；对外表现仅要求**地图渲染对齐 CK3 量级**。

法则 10 与"禁用全图栅格 atlas"由 `CMakeLists.txt` 在**配置期**强制：
`src/thunder/**` 中出现 `#include "game/..."` 直接 `FATAL_ERROR`。

详见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。

## 磁盘格式

二进制格式使用 `THUNDR` 前缀的 **8 字节定长魔数**，扩展名统一为 `.thunder*`
（`.thunderworld`、`.thunderasset`、`.thunderfont`、`.thundersav` …）。

魔数必须严格 8 字节——它们以 `std::array<char,8>` 存储并按 `memcmp(..., 8)` 校验。
修改魔数时须保持等长，并同步修补 `tests/fixtures/` 与 `assets/fonts/` 下二进制
文件的文件头。完整对照表见 `docs/ARCHITECTURE.md` 第 3.2 节。

## 文档

`docs/README.md` 是文档索引。顶层权威文档为 `docs/ARCHITECTURE.md`。
