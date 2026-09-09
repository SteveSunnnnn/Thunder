# 规格 06 · 工程质量修补与门禁（P2-1 … P2-8）

> 锚点基线：2026-09-05。本规格全部为可立即执行的工程修补，不改变内容契约。

## 1. P2-1 Vulkan 后端纳入 CI（最高优先级质量盲区）

现况：`THUNDER_BUILD_VULKAN` 默认 OFF（`CMakeLists.txt:21,236`），CI 仅
`release-headless`（`CMakePresets.json:18-26`）。presentation 层 20,363 行（占全库
37%）在合并门禁中**从不编译**。

改动：
1. `.github/workflows/ci-linux.yml`、`ci-windows.yml` 各加一个 job
   `ci-vulkan`：`cmake --preset dev-desktop` + 编译（不跑 headless 测试外的 GUI
   测试）+ 对 `vulkan_*` 相关可编译目标做链接冒烟。
2. 若 Runner 无 Vulkan SDK/GPU：编译 + `THUNDER_BUILD_VULKAN=ON` 仅需头文件与
   SDK 链接库，Windows Runner 装 Vulkan SDK 即可；Linux 用 `libvulkan-dev` +
   swiftshader（ICD 冒烟可选）。
3. 验收：`ci-vulkan` 绿是 PR 合并前置条件（branch protection 与 ctest 门禁并列）。

## 2. P2-2 性能回归门禁

现况：bench 只 `add_executable` 未 `add_test`（`CMakeLists.txt:342-352`），无阈值断言。

改动：
1. bench 内加**阈值断言模式**：`--check-budget` 读取 `bench/budgets.json`
   （目标机器规格 + p95/均值预算，基线引用 `PERFORMANCE_BUDGET.md:9-31`）。
2. `add_test` 注册 3 个 budget bench（economy_bench / living_map_bench / thunder_bench），
   CI 用固定 runner 规格跑；本地用 `ctest -R budget`。
3. budget 默认宽松（避免 flaky），**恶化 >20% 即失败**；人工更新 budgets.json
   需在 PR 描述给出证据。

## 3. P2-3 本机工具链修复（迭代效率最大拖累）

现况：`/d/mingw64/bin/cmake` 退出 127（可执行文件缺 DLL/加载器问题）；本机验证
只能 `g++ -fsyntax-only`。

改动（脚本化，`scripts/windows/bootstrap_toolchain.ps1`）：
1. 检测 `cmake --version` 退出 127 → 用包管理器替换（`winget install Kitware.CMake`
   或 `python -m pip install cmake ninja`），并加入 `PATH` 最前。
2. 验证：`cmake --preset dev-headless && cmake --build --preset dev-headless &&
   ctest --preset dev-headless` 全绿作为 bootstrap 完成判据。
3. 文档：`docs/DEVELOPMENT.md` 补"本机最小构建"一节（现缺失，开发只能靠 CI）。

## 4. P2-4 时钟 checksum 加固（已核实为"覆盖存在，需防回归"）

审计初判"GameClock 不在 World::checksum"。**复核结论**：`World::checksum()` 确实
不含 clock（`simulation/kernel/World.hpp` 无 clock 成员），但
`ThunderEngine::engine_checksum()` 已折叠 `state.clock.checksum()`
（`ThunderEngine.cpp:327`），存档 runtime checksum 亦含 clock
（`SaveGame.cpp:451-456`）。因此真实风险是**使用面风险**，不是缺实现：

1. 新增回归测试：`tests/engine_tests.cpp` —— 构造两个仅 clock 不同的引擎状态
   （同 world、不同 `tick_index`），断言 `engine_checksum()` 不等；
   断言 `SaveGameCodec` round-trip 后 clock 漂移被 runtime checksum 捕获。
2. 约定加固：所有 desync 哨兵与多线程验证一律调 `engine_checksum()`，禁止直接
   拿 `World::checksum()` 当整局校验（注释写入 `World::checksum` 声明处）。

## 5. P2-5 StrongId 代际：审慎决策（修订审计建议）

审计指出 `PopStore.cpp:113` 创建时丢弃 generation、accessor 只查 bitmap 存活位
→ ABA 风险。**工程结论：不把 generation 塞进 StrongId 字段**——那会改变全部
存档布局、确定性遍历顺序与对外 ABI，收益低于成本。采用两层缓解：

1. **debug/测试构建**启用 liveness 严格校验：`validate_handle(world, id)` 在
   访问前校验 id 值域 + bitmap 位（`PopStore.cpp:176-185` 已具备
   `is_index_alive` 通道），把校验成本留在 debug。
2. **语义护栏**：对"外部持久引用"的 ID（事件上下文、存档 scope 引用）在装载
   后统一做 revalidate 遍历（SaveGameScriptSections 的 scope-reference validation
   已有雏形），发现 stale 即诊断而非静默指向。
3. release 路径维持零开销；不新增字段。

## 6. P2-6 调度并行度名实相符

现况：9 个 TickTask 仅 2 个 `ParallelSafe`（`ThunderEngine.cpp:74-104` 的
notifications_daily / research_weekly），且依赖链导致波次实际串行。

改动（不引入新并发原语，复用 TickScheduler 现模型）：
1. 任务依赖审计：把 economy_weekly 拆成可并行波次 —— 先市场分区级
   （`EconomyMarketPhases` 已在 chunk 内部并行），再周聚合；声明 store 冲突表
   （写 CountryStore 的列 → 排在依赖尾部）。
2. `TickScheduler` 输出波次统计（每波任务数/并行与否），进 `--check-budget`
   日志，防"悄悄退化回全串行"。
3. 验收：same-seed 下 2 线程 vs 8 线程结果 checksum 相等（既有确定性契约保证
   keyed RNG 与排序稳定性已具备）；tick p95 进入预算。

## 7. P2-7 测试框架统一

现况：6/27 套件用 `main()` 内联 `assert()`（`scripted_gui` / `asset_ui` /
`world_bootstrap` / `vector_map_and_ui` / `grand_strategy_advanced` /
`job_system_stress`），无 `test_*` 命名 → 失败定位弱、统计缺失。

改动：新增 `tests/test_main.hpp` 轻量宏层（不引第三方框架），迁移 6 套件到
`THUNDER_TEST(name) { }` 统一入口；`main` 汇总统计（total/passed/failed + 首败
文件:行）。job_system_stress 保持特殊（压力参数），仅统一入口形态。

## 8. P2-8 编辑器决策

现况：`thunder_map_editor` 仅 `--info`/`--paint-test` 合成笔触后退出；
`thunder_gui_editor` 仅打印 inspector（`src/apps/` 两文件为桩）。

决策（二选一，本次**选 A** 执行）：
- **A. 降级为 preview 工具**：CMake 默认不构建，`THUNDER_BUILD_EDITORS=ON` 显式
  开启；文件头标注 "preview — no persistence"；README 不再提编辑能力。
- B. 补全（约 8-12 周，依赖规格 01 写器落地后才能保存）——推迟到 M10 之后。

## 9. 验收汇总

| 项 | 判据 |
|---|---|
| P2-1 | ci-vulkan job 绿且为合并前置 |
| P2-2 | 3 个 budget bench 接入 ctest；>20% 恶化红 |
| P2-3 | bootstrap 脚本一键在本机构建 + 跑 27 套件 |
| P2-4 | 新增 clock-drift 回归测试绿；注释约定生效 |
| P2-5 | debug 构建 stale 引用测试通过；release 零开销（对比基线） |
| P2-6 | 波次统计可见并行收益；同种子 2/8 线程 checksum 相等 |
| P2-7 | 27 套件统一入口；失败报告带文件:行 |
| P2-8 | 编辑器默认不构建；文档如实标注 preview |
