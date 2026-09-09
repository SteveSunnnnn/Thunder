# Presentation 模块 — 最小接口用户文档

职责：把模拟快照画出来（Vulkan 世界地图/实体/UI），并把输入变成相机与命令。
渲染层永远不反向阻塞模拟（快照交换，非锁步）。

## 最小接口（只该用这些头）

| 头文件 | 提供 | 典型调用 |
|---|---|---|
| `thunder/presentation/render/vulkan/VulkanDesktopBackend.hpp` | 桌面渲染后端门面 | 创建/每帧 draw/`submit_ui(UiDrawList)` |
| `thunder/presentation/render/RenderSnapshot.hpp` | `SnapshotExchange` 模拟→渲染快照 | SPSC 三缓冲，渲染端只取最新 |
| `thunder/presentation/render/StrategicCamera.hpp` | 战略相机 | pan/zoom/pitch；`clamp()` 钉住 pitch∈[25°,88°] |
| `thunder/presentation/render/RenderQuality.hpp` | 画质分档 | `auto_quality_tier(gpu)` → `make_quality_settings` |
| `thunder/presentation/ui/StrategyUi.hpp` | UI 绘制列表 `UiDrawList` | `text/map_text/图元`；`map_space` 标签走 MSDF 地图文字管线 |
| `thunder/presentation/ui/FontAtlas.hpp` | MSDF 字体图集 | `.thunderfont` 装载与文字矩形 |

## 世界地图外观（谁画什么，2026-09-05 后）

- `world_map.frag` 单材质 uber-shader：饱和平面政治图（国色直铺+渐变）→
  发丝国界（~2px）+ 白州界 + 海岸墨线 + 经纬网 + 近景 PBR。
- 风格参数与证据链见 `audit/2026-09-05_v3_reference_map_comparison.md`。
- 国名标签：`VectorMapTypography`（脊线布字，Playfair 系，alpha 0.8）。

## 契约

- 渲染读快照，不写模拟状态；UI 点击转成命令交回 runtime。
- 着色器经 `THUNDER_SHADER_DIR` 加载（可被外仓覆盖）；新字段必须过 glslc。
- 地图新配色/线宽改动必须附像素级证据（裁切+剖面），见 audit 目录方法论。

相关文档：`docs/VULKAN_BACKEND.md`、`docs/UI_THEME.md`、`docs/PERFORMANCE_DESIGN.md`。
