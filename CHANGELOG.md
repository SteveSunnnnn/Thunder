# Changelog

## Thunder 1.0 Development — Realism package R1–R12 (employment stays province-local) — 2026-09-07

Per user approval of the 2026-09-07 realism review, with two decisions: employment
stays province-local (no commuting, R9 reviewed and rejected) and building
bankruptcy is added. All items verified by 28/28 suites in both build configs.

- **R1 — market-driven metal anchor.** `GoodDefinition.monetary_metal_key` binds a
  `category = currency` good to a metal numeraire (content:
  `monetary_metal = gold|silver`, requires currency category). Each weekly tick
  feeds the numeraire-averaged market price of bound bullion into
  `update_exchange_rates` (normalized by base price); the classical 1.0/15.5
  constants remain only as the no-bullion fallback. Bullion gluts now move
  metallic-standard parities. Regression:
  `test_metal_market_price_feeds_fx_anchor`.
- **R2 — tech spread at V3 order.** Legacy grand-strategy spread step 150 ppm →
  8000 ppm per weekly success (≈ a decade per spread technology, not five
  centuries). The rules-driven ResearchSystem path is content-parameterized
  and unchanged.
- **R3 — market-access wedge in settlement.** Buildings settle sales/purchases
  through their state's MAPI: up to −25% realized output price / +25% input
  price at zero access (identity at full access). Remote producers earn less,
  pay more, and anchor lower wages. Regression:
  `test_market_access_wedge_discounts_remote_producers`.
- **R4 — blockades throttle trade.** Each weekly trade pass scales cross-border
  legs by (1 − max blockade over sea zones the shipper controls). Regression:
  `test_blockade_throttles_cross_border_trade`.
- **R5 — construction consumes materials.** New content block
  `construction_goods <name> { input = { good quantity_milli } }` declares the
  per-point basket. Queued projects draw from their market's inventory, pay
  sellers into clearing, and stall Leontief-style on the scarcest leg;
  unfunded remainders are refunded in reverse order (escrow → pool →
  treasury). Empty basket keeps the money-only legacy path. Regression:
  `test_construction_consumes_materials`.
- **R6 — pool expansion enters the queue.** Auto-expansion seeds the building
  (escrow) and enqueues the full bill instead of instant level-ups; the queue
  draws contractor escrow first, then pool, then treasury, so the bill is
  charged exactly once. Regression:
  `test_investment_pool_expansion_queues_project`.
- **R7 — migration keeps identity + auto flows.** Completed flows merge into a
  destination POP with matching culture/religion/profession/need-profile or
  found a new POP carrying identity, habits and per-capita savings (sources
  keep a one-person remnant; empty provinces can receive). A weekly generator
  pushes bounded domestic flows (5% of idle hands, ≤1000, 4 weeks, ≤32/week,
  no duplicate pairs) from ≥10%-unemployment provinces to same-country vacancy
  provinces. Wired into the engine's grand-strategy weekly task. Regression:
  `test_migration_preserves_identity_and_auto_flows`.
- **R8 — sovereign restructuring.** A fourth consecutive missed weekly service
  writes debt down 30%: new `BankStore::write_down_sovereign_bonds` takes the
  pro-rata bank share through equity (bank-crisis channel), the saver pool
  takes the rest, rating → D, exclusion clock → 52 weeks. Regression:
  `test_sovereign_restructuring_writes_down_debt`.
- **R10 — bankruptcy.** Settlement collects buildings pinned at the credit
  limit while operating at a loss. Serial resolution: multi-level buildings
  shed one level per loss week (5 weeks of wages recovered from clearing);
  one-level buildings die only with demonstrably failed bank debt
  (`BankStore::building_loan_status` non-performing/charged-off), so ordinary
  credit smoothing never kills a young business. Residual books go to clearing
  (identity-exact), bank loans charge off next bank run, POP employers cleared
  in one pass. Regression:
  `test_bankrupt_building_downsizes_and_dies_on_debt_failure`. Also fixed: the
  money-loop test now skips dead slots (checked store accessors correctly
  reject them).
- **R11 — companies own and earn.** New `companys_mut`/`company_cash`/
  `add_company_cash`. Settlement splits dividends pro-rata over ownership
  stakes into company cash (pool keeps the rest + rounding dust) and folds
  serially in stable stake order; companies spend cash on their own queued
  expansions with the same escrow mechanics. Regression:
  `test_company_receives_ownership_dividends`.
- **R12 — inventory carrying cost.** Settled inventory bleeds 0.1%/week into
  market clearing; `EconomyTickProfile.inventory_carry_cost_milli` names the
  leg so `unexplained_monetary_delta_milli` stays exact. The money-loop test
  now asserts the audited identity (delta == −carry, unexplained == 0).
  Regression: `test_inventory_carry_cost_is_audited_sink`.
- Content chain: `good.monetary_metal`, `construction_goods` block, and their
  bind validations covered in `test_economy_definitions_are_script_driven_and_bind_atomically`.

## Thunder 1.0 Development — Wage attribution, sovereign credit scale, growth overflow fixes — 2026-09-07

- **Wage payment is now attributed per hire segment** (`EconomySystem::employment`).
  A POP whose workers split across several buildings previously had its ENTIRE
  headcount paid by the LAST (lowest-wage) building that hired any remainder,
  at that building's offer: earlier employers received free labour while their
  headcounts still produced output, and the last employer paid phantom wages
  for workers it never hired — distorting per-building profitability, the
  MRP-anchored wage feedback, and construction/expansion targeting. Each hire
  segment now debits its own building's cash (clamped by the building credit
  line) and credits the POP's cash/income at the hiring building's offer.
  `consumption()` no longer moves wage money; it reads the settled income.
  Regression: `test_wages_paid_per_building_at_own_offer` pins exact per-building
  cash deltas and the POP's segment-summed income.
- **Sovereign credit ratings compare debt against ANNUAL GDP**
  (`CountryStore::evaluate_credit_rating`, `borrowing_capacity_milli`). The
  `gdp_` column is the weekly value-added flow, but the rating bands
  (AAA < 30%, …, CCC ≥ 250%) and the 2.5x borrowing ceiling are annual-ratio
  concepts — comparing a debt stock against one week of GDP made every
  meaningful sovereign borrower read as CCC and capped AAA borrowing at
  ≈4.8% of annual GDP. Both paths now annualize with the same factor 52 used
  by weekly debt service. The existing test's "180% of GDP → BB" intent is
  preserved with explicit annual units.
- **Population growth hardening** (`EconomySystem::population_growth`):
  the fractional milli-person accumulator now sums in int64 and the applied
  population is clamped to the uint32 column range, so a near-4-billion-person
  POP saturates instead of overflowing the int32 remainder column or wrapping
  the population. Regression: `test_population_growth_huge_pop_saturation`.
- **`AddTreasury` commands settle in milli** (`CommandQueue::apply_all`): the
  double-unit command value is converted once at the boundary and applied via
  `add_treasury_milli` instead of round-tripping through the derived double
  treasury column (`llround(treasury*1000)` re-rounding could lose milli
  precision on large balances).
- Docs: `ECONOMY_ARCHITECTURE.md` weekly-phase list updated for the employment-
  phase wage settlement, and the stale "POPs retain an assigned employer /
  no job search" boundary note replaced with the actual weekly hiring model.
- Verified: 28/28 suites green (`dev-headless`), including the multi-worker
  determinism and money/material closed-loop conservation suites;
  `build-vulkan` runtime target rebuilds clean.

## Thunder 1.0 Development — glTF importer + quantized mesh layout (near-view asset pipeline, head) — 2026-09-06

- The engine side of the near-view asset pipeline begins: the head of the chain
  that turns authored `.glb` architecture kits into engine-ready meshes is now
  in the library and fully headless-testable (docs/ASSET_ACQUISITION_REQUIREMENTS.md
  is the matching asset acquisition list).
- **Quantized runtime mesh layout** (`content/assets/MeshData.*`): implements
  the storage contract of the 3D asset spec §1.2 — float32 chunk-local
  positions, octahedral-encoded normals (2 × int8, ±127), half-float UVs,
  dynamic uint16/uint32 indices, bounds for culling. Encode/decode with
  zero-copy `QuantizedMeshView` accessors; malformation (bad magic, truncated
  sections, out-of-range indices) rejected at both bake and load time.
- **glTF 2.0 Binary importer** (`content/assets/GlTFImporter.*`): .glb
  container parsing, an embedded minimal JSON DOM reader, accessor decoding
  across int8/uint8/int16/uint16/uint32/float32 with normalized and
  interleaved-view support; triangle-list primitives concatenated with
  base-vertex rebasing. Explicitly out of scope and rejected with diagnostics:
  sparse accessors, non-triangle modes, skins/animation (phase 2).
  Integer index paths bypass float round-trips (uint32 indices exceed the
  float mantissa). Bugs caught by the new tests before ever shipping: a
  signed/unsigned comparison turned half-float 0.0 into infinity, and the
  header size was 12 bytes short of its own layout.
- **Cooker integration**: `thunder_asset_cooker` manifest rows of kind `mesh`
  with `.glb` sources are now imported and baked into the quantized layout
  instead of being stored as opaque bytes.
- Tests: new `thunder_mesh_pipeline_tests` suite (28 suites total) covering
  synthetic-glb import, malformed-container rejection, octahedral normal
  round-trip accuracy, quantized encode/decode fidelity, and the full
  glb → cook → AssetPack → read → decode path.

## Thunder 1.0 Development — Natural population growth driven by standard of living — 2026-09-06

- **Direction decision recorded as architecture law 11** (README): the engine's
  north star is economic and financial simulation with economic growth and
  living standards as the final goal function. No character/dynasty/ruler
  gameplay systems; CK3 alignment is map presentation only.
- **Natural population growth closes the growth loop.** The simulation had no
  births or deaths — POPs only changed through war casualties and migration,
  so rising living standards could never grow the economy endogenously. A new
  weekly `EconomySystem::population_growth` phase (first phase of
  `run_weekly`) now drives deterministic, RNG-free demographic dynamics with a
  realistic **demographic transition**:
  - fertility starts high (≈ +42/1000/yr) and only begins declining once a
    POP is firmly above subsistence, falling to ≈ +10/1000/yr under
    prosperity — the generational lag is modelled explicitly;
  - mortality responds immediately: ≈ +41/1000/yr at subsistence, falling
    linearly to ≈ +9/1000/yr under prosperity;
  - net growth therefore peaks mid-transition (≈ +0.73%/year) and declines
    toward ≈ +0.1%/year as wealth completes the transition — wealth does not
    monotonically raise growth;
  - below crisis living standards, mortality spikes and the POP declines
    (≈ −2.2%/year at zero), with a remnant floor of one person keeping
    destitute POPs recoverable.
- **Fractional growth is exact for POPs of any size**: each week's growth
  accumulates in a new `growth_progress_milli` column (milli-persons) and is
  applied when it crosses a whole person, with the remainder carried — no
  rounding stagnation for small POPs. The column participates in
  `PopStore::checksum` (desync coverage) and compaction/destroy lifecycle,
  and persists through a new tagged save extension.
- Tests: `test_population_growth_tracks_standard_of_living` (exact weekly
  deltas for transition-peak/affluent/subsistence/destitute POPs, the hump
  assertion that mid-transition outgrows both edges, remainder carry, remnant
  floor, settlement aggregation of grown sizes), `test_population_growth_
  section_roundtrip` (growth state round-trips and matches the world checksum).

## Thunder 1.0 Development — War/Front/Army scopes (spec-04 batch two) — 2026-09-06

- `ScopeType` grows to nine domains: Country, State, Province, Pop, Market,
  Building, **Army, Front, War** — the GrandStrategyStore military records
  (1.4k+ lines of implemented-but-unreachable state machine data) are now
  scriptable end to end.
  - Navigation: country→armies (own units), country→fronts (either side),
    country→wars (**active only**; the global `every_war` sees all records),
    war→fronts (attacker/defender pair in either order, mirroring the weekly
    warfare pass), state→armies; `owner =`/`state =` selectors extended.
  - Primitives: triggers `army_manpower_above`, `army_organization_above`,
    `front_progress_above`, `war_score_above`, `war_weeks_above` and the
    boolean `war_active = yes/no` (first typed-boolean builtin trigger);
    effects `army_add_manpower` (saturating), `set_army_organization`,
    `shift_front_progress` / `shift_war_score` (clamped to the ±100'000 milli
    relation scale via the same bounds as the weekly pass).
  - Expression value sources: `army_manpower`, `army_organization`,
    `front_progress`, `war_score`, `war_weeks` — whole-unit conventions
    matching the pop/building columns (milli ×1000, organization 0..1).
  - Store: `fronts_mut()` / `wars_mut()` added alongside the existing mutator
    spans; no layout or checksum changes (record vectors are unchanged).
- Tests: `test_war_front_army_scopes_and_primitives` (gated country→army
  reinforcement touching only own units, war→front pair matching in either
  order, war score shift, boolean trigger + infix comparison, expression value
  on the front scope).
- Reference content: `reference-content/examples/13_war_front_army_scopes.core`.
- Docs: `THUNDER_SCRIPT.md` "War, front and army scopes" section.

## Thunder 1.0 Development — Building scope (first spec-04 domain expansion) — 2026-09-06

- `ScopeType::Building` is the sixth scriptable domain, unlocking the building
  SoA store (level/employees/wages/cash/profit) to content for the first time:
  - Scope plumbing: `ScopeRef::building`, `ScopeResolver::valid` (slot-liveness),
    `all()` and `children()` navigation — buildings are owned through their
    market's country, with province/state links from the cold column;
    `owner =`/`market =`/`province =` selectors work from a building.
    Same-type `every_building`/`ordered_building`/`random_building` iterators
    and all new control flow (`limit` gates, `while`, `random_list`, infix
    comparisons) work on the domain immediately.
  - Built-in primitives: triggers `building_level_above`,
    `building_employees_above`, `building_profit_above`, `building_cash_above`,
    `building_wage_above`; effects `set_building_level`, `set_building_employees`,
    `building_add_cash` (milli-backed like the store columns).
  - Expression value sources: `building_level`, `building_employees`,
    `building_profit`, `building_cash` — usable in `expression = (...)`
    scripted values and infix comparisons on the building scope.
- Persistence: `validate_persistent` routes through `ScopeResolver::valid`
  (already extended), so building scope references in retained gameplay
  contexts validate and checksum correctly; old saves are unaffected.
- Tests: `test_building_scope_and_primitives` (gated every_building from a
  country, ordered_building from a market with expression order_by, an infix
  arithmetic trigger on the building scope, an expression scripted value).
- Docs: grammar rule clarified — expression values must start with `(` (a bare
  word value is a single symbol); example 10 updated accordingly.

## Thunder 1.0 Development — `while` loops + `else_if` chains — 2026-09-06

- ThunderScript effect blocks support the remaining control-flow forms:
  - `while = { limit = { <conditions> } <effects...> }`: the condition tree is
    re-evaluated in the current scope before every pass and each pass is charged
    to the VM work budget — a limit that never fails terminates deterministically
    with the budget error (no hang, no nondeterminism). Composes freely with
    `if`, `random_list`, iterators and value expressions.
  - `else_if = { limit = { ... } ... }` chains attach to the innermost preceding
    `if` (compiled as nested ifs in `else_children`; first matching branch wins)
    with a trailing `else` closing the chain. Sibling layout matches Clausewitz.
  - IR: `ScopedEffectKind::While`; `else_if` introduces no new IR (it lowers to
    nested `If` nodes, so saves/checksums are untouched).
- Tests: `test_while_loops_and_else_if_chains` (five-pass variable loop, budget
  termination of a never-failing loop, first-match-wins across an else_if chain
  including the else fallback).
- Reference content: `reference-content/examples/11_control_flow_while_and_else_if.core`.
- Docs: `THUNDER_SCRIPT.md` "Loops and `else_if` chains" section; the gaps list
  now names arrays/mixed containers, Date/Duration and scripted random_list
  weights as the remaining language items.

## Thunder 1.0 Development — Value expression grammar front-end (arithmetic trees + infix trigger comparisons) — 2026-09-06

- The `ScriptValueBytecode` VM (Add/Sub/Mul/Div/Neg, Eq/Ne/Lt/Le/Gt/Ge, And/Or/
  Not/If, Max/Min/Clamp) was fully implemented but had no producer. The grammar
  front-end now ships, closing the largest remaining language gap:
  - `scripted_value` accepts `expression = (population * 0.5 + gdp * 2) / 1000`:
    operands are builtin value sources, `value:<name>` scripted values,
    `var:<name>` variables and constants; `+ - * /`, parentheses, unary minus
    with standard precedence. Bytecode programs set `uses_bytecode` and skip the
    legacy `source*multiply+add` path (unchanged for existing content).
  - Trigger blocks accept infix comparisons — `population > 1000`,
    `value:income - 50 >= 40`, `pop_size <= 100` — compiled into the new
    `ScopedConditionKind::SourceCompare` (both sides value bytecode, evaluated
    in the current scope, tolerance semantics matching the bytecode ops). This
    retires the "one C++ primitive per comparison shape" pattern.
  - `PushSource` op added (shared `source_value()` helper with the legacy path,
    so expression operands read exactly the same world columns); the dormant
    `PushScriptedValue` no-op was implemented for real (it previously pushed
    nothing — a latent wrong-value bug once bytecode producers existed) with a
    call-frame guard and depth-carrying recursion that preserves the work budget.
  - Link validation extended: `value:` refs inside expression bytecode are
    checked for existence, scope match and call cycles; `instruction_bytes`
    accounts for bytecode pools and the new iterator-filter/random_list IR.
- Parser: comparison tokens (`<`, `>`, `<=`, `>=`, `!=`), symbolic expression
  AST (`ScriptValueKind::Expression`), trigger comparison lines where the
  property key becomes the first expression operand, and constant folding that
  now runs through the same symbolic path (byte-identical left-associative
  results; division by zero in constant expressions remains a hard parse error).
  `==` is intentionally not a line form (collides with `key = value`).
- Tests: `test_value_expressions_and_comparisons` (expression scripted values
  against live world state, folded constants, both comparison directions,
  variable operands, unknown-operand diagnostic).
- Reference content: `reference-content/examples/10_value_expressions_and_comparisons.core`.
- Docs: `THUNDER_SCRIPT.md` documents both forms; the "Remaining major gaps"
  list drops the value-grammar item (remaining: `while`, arrays/mixed
  containers, Date/Duration, scripted random_list weights).

## Thunder 1.0 Development — Iterator gating, global same-type iteration, `random_list`, test-gate DLL fix — 2026-09-06

- ThunderScript iterators accept a Clausewitz-style candidate gate
  `limit = { <conditions> }`, distinct from the numeric `limit = N` count cap.
  - IR: `CompiledScopedEffect::has_iterator_condition` (effect side, condition
    reused from the `if` slot) and `CompiledScopedCondition::iterator_filter`
    (trigger side); both compile the block through the scoped-condition algebra
    in the iterated scope.
  - VM: failing candidates are excluded before Every/Ordered/Random application
    and before collection iteration, on both the effect and trigger sides
    (filter evaluations are charged to the work budget; `random_*`/`any_*` draw
    only from passing candidates; `ordered_*` gates before order_by/offset/limit).
- Same-type scope iteration is now world-global (Clausewitz semantics):
  `ScopeResolver::children` with `s.type == target` enumerates ALL instances of
  the target type instead of returning the current scope only — `every_country`
  inside a country scope used to iterate just that country, making world-wide
  content unexpressible. Cross-type navigation (owned states/provinces/markets,
  pop filtering) is unchanged.
- `random_list = { <weight> = { <effects...> } ... }`: weighted single-branch
  random effects.
  - Parser: block children may now carry numeric keys (the weight); previously a
    hard parse error, so no existing content changes meaning.
  - IR: `ScopedEffectKind::RandomList` + `Group` with per-branch weights;
    compiler rejects non-numeric branch keys with a line-numbered diagnostic.
  - VM: cumulative-weight draw against the keyed deterministic RNG (new
    `deterministic_draw` full-range helper; `deterministic_index` recipe is
    byte-stable so existing random-iterator draws are unchanged).
- Test gate repaired on Windows: MinGW builds now bundle the compiler's own
  `libstdc++-6.dll`/`libgcc_s_seh-1.dll`/`libwinpthread-1.dll` next to every
  executable (`thunder_bundle_runtime_dlls` in `cmake/ThunderWarnings.cmake`).
  Root cause of 15/27 local ctest failures with `0xc0000139`
  (STATUS_ENTRYPOINT_NOT_FOUND): Git Bash's older `/mingw64/bin` runtime
  shadowed the GCC 15.2 runtime through PATH; the app-directory copy wins DLL
  search order, making the gate environment-independent.
- Tests: `test_iterator_limit_condition_filtering` (effect/trigger gating,
  ordered gate+cap, empty-set semantics), `test_random_list_weighted_branches`
  (determinism, branch reachability, malformed-weight diagnostic).
- Reference content: `reference-content/examples/09_iterator_gating_and_random_list.core`.
- Docs: `THUNDER_SCRIPT.md` documents both idioms and trims the gaps list
  (remaining: `while`, arrays/mixed containers, comparison operators, scripted
  random_list weights, value-expression grammar front-end).

## Thunder 1.0 Development — Script conditional effects (`if`/`else`) + docs sync — 2026-09-06

- ThunderScript `script` effect blocks now support conditional effects:
  `if = { limit = { <conditions> } <effects...> else = { <effects...> } }`.
  - IR: `ScopedEffectKind::If` with a compiled `CompiledScopedCondition`
    (limit children under an `All` group) and an `else_children` vector on
    `CompiledScopedEffect`.
  - Compiler: `compile_scoped_effect_node` handles `if`/`limit`/`else` (an
    `if` without `limit` is a content error); `is_advanced_effect` routes
    `if` blocks through the scoped-effect path so they nest inside scope and
    iterator blocks.
  - VM: `apply_scoped_node` evaluates the condition tree in the current scope
    and executes exactly one branch.
  - Test: `test_script_conditional_effects_if_else` asserts both branches
    against live world state (treasury gate + POP wealth outcomes).
- Docs sync pass: `THUNDER_SCRIPT.md` documents the `if` grammar and drops
  conditional effects from the gaps list; `SCRIPT_FIRST_CONTENT.md` gains the
  goods `category` field and arithmetic-folding note; `FINANCE_BANKING.md`
  documents the FX dual-path settlement (`coin_commodity_value_milli`) and the
  `category = currency` metal-price linkage.

## Thunder 1.0 Development — ThunderScript arithmetic folding + Vulkan CI job — 2026-09-06

- ThunderScript numeric fields now accept constant arithmetic expressions
  folded at parse time: `base_price_milli = (900 - 100) / 4 * 10`, with `+ - *
  /`, parentheses, unary minus, standard precedence. Binary `+`/`-` require
  surrounding whitespace (attached signs keep legacy literal meaning);
  division by zero and non-constant operands are parse diagnostics. Applies
  engine-wide to every numeric field with zero runtime cost.
- CI: added a `vulkan` job to `ci-linux.yml` — configures
  `-DTHUNDER_BUILD_VULKAN=ON`, builds and runs ctest, closing the
  presentation-layer (37% of codebase) merge-gate hole.
- Completeness check refreshed against the script stack: the bytecode VM
  already implements full arithmetic/comparison/conditional opcodes
  (`ScriptValueOp` incl. `If`), condition algebra (All/Any/Not/Iterator) is
  compiled, Date literals exist for history patches — the remaining language
  gap is precisely the expression-grammar front-end (operand references) and
  conditional effects (`ScopedEffectKind::If`), now documented in
  `docs/THUNDER_SCRIPT.md` "Remaining major gaps" with exact IR pointers.

## Thunder 1.0 Development — Per-module minimal API user docs + doc cleanup — 2026-09-05 (late)

- Added `docs/modules/` — one user document per engine layer exposing the
  module's minimal calling surface (facade headers, core types, minimal
  examples, contracts, forbidden internals): `foundation.md`, `scripting.md`,
  `content.md`, `simulation.md`, `presentation.md`, `runtime.md`. Hosts
  integrate starting from `runtime.md`; ARCHITECTURE.md links the set.
- Deleted `docs/script_docs.md`: stale pre-rename "CoreScript API" listing
  with outdated builtin counts, superseded by `THUNDER_SCRIPT.md` and the
  in-code `ScriptRegistry::make_builtin` registry; unreferenced anywhere.
- Removed the temporary unpacked SDL3 headers from the system temp directory
  (used only for syntax checks).

## Thunder 1.0 Development — Monetary-metal goods class + FX coin commodity valuation — 2026-09-05 (late)

- `GoodCategory::Currency` added as the fifth goods category (`category =
  currency` in script): monetary metals (gold/silver bullion) and coinage are
  now a first-class goods side of the monetary standard. Their market price is
  the metal price that anchors `CurrencyStore` parities.
- New `CurrencyStore::coin_commodity_value_milli(coin, gold_price_ppm,
  silver_price_ppm)`: commodity-side valuation of a foreign coin (melt value
  from mint parity x market metal price). Gold/Silver standards use own metal
  content; bimetallism takes the richer leg (melt side of Gresham's law — the
  circulation rate tracks the cheaper leg); fiat is 0. Together with
  `convert()` this forms the dual settlement path: foreign money circulates
  as commodity-valued coin, the local unit of account stays fixed to the
  standard, and the spread drives the existing specie-flow arbitrage.
- Design rationale and data flow recorded in
  `audit/2026-09-05_monetary_standard_fx_design.md`.
- Tests: `category = currency` parse/bind asserted in thunder_tests; new
  `test_coin_commodity_value_milli` covers gold/silver/bimetallic/fiat and
  unknown-currency valuations at the standard 15.5:1 ratio.

## Thunder 1.0 Development — Precious-metal currency class + FX-as-currency design — 2026-09-05 (night)

- Added `CurrencyClass` enum (`National` / `Metal`) and canonical metal numeraire
  keys `core.metal.gold` / `core.metal.silver`. Precious metals are now first-class
  currencies registered at bootstrap, acting as the FX anchor for metallic-standard
  national currencies. Class is derived from the key (no new disk field), so saves
  stay 100% round-trip compatible. `convert()` now works between any national
  currency and the gold/silver numeraire.
- `CurrencyStore::update_exchange_rates` now treats Metal numeraires as the
  authoritative metal-price reference (the supplied `gold_price_ppm` /
  `silver_price_ppm` pin them) and skips mint-parity / specie-flow / trade-pressure
  for metal records. National mint parity is computed from these references, keeping
  existing numeric behaviour for the default gold=1'000'000 case.
- Added `currency_class()` / `is_metal_currency()` accessors and a
  `target_rate_ppm()` getter.
- Design decision recorded in `audit/2026-09-05_currency_metal_fx_design.md`:
  **foreign exchange circulates as a currency, not a commodity**; precious metals
  are dual-natured (Metal currency for settlement + optional bullion `good` for
  commodity trade), reconciled by specie-flow arbitrage.

## Thunder 1.0 Development — Script-authored consumer goods + coastline ink line — 2026-09-05 (late)

- `good` script definitions now accept a `category` field
  (`staple` / `luxury` / `military` / `industrial`), giving consumer goods the
  Victoria 3-style classification. Unknown categories and non-symbol values
  are rejected at ingest with a line-numbered diagnostic; omitted category
  defaults to `staple`. The category flows through
  `DefinitionDatabase::bind_economy` into `EconomyDefinitions::GoodDefinition`
  as a checksum-stable byte (`GoodCategory`), exposed via
  `good_category_name()` for stores and diagnostics.
- Added `reference-content/examples/08_consumer_goods.core`: a full
  script-authored consumer-goods chain (staple/luxury/military goods, three
  POP need profiles consuming them).
- Extended `test_economy_definitions_are_script_driven_and_bind_atomically`
  to cover category parsing, binding and defaults.
- Map line quality: added a crisp engraved coastline ink seam to the flat
  political map (`world_map.frag` §6) — a ~1px dark line hugging the
  land/sea boundary slightly biased into the water, complementing the
  paper-cutout sea shadow, fading toward the near view where foam takes
  over, and honouring the coast-off debug bit.

## Thunder 1.0 Development — Architecture tidy pass one: dead shaders, dedup, per-frame waste — 2026-09-05 (night)

- Deleted three orphan shaders with zero runtime loaders and zero references
  (`terrain.frag`, `ocean.frag`, `political_overlay.frag`; the renderer loads
  13 shaders, listed in VulkanRuntimeRenderer.cpp:268-287).
- Deduplicated `decode_utf8` (byte-identical copies in FontAtlas.cpp and
  VulkanUiStaging.cpp) into the shared header
  `thunder/presentation/ui/Utf8Decode.hpp` (bounds-guarded variant kept).
- Removed the per-frame `update_wired_terrain_state` (function, declaration,
  draw-path call block): it recomputed sky environment, cloud shadow, clipmap
  build and forest/river/water evaluations every frame and discarded every
  result except one diagnostics line, with a constant sun direction.
- Trimmed `ensure_wired_terrain_pipelines` smoke blocks (Gerstner water,
  forest wind, river rapids, clipmap build, fixed-position demo particles,
  border ribbon) that existed only to "prove wiring".
- Removed seven hardcoded diagnostic lines (`wired_forest_wind_ok=1`,
  `terrain_parity_fixed=1`, and a `(x > 0 ? 1 : 1)` tautology).
- Full consumer-matrix audit and the Phase 2 removal plan (engine-dead
  classes, the CPU parity family, ~4.5k lines) are recorded in
  `audit/2026-09-05_code_architecture_tidy.md`.

## Thunder 1.0 Development — Map labels aligned to official Victoria 3 name defines — 2026-09-05 (night)

- Researched the official Victoria 3 map-name parameters from the wiki Defines
  page (`MapName` / `JominiMapGraphics`) and pixel-sampled label inks on
  official screenshots; findings and the full alignment table are recorded in
  `audit/2026-09-05_v3_reference_map_comparison.md` §8.
- Country label ink opacity raised from 66% to **80%** to match the official
  `MAX_OPACITY = 0.8` define (semi-transparent by design; sampled glyph cores
  on screenshots confirm ~0.75-0.8 effective alpha over near-black warm ink).
- Letter-tracking upper bound tightened from 1.65 to **1.60**, mirroring the
  official `COUNTRY_NAMES_MAX_STRETCH_FACTOR = 1.6` (names stretch to fit their
  spine, never squeeze).
- Verified as already aligned with the official design: Playfair Display as
  the map-name typeface, zero thickness bias (never emboldened), per-zoom
  visibility windows, spine-fitted rotated placement with wide tracking.
  Sea-name labels remain a data-side follow-up.

## Thunder 1.0 Development — V3 pixel-verified map polish: gradients, hairline borders, seam shadows — 2026-09-05 (late)

- Pixel-verified three user-flagged Victoria 3 details on official 1920x1080
  screenshots (3x crops + per-pixel profiles in `audit/ref/`) and implemented
  all of them in `shaders/world_map.frag`:
- Country borders thinned to V3 hairline weight: stroke half-width 1.8px →
  1.1px (~2px full line), ink alpha 0.88 → 0.80. The old stroke was twice the
  measured V3 width (1-3px).
- Border seam shadows: the dark band flanking the border now matches the
  measured profile — 12px soft gradient at ~11% peak darkening (was 16px at
  ~14%), reading as a pressed paper seam on both sides of the line.
- Gradients replace flat fills: broad tonal drift (~1000km wavelength, ±5%),
  per-country lightness bias (±3%) and a fine canvas grain (±2.5%) are layered
  onto the political pigment — V3 fills visibly drift (#68a868 → #80b682
  inside France) and carry a linen texture.
- Ocean switched to the measured V3 model: near-white warm-gray paper sea
  (`srgb(0.83,0.85,0.83) → (0.72,0.76,0.75)`) when zoomed out, blending to the
  nautical blues as the camera descends (V3 mid-zoom Atlantic sampled
  #285070). Waterline ink now fades with altitude (`mix(0.08, 0.45)`) and the
  pale sea gained a subtle mottle.
- Land drop-shadow onto the sea: soft gray gradient band hugging coastlines in
  the far view (V3 paper-cutout depth cue).
- Internal state borders (white, hairline, translucent) restored via a new
  `out_prov_stroke` isosurface on province IDs inside
  `sovereign_border_smooth`; they yield to the country ink line and fade out
  as the camera descends into 3D. Dead `loc_stroke` code removed.
- Verified with glslc.

## Thunder 1.0 Development — Flat political map: parchment removal + label typography — 2026-09-05

- Removed the antique parchment art direction from the live world map
  (`shaders/world_map.frag`): the rag-paper base, aging stains, cellulose
  fibers, fold creases, archival vignette and copperplate mountain hachures
  are gone. The far view is now a saturated flat political map — official
  country pigment laid on directly, neutral gray for decentralized land,
  desert tint reduced to a terrain hint (0.72 → 0.35) so political colors
  dominate, gentle neutral hillshading only.
- Ocean far view recolored from celadon/parchment wash to a saturated
  cerulean-to-nautical blue; engraved waterline ink reduced 0.88 → 0.30
  (subtle chart feel kept for map readability).
- The mahogany study-table/brass frame at the polar margins is replaced by a
  printed-chart ink neatline with a single light rule.
- Deleted the unwired `PaperMapTransition` module (parchment style config +
  transition evaluator that was only `(void)` smoke-tested and never fed to
  the shader), including its CMake source entry and the
  `VulkanDesktopBackend` include/member/smoke-test call sites.
- Map-label typography aligned with the new direction (`ui_msdf.frag`
  mapTextMode + `VectorMapTypography`): neutral cool back-glow replaces the
  warm parchment halo (0.45 → 0.38 contribution), and the engraved country
  ink drops to ~66% alpha (`0xf2` → `0xa8`) for semi-transparent, non-bold
  serif labels embedded in the map.
- Verified with glslc (world_map.frag/vert, ui_msdf.frag) and g++
  `-fsyntax-only` on the touched translation units.

## Thunder 1.0 Development — UI polish round two: camera, fullscreen, interaction — 2026-08-29

- Wired the strategic camera into the map renderer: `fullscreen.vert` now
  takes a uv-viewport push constant and the desktop shell converts
  `StrategicCamera` state (pan/zoom) into it every frame; middle/right-drag
  pans and the wheel zooms the live map. The camera starts at a continental
  overview altitude instead of a 250 km close-up.
- Desktop shell now launches fullscreen by default (`CORE_WINDOWED=1` opts
  out), matching grand-strategy conventions.
- Scripted GUI interaction states: hover and pressed are tracked per node
  (`GrandStrategyGui::set_hovered` / `set_pressed`), buttons brighten and
  re-frame on hover, and tooltips resolve from `hud.tip.*` localization keys.
- Typography scale raised for 1080p readability (body 15, window titles 19,
  stat values 16); top bar reworked into flag plate plus circular stat chips
  with icon medallions, and the navigation rail into circular icon buttons.
- Render-quality tiers (`RenderQuality`, FXAA, MSAA, HDR tonemap, depth
  attachment) and the 3D dynamic flag landed alongside; benchmark harness
  scenes added under `src/game/harness/` (see `docs/RENDER_OPTIMIZATION.md`).
- MSVC toolchain now builds and passes the full 27-test suite locally;
  MinGW remains affected by the environment loader issue noted previously.

## Thunder 1.0 Development — Unified Victorian grand-strategy UI theme — 2026-08-28

- Added the engine-wide design-token system `UiTheme`
  (`src/thunder/ui/UiTheme.hpp`): backgrounds, borders, accents, text,
  interaction states, shadow tiers, materials (wood / parchment / leather /
  brass / wax), typography scale, compact metrics and motion budgets. The
  default `UiTheme::victorian()` drives every drawing primitive; a draw list
  with no theme installed still renders the house style.
- Added smooth shading primitives: `quad_gradient` (per-vertex interpolated
  gradients, no banding) and `drop_shadow` (stacked penumbra layers); all
  materials and components were rebuilt on them, replacing hard-edged offset
  shadows and banded gradients.
- Expanded the generic component set: `ornate_header`, `window_frame`, `tab`,
  `dropdown_row`, `checkbox`, `radio`, `slider`, `scrollbar`, `input_box`,
  `list_row`, `table_header_cell`, `stat_row`, `notification_card` (five
  severity tiers), `modal_window`, `corner_ornaments`, `divider_ornament`,
  `separator` — all theme-driven with hover/pressed/selected/disabled/focus
  states and right-aligned numeric columns.
- Scripted GUI: panels now declare a surface role via `style =`
  (standard/wood/parchment/leather/recessed) and an optional `text` field
  rendered as an ornate header band; the painter maps roles onto theme
  materials. The bundled sample HUD (`content/base/ui/main.coregui`) was
  restructured into a wood top bar with recessed stat cells, wood navigation
  rail, parchment page windows with titled headers, and a parchment right
  context panel.
- Tooltip stack themed end to end (parchment body, leather title plaque,
  gold term links, separator rule, depth/lock indicators).
- Number presentation utilities: `ui_format_number` (thousands separators,
  decimals, compact K/M/B), `ui_format_delta`, `ui_delta_color`.
- Bundled OFL-licensed MSDF font atlases (`assets/fonts/`): Playfair Display
  Bold (`ui_display`) and EB Garamond Medium (`ui_body`); the desktop shell
  auto-loads `ui_body` when `CORE_UI_FONT_ATLAS` is unset. Attribution in
  `THIRD_PARTY.md`; bake recipe in `docs/UI_THEME.md`.
- Added `core_ui_theme_tests` (theme resolution, material tokens, component
  geometry, formatting, degenerate-input safety) and a scripted sample-HUD
  acceptance test that compiles and paints `main.coregui`.
- Documented the system in `docs/UI_THEME.md`.

## Thunder 1.0 Development — Engine/game boundary cleanup and stale-doc removal — 2026-08-28

- Completed the engine/game split: game composition code (`DesktopApp`,
  `GameAppController`, `StrategyHudSystem`, `GrandStrategyGui`,
  `GameProjectConfig`) lives in `src/game` as the `core_game_ui` target; the
  empty `src/thunder/platform/` directory was removed and configure-time guards
  keep `src/core` free of game-layer includes.
- Removed stale milestone documents: `PERFORMANCE_BUDGET_0_4/0_7/0_8/0_9.md`,
  `RELEASE_STATUS_1_0_RC_GPU.md`, `VIC3_JOMINI_GAP_MATRIX.md` and the frozen
  `VALIDATION_1_0.txt` snapshot; `docs/FINAL_ENGINE_AUDIT.md` remains the
  authoritative verification record.
- Kept `content/base` and `demo/` as the bundled sample game, loaded through
  the VFS/script pipeline and outside every engine target.
- Regenerated `SOURCE_FILES_1_0.txt` / `SOURCE_SHA256_1_0.txt` via
  `tools/update_source_manifest.py` (354 first-party files).

## Thunder 1.0 Development — Script-first bootstrap and DPI-safe UI text — 2026-08-28

- Fixed the Vulkan UI coordinate contract: logical UI, scissors and pointer
  input now share a top-left origin, removing the previous vertical mirror.
- Added logical-window to swapchain-pixel conversion for high-DPI scissors and
  viewport constants.
- Wired the desktop renderer to `.corefont` MSDF glyph metrics, real bearings
  and advances, derivative-based edge coverage and UTF-8 fallback glyphs. The
  fixed-cell/5x7 renderer remains diagnostics-only.
- Extended the font cooker to emit the runtime `COREIMG1` atlas alongside
  `.corefont` metrics, with optional KTX2 output.
- Added strict script definitions and transactional binding for goods,
  building types, production methods and need profiles.
- Added `GameContentRuntime`, a one-shot owner for symbols and compiled scripts
  that validates all content domains before installing a new game and binds
  the effective content hash into saves. The desktop shell uses it whenever
  `CORE_CONTENT_ROOT` is supplied.
- Added the script-first engine/content boundary document and new regression
  coverage for economy binding and full content bootstrap.
- Standardized topic-neutral engine naming: `StrategyHudSystem`,
  `MapDecorationRenderer`, `apply_warm_archival_grading` and
  `decorative_parchment_texture` replace setting-bound identifiers, with no
  legacy aliases. Built-in examples, tests and base-content headers now follow
  the same rule.
- Fixed the font cooker's default charset invocation for current
  `msdf-atlas-gen`, added a UTF-8/CJK glyph regression, documented that serif,
  sans-serif and CJK fonts share the same MSDF path, and made the pixel-font
  diagnostics fallback emit an explicit non-shipping warning.

## Thunder 1.0 Development — Deterministic Runtime Hardening & Global Script Persistence — 2026-08-28

- Added the tagged `GLB1` save extension for world-level ThunderScript state:
  stable-key variables/parameters, event targets, typed collections, scope
  bindings and deterministic random-draw counters now survive save/load.
- Extended persistent-context validation to support an untyped global root while
  retaining world reference checks and atomic decode/commit behavior.
- Hardened stale entity handles across scripted traversal, bytecode evaluation,
  economy settlement, research, migration, warfare and construction queues by
  filtering destroyed SoA slots consistently.
- Wired data-driven weekly research diffusion to the persisted GameClock (with
  a legacy fallback only when no finalized research catalogue is present) and
  resolved due migration flows during the normal world-aware weekly tick.
- Added construction-queue invariants and overflow guards: project IDs remain
  monotonic, malformed targets are rejected before save/tick, completed
  expansions cannot wrap a building level, and weekly progress now participates
  in deterministic checksums with legacy CQ01 checksum migration retained.
- Added malformed UI geometry/tooltip overflow guards and data-backed desktop HUD
  values with explicit empty states instead of demo placeholders.
- Verified MSVC `/WX`, MinGW/GCC `-Werror`, Debug and Release headless builds;
  all 26 regression suites pass in each configuration.

## Thunder 1.0 Development — Realistic Governance, State Resistance & Natural Integration, Autonomous Trade & Treasury Funding — 2026-08-27

- **Treasury-Funded Institutions & Fiscal Budgeting** (`src/thunder/grand_strategy/GrandStrategyStore.cpp` / `hpp`):
  - Completely abolished abstract "administrative mana/points"; government institutions (1~5 levels across education, police, healthcare, bureaucracy) are directly funded with real money drawn weekly from the national Treasury (`GrandStrategyStore::run_institutions_weekly`).
  - Deficit spending or salary arrears reduce national prestige and degrade institutional effectiveness realistically.
- **Autonomous Market Trade Routes & Adaptive Merchant Scaling** (`src/thunder/economy/EconomySystem.cpp`):
  - Market routes are autonomously scaled by private commercial capital based on spatial price arbitrage spreads after freight logistics and bilateral import/export tariffs (+5%/week for profitable lanes; -10%/week shrinkage and eventual deregistration for unprofitable or unsupplied lanes).
- **State Resistance & Organic Thunder Integration** (`src/thunder/world/GeographyStore.cpp` / `hpp`):
  - Abolished artificial point-based state integration. Added continuous `state_resistance_ppm_` ($0 \sim 1,000,000$ ppm).
  - Newly conquered or annexed states start with high resistance, inducing tax evasion and reducing effective tax collection by up to 75%.
  - Resistance naturally decays toward zero ($-2,000$ ppm/week, $\approx 10.4\%$ annualized) when local population Standard of Living (SoL $\ge 9.0$) is high, cultural acceptance is maintained, and institutions are funded, organically transforming the territory into an integrated core without arbitrary point expenditure.
- **Continuous Social & Geopolitical Progression**:
  - POP qualification accumulation (`qualification_permyriad`) smoothly builds up weekly driven by local literacy rates and standard of living.
  - **Comprehensive Realistic Technology & Research Mechanics** (`src/thunder/research/ResearchSystem.cpp` / `hpp`):
    - **Multi-Pillar Innovation Generation**: Academic & popular innovation scales dynamically with literate population, amplified by up to $+75\%$ through government **Education Institutions (Tiers 1~5)**.
    - **Scientific Breakthroughs ("Eureka" Moments)**: Active research has a deterministic weekly chance ($2.5\%$) to trigger inspiration breakthroughs, delivering $+5\%$ immediate progress leaps.
    - **World-First Discovery Honors**: The pioneer country to first invent a technology in the world is awarded $+5.0$ international **Prestige**, reflecting global acclaim and technological leadership.
    - **Realistic Absorptive Capacity**: Foreign tech diffusion rate is directly modulated by the recipient nation's literacy and educational capacity, preventing backward, illiterate societies from instant assimilation while rewarding educational reforms.
    - **Strict Prerequisite & Era Gating**: Technologies cannot diffuse without all prerequisite dependencies unlocked and era thresholds achieved.
    - **Deterministic Probabilistic Rolls**: Diffusion requires peaceful diplomatic contact, trade agreements, and alliances, with deterministic `Fnv1a64` rolls.
  - Peacetime diplomatic relation drift, tension cooldown, and unengaged army readiness/organization recovery advance continuously.
- **Tagged `RES1` Save Extension** (`src/thunder/save/SaveGame.cpp`):
  - Emits and decodes state resistance state within a backward-compatible tagged section, preserving 100% downward compatibility across v1, v3, and v4 save schemas.
- **All 26/26 Unit & System Test Suites Passing 100%** across both `release-headless` and `dev-headless`.

## Thunder 1.0 Development — Gradual Production Method Transition, Construction Queue & 3D Monument Pipeline — 2026-08-27

- Implemented **Gradual Production Method (PM) Transition & Retooling** via a unified national **Construction Queue** (`ConstructionStore` in `src/thunder/economy/ConstructionStore.cpp` / `hpp`):
  - Changing a building's production method (PM) is no longer an instant switch; it enqueues a retooling project in the national construction queue.
  - Construction capacity points (scaled by base output, GDP, and private investment) and funds from the national `InvestmentPool` or state Treasury are invested each weekly tick.
  - Retooling progress (`pm_transition_progress_ppm`) smoothly advances from $0 \to 1,000,000$ ppm, preventing abrupt economic shocks, supply cliffs, and unemployment spikes.
  - Supports building expansion (`ExpandBuilding`), PM upgrade (`UpgradeProductionMethod`), and historical monument construction (`ConstructMonument`).
  - Supports dynamic queue management: priority reordering (`move_up`, `move_down`), pause/resume (`set_paused`), and project cancellation.
- Implemented **Procedural 3D Monument Generation Pipeline** (`tools/assets/generate_statue_of_liberty.py`):
  - Procedurally generates 4 Level-of-Detail (LOD0 to LOD3) models for the **Statue of Liberty (自由女神像)** featuring:
    - 11-pointed star fort base (Fort Wood) with stone ramparts.
    - Neoclassical rusticated granite pedestal with Greek key entablature, columned loggias, and observation balconies.
    - Classical draped Roman figure of Libertas with detailed cloth folds.
    - Radiant 7-spiked diadem crown (symbolizing the sun, 7 seas, and 7 continents).
    - Raised right arm gripping the Torch of Enlightenment with multi-faceted gilded flame geometry.
    - Left arm cradling the molded Tabula Ansata tablet inscribed with July 4, 1776.
  - Outputs binary glTF 2.0 (`.glb`), Wavefront (`.obj`), PBR material definition (`.coremat.json`), and LOD manifest (`.corelod.json`).
- Integrated **Construction Queue UI & Retained Widget Components**:
  - Registered `construction_queue` and `construction_project` contexts and properties in `ScriptedGuiSchema`.
  - Added `UiDrawList::construction_queue_row` in `StrategyUi.hpp` / `StrategyUi.cpp` rendering ornamental queue cards with wood/leather backgrounds, gilded progress bars, badges, ETA calculations, and pause status indicators.
- Added **`CQ01` Atomic Save Game Extension** (`src/thunder/save/SaveGame.cpp`):
  - Emits and decodes tagged construction queue sections with full backward compatibility and legacy checksum fallbacks.
- Added comprehensive unit tests in `tests/economy_tests.cpp` and `tests/scripted_gui_tests.cpp`, all verified across both `release-headless` and `dev-headless` with **26/26 CTest suites passing 100%**.

- Added SoA commercial-bank and persistent loan stores with stable keys, balanced assets/liabilities, reserve and capital requirements, interest, amortization, arrears, NPL classification, charge-offs, and insolvency.
- Added the optional `FIN1` save extension with atomic validation, deterministic checksums, and pre-FIN1 migration.
- Routed sovereign issuance through funded bank capacity or accumulated saver pools and returned debt service to recorded lenders.
- Added authored-route capacity, tariffs, international logistics budgets, global-numeraire BOP/reserve accounting, and cross-currency price ordering.
- Split monetary diagnostics into private-credit creation, central issuance, currency revaluation, and unexplained residual.
- Added ThunderScript finance triggers/effects and logarithmic currency/bank lookup accelerators outside the high-cardinality hot path.

## Thunder 1.0 Development — Monetary Standards, Currency Zones & Monetary Sovereignty — 2026-08-27
- Refactored `CurrencyStore` (`src/thunder/economy/CurrencyStore.cpp`) to implement authentic **Monetary Standards**: `GoldStandard` (anchored to fine gold milligrams and bounded by gold transport points), `SilverStandard`, `Bimetallism` (governed by Gresham's Law ratio dynamics), and `FiatFloating` (market credit-driven).
- Implemented **Currency Zones (Monetary Unions)** and **Monetary Sovereignty**: The member country with highest prestige and power in a currency union dynamically holds monetary sovereignty, dictating metallic parity policy and harvesting seigniorage revenues into its national treasury.
- Added country prestige (`prestige`, `power_score`) to `CountryStore` and integrated it into authoritative state checksums.
- Enhanced `EconomySystem::trade` with multi-currency invoice clearing and Hume's Price-Specie Flow mechanism across international markets.
- Guaranteed rigorous domestic vs foreign exchange settlement separation: POP wages, consumption, building retention, and taxation strictly clear in domestic local currency, while cross-zone trade clears in foreign exchange.
- Standardized country GDP calculation to the global gold parity numeraire (`default_currency_key`), eliminating currency devaluation/inflation distortions in international Great Power rankings while retaining nominal local currency GDP for domestic fiscal accounting.
- Implemented Hume's classical **Gold & Silver Transport Points Specie Arbitrage**: When persistent trade deficits drive open-market FX costs above mint parity plus gold transport/insurance, bullion arbitrageurs automatically redeem paper for physical specie and ship gold/silver to clear the trade gap, directly debiting national central reserves. If reserves are drained to zero, convertibility is suspended and the currency is forced into fiat depreciation.
- Added **Sovereign Debt, Bond Issuance, Credit Ratings & Bond Markets** to `CountryStore`:
  - Countries can issue sovereign bonds to raise funds into their treasury up to credit-rating-scaled debt ceilings (`AAA` to `D`).
  - Implemented dynamic bond yields and credit rating evaluation based on Debt-to-GDP ratio, treasury coverage, and default history.
  - Weekly debt service interest is automatically deducted during economy settlement; insolvency triggers Sovereign Default (`CreditRating::D`), freezing bond markets and penalizing national prestige.
- Added rolling 52-week exchange rate history buffers in `CurrencyStore` enabling native in-game timeseries charts (`UiChartKind::Line`, `UiChartKind::Candlestick`) in `ScriptedGuiRuntime`.
- Updated `FX01` atomic save serialization to persist monetary standards, fine metal parities, zone leadership, country prestige, convertibility status, national debt, credit ratings, and bond yields.
- Added comprehensive unit and regression tests in `tests/economy_tests.cpp` covering metallic standard parities, Gresham's law ratio divergence, zone leadership election/transfer, specie drain/convertibility suspension, and sovereign bond issuance/default.

## Thunder 1.0 Development — Retained UI Runtime & Pin Updates — 2026-08-27
- Implemented `src/thunder/ui/ScriptedGuiRuntime.cpp` and wired it into `core_runtime`, completing the retained UI state machine.
- Added comprehensive unit tests for `ScriptedGuiRuntime` in `tests/scripted_gui_tests.cpp` covering data provider bindings, virtualized list/grid viewports, chart downsampling, and dirty diff generation.
- Added `ui_stable_node_key` helper to `src/thunder/ui/ScriptedGui.hpp` for deterministic node-hierarchy lookup.
- Clarified Vulkan-Headers 1.4.360 version pin and build-time dependency discovery in `THIRD_PARTY.md`.
- Updated engine audit and documentation reflecting the compiled and tested retained UI subsystem.

## Thunder 1.0 Development — Public Repository Packaging — 2026-08-27
- Declared the first-party source under the MIT License and added the canonical root `LICENSE` file.
- Added contribution and security policies, Git attributes, a documentation index, and Windows headless CI.
- Reworked the public README to match the verified Development Preview maturity, 26-test suite, build presets, and current directory layout.
- Corrected third-party notices for the vendored Khronos Vulkan-Headers revision and build-time SDL3/Zstandard/xxHash dependencies.
- Removed local machine paths and proprietary-product wording from public-facing example content and research metadata.
- Refreshed first-party source and SHA-256 manifests; generated packages and build trees remain excluded.

## Thunder 1.0 Development — Economy Closed-Loop & Render Fixes — 2026-08-27
- Added per-(market, good) inventory to MarketStore (serialized in save schema v4; v1/v3 remain read-only migrations with a byte-stable legacy checksum).
- Reworked price formation: demand clears against supply plus carried stock, unsold surplus becomes inventory (capped at 4 weeks of liquidity), and price pressure follows the net stock position with a symmetric clamp and dead-band correction.
- Added market-wide fulfillment ratios: production throughput is now rationed by worst input availability (derived stateless from serialized flows, so save/restore stays checksum-identical), and POP consumption payments plus standard-of-living targets scale with basket fulfillment.
- GDP accounting now uses wages + operating surplus instead of goods profit alone.
- Hoisted base prices out of the per-(market, good) price loop (removes the throwing `good()` accessor from the hot path).
- Render/UI: MSDF text shader now honors the atlas `px_range` push constant; political overlay alpha is no longer hardcoded to 45% (respects the pushed color); swapchain prefers MAILBOX over FIFO and accepts R8G8B8A8_SRGB surfaces; BindlessMaterialSystem recycles descriptor slots on unregister instead of leaking/aliasing them.

## Thunder 1.0 Development — Logic Foundation — 2026-08-26
- Kept the engine on the Thunder 1.0 release line as the initial foundation.
- Added ThunderScript 1.0 scope traversal, saved scopes, ROOT/FROM/PREV/THIS and deterministic iterators.

- Added typed symbolic primitive arguments for stable country/content keys.
- Added data-driven Event/Decision/Journal/AI Action loading with event options and gameplay logging.
- Added persistent deterministic AI strategic plans (`ai_plan`) with priority, commitment, completion and action allow-lists.
- Added operational politics/diplomacy/warfare foundation systems and ThunderScript bridges.
- Extended atomic runtime save/load to Gameplay, Journal/Event state, AI cooldowns and active plans using stable keys.
- Current writes use save schema v4; Thunder 1.0 schema-v1 and prior runtime schema-v3 are read-only migration formats.
- Added dedicated strategy-system and runtime-save regression tests; current Release CTest result is 9/9 PASS.
- This milestone does not claim complete production grand-strategy feature coverage; see `docs/LOGIC_LAYER_1_0.md`.


## 1.0 RC-GPU — 2026-08-26

- Re-established a persistent 1.0 source tree and promoted strict Release warnings-as-errors as a release gate.
- Added GeographyStore and generic GrandStrategyStore records for technology/law/institution/company/trade/ownership/treaty/military/migration/politics/power-bloc/diplomatic-play/front/battle/colony/ship-design/investment primitives.
- Added Production Methods and expanded POP culture/religion/profession/literacy/qualification/wealth/political-strength/interest-group columns.
- Added complete current-world Save/Load, ReplayJournal and unified CoreEngine lifecycle with deterministic restore/continuation tests.
- Fixed stale MarketEntityIndex and ProvinceEntityIndex membership caches through explicit membership revisions.
- Hardened deterministic fixed-point arithmetic against extreme 64-bit overflow while retaining a normal-value fast path.
- Replaced transport bounding-box chunk enumeration with grid traversal proportional to traversed chunks.
- Reduced Living Map steady-state allocations/scans and added authored multi-chunk spatial placement.
- Added SpatialPlacementDatabase, settlement anchors and WorldBootstrap loading from `.coreworld`.
- Added production GIS compiler for province pages, coast SDF, terrain, masks, placement, anchors and adjacency.
- Hardened WorldPack and AssetPack malformed-input rejection.
- Added `.coreasset` asset packs, residency budgeting and `.corearch` architecture-kit binary cooker/reader.
- Added Strategy UI draw/batch/scissor/hit-region and virtualized-list foundation.
- Improved RenderGraph multi-reader/write/layout/queue hazard tracking and page streaming lifetime management.
- Added Vulkan 1.3 SDL3 desktop validation backend, validation/debug messenger, GPU capability report and Windows one-command validation gate.
- Added reference GLSL contracts for terrain, ocean, political overlay, Living Map and Strategy UI/MSDF text.
- Added Linux GitHub Actions CPU CI.

**Status:** RC-GPU. CPU/data/content tests are release candidates; Final requires target Windows physical-GPU validation and final visual pipeline integration/QA.

# Thunder Changelog

## 0.9 Living Map Data Foundation — 2026-08-25

- Added cold province-location SoA columns to POP and Building stores; economy hot loops remain unchanged.
- Added `ProvinceEntityIndex` CSR for province-owned POP/building aggregation.
- Added `LivingMapSystem` with per-province population/employment/SoL/building aggregation and quantized visual signatures.
- Added deterministic procedural visual generation with fixed 16-byte near-instance payloads and fixed 16-byte medium/far province clusters.
- Added 64 km Living Map chunks, per-chunk render versions and incremental rebuild/upload accounting.
- Added distance/budget driven `LivingMapStreamingPlanner`; near zoom uses full instances while medium/far zoom uses province clusters.
- Added `TransportNetwork`: province links are clipped into independently streamable 16-byte road/rail/canal segments; level changes update only affected segment/chunk versions.
- Added Living Map RenderGraph contract: transfer upload -> compute cull -> indirect transport/cluster/instance draws.
- Added deterministic cross-worker Living Map tests, incremental dirty tests, transport tests and `core_living_demo`.
- Added 8k-province / 300k-POP and 1M-POP Living Map benchmarks. Current-container steady-state baselines are ~0.6-0.8 ms and ~1.1-1.2 ms respectively; these are regression baselines, not hardware guarantees.
- Explicitly deferred final polygon-correct spawning, historical route polylines, architecture kits and live Vulkan rendering to the next visual/compiler phase.

## 0.8 Pops / Buildings / Markets Vertical Slice — 2026-08-25

- Added `EconomyDefinitions` with compact typed goods, building recipes and POP need profiles stored as flat flow arrays.
- Added `PopStore` SoA (32 bytes/cohort current hot layout), `BuildingStore`, flat market-good arrays and `MarketEntityIndex`.
- Added 64-bit deterministic fixed-point economy quantities/prices/money for the high-cardinality simulation path.
- Added weekly employment, production, industrial demand, POP consumption, market price convergence and settlement phases.
- Added bounded wage feedback, building cash/profit, POP income/standard-of-living, taxation and stable country GDP/population aggregation.
- Added market-owned parallel execution: market rows and entities are mutated without atomics; country results fold later in market ID order.
- Replaced per-POP checked getter/setter calls in hot kernels with validated SoA column spans after profiling identified access overhead.
- Replaced per-POP employer building scans with direct `BuildingId` remaining-capacity indexing.
- Added `(market, need_profile)` population aggregation and basket-cost caching so identical needs are evaluated once per profile/market instead of once per POP.
- Added deterministic serial-vs-parallel economy checksum tests, scarcity/price tests, storage-budget tests and `core_economy_demo`.
- Added 300k-POP and 1M-POP benchmarks. Current-container final baselines are ~3.5 ms average / ~3.1 ms median for 300k POPs, and ~18.5 ms average / ~15.7 ms median for 1M POPs with five execution slots. Measurements are regression baselines, not hardware guarantees.
- Revalidated the new economy path with Release `-Werror`, ASan/UBSan, ThreadSanitizer and JobSystem lifecycle stress.

## 0.7 Simulation Job System — 2026-08-25

- Added persistent `JobSystem` worker pool with caller-thread participation and a default cap of 15 background workers pending topology/NUMA probing.
- Added stable fixed-grain partitions; chunk identity is independent of worker count and dynamic claiming order.
- Added `DeterministicReduction` with chunk-index fold order and bit-stability tests across different worker counts.
- Added keyed deterministic RNG samples based on stable seed/stream/counter rather than worker or execution order.
- Added cache-line-separated `DeterministicCommandStage`; parallel chunks stage commands independently and flush in chunk order before global sequence assignment.
- Added worker-local linear scratch with per-job mark/rewind and nested-dispatch preservation.
- Added inline fast path for dispatches below eight jobs after profiling showed worker wake-up overhead dominated tiny work.
- Added conservative Tick DAG parallel execution: `Serial` remains default; only explicit `ParallelSafe` waves are eligible.
- Added `TickExecutionProfile`, `JobDispatchStats` and Graphviz DOT export for compiled tick graphs.
- Added nested dispatch fallback to inline execution and exception propagation from worker jobs.
- Stress-tested 10,000 repeated create/dispatch/destroy cycles.
- ThreadSanitizer found and drove fixes for a worker-shutdown lifetime race and a rare lost-wakeup bug in the initial condition-variable completion path; final completion uses C++20 `atomic::wait/notify`.
- Current-machine release baseline for a stable-chunk 5m-row synthetic dense kernel: ~4.2 ms serial vs ~1.6 ms parallel with five available worker slots; use only as a regression baseline.

## 0.6 ThunderScript + mod runtime — 2026-08-25

- Added stable startup `SymbolTable`; compiled simulation code uses compact symbol/primitive IDs instead of hot-path text lookup.
- Reworked `ScriptRegistry` so triggers/effects receive 16-bit primitive IDs and native function-pointer dispatch.
- Added ThunderScript lexer/parser with nested blocks, comments, quoted strings, numeric/date tokens and source diagnostics.
- Added typed `ScriptCompiler` and `ScriptProgramDatabase`.
- Added flat-AND trigger fast path after profiling the generic boolean VM regression.
- Added complex `all / any / not` RPN condition IR with fixed local evaluation stack and no runtime heap allocation.
- Added compiled scripted values for direct country SoA sources and compiled history effects.
- Added immutable `DefinitionDatabase` country content plus deterministic runtime instantiation.
- Added priority-based `VirtualFileSystem`: same logical path overlays and different-path duplicate definitions obey deterministic mod priority/load order.
- Added deterministic effective-content hash that includes semantic load priority, logical path and effective source bytes.
- Added symbol-backed localization tables with fallback lookup.
- Added `ContentLoader` and standalone `core_content_check` validator/compiler CLI.
- Added base content smoke fixtures for countries, scripts, history and English/Chinese localization.
- Added scope-error, boolean-condition, history, VFS override, content-hash and localization tests.
- Measured ~17-21 ms parse+compile for 10k simple scripts and restored ~51-53 ms for 5m two-trigger evaluations on the current machine after the fast-path optimization.

## 0.5 world compiler + resident map modes — 2026-08-24

- Added `.coreworld` seekable binary pack with 64-byte header, fixed sorted index and per-chunk keys.
- Added Zstd-per-chunk compression with raw fallback and 64-byte chunk alignment.
- Added self-described XXH3-64/FNV chunk integrity checks and O(1)-average duplicate-key detection.
- Added deterministic pack build hash, stable across manifest row order after chunk-key sorting.
- Added POSIX `pread()` random-access file path; runtime no longer reopens/seeks a stream per page.
- Added per-streaming-worker reusable compressed/decoded buffers and reusable Zstd decode context.
- Added `core_world_compiler` and `core_world_inspect` CLI tools.
- Added `PoliticalMapPageBundleView` for atomic 32 KiB province + 32 KiB coast payloads.
- Added compact `MapModeStore`: four scalar + three categorical modes stay resident in 112 KB at 8k provinces.
- Added optional GPU 16-bit storage capability; compatibility path may expand to 32-bit at upload.
- Added deterministic Britain technical world fixture and preview; coast is real GSHHS-derived data, provinces are explicitly synthetic/non-historical.
- Rejected 4 KiB-per-chunk alignment after measuring compressed-page hole overhead; default is now 64 bytes.
- Replaced scalar FNV hot-path chunk validation with XXH3, reducing the 10k cached page-read benchmark from ~864 ms to ~149 ms on this machine.
- Added world-pack/map-mode/bundle tests and clean Release + ASan/UBSan validation.

## 0.4 strategic map kernel — 2026-08-24

- Added 128x128 uint16 province-ID pages (32 KiB/page), with zero reserved for water.
- Added 128x128 int16 signed coast-distance pages (32 KiB/page, 0.5 m quantization).
- Added atomic 64 KiB province-ID + coast-distance streaming bundles.
- Added `PoliticalMapState`: province-owner/flags, RGBA8 country palette and per-province map-mode scalar buffers.
- Added sparse dirty spans plus dense-update fallback to full sequential buffer upload.
- Added political map RenderGraph upload and overlay passes.
- Added CPU `ProvincePickingCache` to avoid synchronous GPU readback for hover/selection.
- Added immutable CSR `ProvinceAdjacencyGraph` with land/river/strait/impassable flags and fixed-point costs.
- Added SDK-independent headless Vulkan loader probe; current container reaches Vulkan 1.4.309 loader and `vkCreateInstance`.
- Added political-map, picking, streaming, adjacency and Vulkan-probe tests/benchmarks.

## 0.3 terrain foundation — 2026-08-24

- Added double-precision Mercator projection and camera-relative GPU coordinate policy.
- Added logarithmic strategic camera with zoom-to-cursor approximation.
- Added constant-size 8-level terrain clipmap (400 patch instances by default).
- Added 65/33/17 grid LOD selection, reducing default terrain geometry from ~3.28M to ~0.844M triangles before culling.
- Added 65x65 uint16 absolute height pages: 8,450 bytes/page at 0.5 m precision.
- Added terrain page residency cache, streaming planner and adaptive upload byte budget.
- Added Terrain RenderGraph transfer -> compute cull -> indirect draw declaration.
- Added normalized GPU capability scoring/tiering for future Vulkan physical-device selection.

## 0.2 performance/render foundation — 2026-08-24

- Added frame-local linear allocator and 3-frame retirement ring.
- Added RenderGraph v0 with RAW/WAR/WAW dependency compilation, parallel batches and transition plan.
- Added non-blocking triple-buffer simulation/render snapshot exchange.
- Compacted country render records to 20 bytes and removed per-frame string copies.
- Removed world checksum from render hot path.
- Replaced deque command queue with retained-capacity contiguous vector queue.
- Replaced recursive modifier dirty propagation with reusable iterative traversal and linear batch recompute.
- Tick scheduler now precompiles dependency-free waves for later deterministic multithreading.
- Added microbenchmark target and explicit performance architecture rules.

## 0.1 foundation — 2026-08-24

Initial Thunder grand-strategy runtime foundation: deterministic time/commands, SoA world storage, tick
DAG, modifier graph, typed script primitive registry, render snapshots, tests and optional SDL3/Vulkan
desktop shell target.
