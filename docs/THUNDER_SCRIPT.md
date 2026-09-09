# ThunderScript in Thunder 1.0

ThunderScript is Thunder's typed, moddable content language. This document describes the
current Thunder 1.0 development implementation; it does not declare a new product
version, and it does not claim complete grand-strategy content-runtime coverage.

Source text is parsed and linked during content loading. Simulation code receives
compact primitive IDs, stable keys and compiled instruction trees, so primitive
names and scope properties are not resolved from strings in hot loops.

## Pipeline

```text
.thunder source
  -> bounded lexer/parser
  -> symbol interning and stable-key collision checks
  -> typed script/value compilation
  -> whole-database link validation
  -> compact fast path or scoped VM
```

`ContentLoader` parses all effective VFS files before calling
`ScriptProgramDatabase::validate_links`. Cross-file calls can therefore resolve
independently of file boundaries. A content transaction with parser, compiler or
linker diagnostics must not become live content.

## Programs and execution paths

A script declares a root scope and may contain repeated trigger and effect blocks:

```text
script fiscal_relief {
    scope = country
    trigger = { treasury_above = 10 }
    effect = { add_treasury = 2 }
}
```

All trigger blocks in one script are ANDed. An explicit empty trigger is true.
Thunder chooses one coherent backend for the complete program:

- flat numeric AND conditions use a contiguous `fast_all` array;
- same-scope `all` / `any` / `not` expressions use compact RPN instructions;
- scope navigation, variables, calls and iterators use the scoped tree VM.

If any block needs the scoped backend, every block of that kind is compiled there;
mixing a classic block with an advanced block does not silently discard either.
Simple numeric scripts therefore keep their short-circuiting fast path while
advanced content pays the interpreter cost.

Implemented root scope types are Country, State, Province, Pop and Market. The
primitive registry owns the accepted scope and argument kinds for each native
trigger/effect. A mismatch is rejected while compiling and checked again at the
runtime boundary.

## Typed parameters and linking

Scripts and scripted values can declare named signatures:

```text
script grant_relief {
    scope = country
    parameters = {
        amount = number
        recipient = country
        bonus = { type = number required = no default = 2 }
    }
    effect = {
        add_treasury = arg:amount
        add_treasury = arg:bonus
    }
}

script caller {
    scope = country
    parameters = { recipient = country }
    effect = {
        scripted_effect = {
            name = grant_relief
            amount = 5
            recipient = arg:recipient
        }
    }
}
```

Current parameter kinds are Number, Symbol/Key, Boolean, generic Scope, and the
Country/State/Province/Pop/Market scope subtypes. Parameters are required by default;
an optional non-scope parameter may provide a literal default. Named bindings are stored in
stable-key order and reject non-finite numbers. Numeric negative zero is canonicalized
so it cannot create a checksum-only difference.

The whole-database linker reports:

- unknown scripted trigger/effect/value targets;
- caller/callee root-scope mismatch;
- missing, extra or duplicate named arguments;
- statically provable argument-kind and scope-subtype mismatch;
- references to undeclared typed parameters;
- direct or indirect script and scripted-value call cycles.

Runtime entry points bind the same schema again. This protects calls originating
from C++ or restored state, not only calls produced by the compiler. Callee frames
are lexical and are removed after the call; callee-local parameters and variables
do not leak into their caller.

## Arguments and scripted values

Compiled arguments can come from a literal, variable, parameter, event target,
THIS/ROOT/FROM/PREV, or another scripted value. `ScriptArgument` currently carries
Number, SymbolHash, Boolean or Scope.

Scripted values support typed parameters and the following direct sources:

- Country: population, GDP, treasury and tax rate;
- Pop: size, employment, standard of living, literacy, qualification, wealth and
  political strength;
- Market: aggregate supply and demand;
- State and Province: population;
- a runtime argument or another scripted value.

The current value expression is `source * multiply + add`; the general runtime
arithmetic bytecode (value-source/variable operand references) is not yet wired
into the grammar. Results and external numeric arguments must be finite.

### Parse-time arithmetic folding (2026-09-06)

Every numeric field in ThunderScript accepts a **constant arithmetic
expression** that the parser folds into a plain number before content binding:

```
good steel { base_price_milli = (900 - 100) / 4 * 10 }   # -> 2000
building_type mill { workers_per_level = 500 * 2 + 1000 } # -> 2000
```

- Operators: `+ - * /` with standard precedence, parentheses, unary minus.
- Binary `+`/`-` require surrounding whitespace; a sign attached to a number or
  word keeps the legacy literal meaning (`-5`, `+3.5`, `-state-id`).
- Division by zero and non-constant operands are parse diagnostics.
- Folding is deterministic and applies engine-wide (any `ScriptValueKind::Number`
  consumer), with zero runtime cost.

### Conditional effects (2026-09-06)

`script` effect blocks support `if` with a `limit` condition group and an
optional `else`:

```thunder
script prosperity {
    scope = country
    effect = {
        if = {
            limit = { treasury_above = 5000 }
            every_pop = { set_pop_wealth = 90 }
        }
        else = {
            every_pop = { set_pop_wealth = 10 }
        }
    }
}
```

- `limit` compiles into the scoped-condition algebra (`All` over its children;
  triggers, scope selectors, iterators and script calls are all usable inside).
- Exactly one branch runs, in the current scope — `if` never changes scope.
- `if` routes through the scoped-effect compiler (like `every_*` blocks), nests
  arbitrarily, and may appear inside scope/iterator blocks. An `if` without
  `limit` is a content error.

### Iterator candidate gating: `limit = { <conditions> }` (2026-09-06)

Iterators accept a Clausewitz-style condition block in addition to the numeric
count cap. Failing candidates are invisible to the iteration whatever the mode:

```thunder
script war_industry {
    scope = country
    effect = {
        every_country = {
            limit = { treasury_above = 5000 }
            add_treasury = 100
        }
        ordered_country = {
            limit = { treasury_above = 5000 }   # candidate gate (block)
            limit = 3                            # count cap (number)
            order_by = order_wealth
            add_treasury = 500
        }
    }
}
```

- `limit = { ... }` (block) is the candidate filter; `limit = 3` (number) stays
  the ordered count cap. Both may appear in one `ordered_*` iterator.
- The filter evaluates in each candidate's own scope and is charged to the work
  budget; `any_*`/`random_*` draw only from passing candidates.
- On the trigger side the same gating applies: failing candidates are excluded
  from `any_*`/`every_*`/`ordered_*` semantics (an `every_*` whose filter empties
  the candidate set is vacuously true, matching Clausewitz).
- Same-type iteration is world-global (Clausewitz semantics): inside a country
  scope, `every_country` enumerates ALL countries, not the current one. Owned
  variants are expressed by navigation plus a `limit` gate.

### Weighted random effects: `random_list` (2026-09-06)

`random_list` picks exactly one weighted branch:

```thunder
random_list = {
    10 = { add_treasury = 10 }
    90 = { add_treasury = 1 }
}
```

- Each child key is the numeric branch weight; the value is a block of effects
  compiled like any scoped effect body (if/iterators/scope changes nest freely).
- The draw uses the keyed deterministic RNG (callsite salt + per-callsite draw
  counter), so replays are exact for equal world state; the draw counter lives
  in the context and is checksummed for persistent contexts like iterator draws.
- Weights are static numeric literals today; scripted-value weights remain a
  documented gap below. A non-numeric branch key is a compile diagnostic.

### Value expressions and infix comparisons (2026-09-06)

`scripted_value` accepts an `expression` field — the general arithmetic tree the
bytecode VM was built for:

```thunder
scripted_value levy {
    scope = country
    expression = (population * 0.5 + gdp * 2) / 1000
}
```

- Operands: builtin value sources (`population`, `gdp`, `treasury`, `pop_size`,
  `standard_of_living`, `building_level`, … — the same set as `source =`),
  `value:<name>` scripted values, `var:<name>` variables and numeric constants.
- **The expression value must start with `(`** — the parser reads a bare word
  value as a single symbol, so `expression = (population * 2)` is an expression
  while `expression = population` would just be that one operand.
- Operators: `+ - * /`, parentheses, unary minus, standard precedence. Binary
  `+`/`-`/`/` require surrounding whitespace (a sign attached to a number or
  word keeps the legacy literal meaning; `/` is a word character). Division by
  zero in a constant expression is a parse diagnostic; at runtime it yields 0.
- `value:` references are link-validated (existence, scope match, cycle
  detection) like every other scripted-value call.
- Constant-only expressions fold at parse time with zero runtime cost, exactly
  like numeric fields.

Trigger blocks accept infix comparisons — both sides are value expressions
evaluated in the current scope (`SourceCompare` condition):

```thunder
script levy_ready {
    scope = country
    trigger = {
        population > 1000
        value:levy - 50 >= 40
        pop_size <= 100
    }
}
```

- Operators: `>`, `<`, `>=`, `<=`, `!=`. Equality stays with the named
  `*_equals` primitives and `variable_equals`; `==` is intentionally not a
  line form (it would collide with `key = value`).
- Comparison lines route through the scoped-condition backend and nest inside
  `all/any/not`, scope selectors, `limit = { ... }` gates and `if` conditions.
- Tolerance semantics match the bytecode comparison ops (Eq/Ne at 1e-6,
  AtLeast/AtMost epsilon-inclusive).

### Loops and `else_if` chains (2026-09-06)

Effect blocks support bounded looping and full conditional chains:

```thunder
script compound_interest {
    scope = country
    effect = {
        set_variable = { name = total value = 0 }
        while = {
            limit = { variable_below = { name = total value = 500 } }
            change_variable = { name = total value = 10 }
        }
        if = {
            limit = { total > 400 }
            add_treasury = 100
        }
        else_if = {
            limit = { total > 200 }
            add_treasury = 50
        }
        else = {
            add_treasury = 10
        }
    }
}
```

- `while` re-evaluates its `limit` in the current scope before every pass; each
  pass is charged to the work budget, so a limit that never fails terminates
  deterministically with the budget error (never a hang, never nondeterministic).
- `else_if` chains attach to the innermost preceding `if` (first matching
  branch wins); a trailing `else` closes the chain. Chain branches may appear
  as siblings of the `if` block, matching Clausewitz layout.
- `while` composes with `if`/`random_list`/iterators/expressions; the loop
  condition can use the full trigger algebra including infix comparisons.

## Scope context, targets and collections

`ScriptExecutionContext` contains ROOT, current scope, FROM, the PREV stack, lexical
call frames, event-target bindings, typed collections and deterministic random-draw
counters. Scope selectors include THIS, ROOT, FROM, PREV, owner/parent navigation,
Country/Market/State/Province direct selectors and a saved/event target.

Implemented state operations include:

- set/change/clear and compare variables;
- save/clear event targets (`save_scope_as` remains a compatibility alias);
- add/remove/clear collection values;
- `any_*`, `every_*` and deterministic `random_*` scope iteration;
- `any_in:name`, `every_in:name` and `random_in:name` collection iteration.

A collection is homogeneous. Scope collections also retain the concrete element
scope type, so a Country collection cannot later accept a Pop even though both are
represented as `ScriptArgumentKind::Scope`. Unique insertion preserves deterministic
first-in order.

These bindings are transient for a standalone invocation. A live Event or Journal
instance can retain the complete context across ticks. The `GCT1` save section then
encodes it with bounded counts, stable keys, scope-reference validation, deterministic
checksum coverage and staging-before-commit restore. Completed gameplay instances
release their context. This is a gameplay persistence boundary, not yet a universal
global-variable store.

### War, front and army scopes (2026-09-06)

The GrandStrategyStore military records join the scriptable domains
(`ScopeType` now covers Country/State/Province/Pop/Market/Building/Army/
Front/War):

```thunder
script general_offensive {
    scope = war
    trigger = { war_active = yes   war_score < -30 }
    effect = {
        every_front = {
            limit = { front_progress_above = -100 }
            shift_front_progress = 5
        }
        shift_war_score = 3
    }
}
```

- Navigation: a country's `every_army` enumerates its own units; `every_front`
  from a war matches the attacker/defender pair in either order (the same rule
  as the weekly warfare pass); a country's `every_war` covers **active** wars
  only — `every_war` from a root/global context sees all records and content
  gates with `war_active = yes/no`.
- Primitives: triggers `army_manpower_above`, `army_organization_above`,
  `front_progress_above`, `war_score_above`, `war_weeks_above`, `war_active`
  (boolean); effects `army_add_manpower`, `set_army_organization`,
  `shift_front_progress`, `shift_war_score` (front/war shifts clamp on the
  shared ±100 relation scale).
- Value sources for expressions: `army_manpower`, `army_organization`,
  `front_progress`, `war_score`, `war_weeks`. Manpower is whole units;
  organization is 0..1; front/war progress/score are whole units (milli
  internally, like the pop columns).
- `owner =` from an army resolves its country; `state =` works from armies and
  fronts.

## Deterministic random semantics

Random iterator and random_list callsite salts derive from the program's stable
name and compiled structural path. They do not depend on source line numbers or
SymbolTable insertion order. The execution context stores sorted per-callsite draw
counters; repeated execution at one callsite advances a deterministic sequence
rather than returning the same choice forever. Live gameplay contexts save and
checksum those counters.

Ordered selection with scripted ordering values exists (`order_by`); scripted
weight values for `random_list` remain outstanding.

## Safety and diagnostics

The parser rejects malformed tokens, numeric overflow/non-finite literals, excessive
nesting and excessive AST size. Compilation rejects unknown primitives, scope and
argument mismatches, malformed call/signature blocks, stable-name hash collisions and
invalid defaults. The VM has call-depth checks and a per-public-invocation work budget;
the remaining budget is deliberately transient and excluded from save/checksum.

Use the engine's `ScriptCompiler` and `ScriptProgramDatabase` for startup-equivalent
parsing, compilation and link diagnostics without starting a game. Production
diagnostics still need richer mod source chains, script call stacks, cost profiling
and desync-context diffs.

## Save/checksum contract

Persistent contexts use stable 64-bit binding keys and are validated against the
restored World. Their checksum covers ROOT/current/FROM/PREV, parameters, variables,
targets, collections, seed and random-draw counters in deterministic order. Resource
budgets and other VM-local guard state are not simulation state and are not serialized.

Dedicated validation coverage includes insertion-order-independent checksums,
scope-subtype rejection, frame isolation, mixed-backend compilation, empty triggers,
stable random salts, repeated draw counters, parser limits, non-finite rejection,
typed signature defaults and failures, link-cycle diagnostics, and save/restore of a
live event context followed by identical continuation.

## Remaining major gaps

ThunderScript is a substantial foundation, but it is not yet a complete general
a production-scale grand-strategy content runtime. Important missing work includes:

- broader scope registration for politics, diplomacy, warfare, companies, characters
  and other domain objects;
- scripted sort and weight values for iterators/random_list (numeric weights and
  `limit = { ... }` candidate gating ship today; `order_by` accepts scripted
  values already);
- arrays/mixed containers in the format layer, and a general Date/Duration type
  (Date exists for history patches as a `yyyymmdd` literal; a general Date type
  does not);
- domain-owned persistent variables/lists outside gameplay instances (the
  world-level global store is persisted by the `GLB1` save extension);
- a typed on-action bus and saved delayed-effect/event scheduler;
- compact parameter slots and allocation-free high-frequency call frames;
- structured profiling, source provenance, compiled-content cache and editor-safe
  incremental reload.

New language features must remain data driven and preserve the existing fast path:
high-cardinality simulation loops should query SoA/CSR views or compiled numeric IDs,
not perform per-object string reflection or unbounded allocation.
