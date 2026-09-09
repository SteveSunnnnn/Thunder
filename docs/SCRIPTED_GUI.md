# Scripted GUI blueprints

`ScriptedGui` is Thunder's game-agnostic declarative UI content compiler. It turns
ThunderScript objects into immutable widget, binding, template, and collection-view
arrays. Parsing and schema validation happen at content load; frame rendering does
not resolve widget types, commands, properties, or template names from strings.

The subsystem intentionally does not contain country, economy, politics, or any
other game-specific property. A game registers its own typed data-context schema
and command catalog before compiling content.

## Pipeline

```text
ThunderScript GUI source
  -> ThunderScript parser and SymbolTable
  -> ScriptedGuiSchema validation
  -> stable-key template/screen ordering
  -> compact node, binding-step, constant, and metadata arrays
  -> renderer/data-context provider
```

`ScriptedGuiBlueprint` is content state rather than authoritative simulation
state. It is rebuilt from the active mod stack and therefore is not written into
save games or included in the world checksum. Its own deterministic `checksum()`
is available for compiled-content caches, multiplayer content handshakes, tests,
and hot-reload invalidation.

## Registering a data schema

```cpp
thunder::SymbolTable symbols;
thunder::ScriptedGuiSchema schema{symbols};

const auto polity = schema.register_context("polity");
const auto region = schema.register_context("region");

schema.register_property(polity, "display_name", thunder::UiValueType::Text);
schema.register_property(polity, "can_reform", thunder::UiValueType::Boolean);
schema.register_property(polity, "regions", thunder::UiValueType::Collection, region);
schema.register_property(polity, "capital", thunder::UiValueType::Entity, region);
schema.register_property(polity, "output_history", thunder::UiValueType::NumberSeries);
schema.register_property(region, "display_name", thunder::UiValueType::Text);
schema.register_command("open_reform_screen");
```

Supported value types are boolean, number, text, localization key, asset, color,
entity, collection, number series, and text series. An entity property names the
context reached by the next path step. A collection property names its element
context. Scalar properties must not specify a related context.

Property names cannot contain `.` because dots delimit typed paths. Registration
rejects duplicate names, invalid related contexts, and stable-hash collisions.
The runtime data-context provider uses the same `UiDataContextId` and per-context
`property_slot` values emitted into the blueprint.

## Source format

Templates use `ui_template` (the `gui_template` alias is also accepted). Screens
use `scripted_gui` (`gui_screen` is accepted as an alias). Both require a context
and one root widget.

```text
ui_template region_row {
    context = region
    root = {
        type = row
        gap = 4
        children = {
            label = { text = { bind = display_name } }
        }
    }
}

scripted_gui polity_overview {
    context = polity
    root = {
        type = column
        id = overview_root
        gap = 8
        children = {
            label = {
                text = { bind = capital.display_name }
            }
            button = {
                text = reform_button_label
                visible = { bind = can_reform }
                enabled = yes
                command = open_reform_screen
                tooltip = reform_button_tooltip
            }
            list = {
                items = { bind = regions }
                item_template = region_row
                row_height = 28
                overscan = 3
                virtualized = yes
            }
            chart = {
                series = { bind = output_history }
                chart_kind = line
                max_points = 512
                include_zero = yes
            }
        }
    }
}
```

Children may use `widget = { type = ... }` or a widget-name shorthand such as
`label = { ... }`. `use = { template = region_row }` instantiates a template in
the current context. A root containing `template = name` is also a template
instance.

Implemented widget kinds are panel, row, column, stack, label, button, image,
scroll view, list, grid, chart, spacer, progress, and template instance.

### Common fields

- `id`: optional stable node-local identifier;
- `visible`, `enabled`: `yes`/`no` or a boolean binding;
- `command`: an ID registered in the command schema;
- `tooltip`: stable localization key;
- `width`, `height`, `min_width`, `min_height`, `max_width`, `max_height`,
  `grow`: finite numeric constants;
- `children`: valid on container widgets;
- `gap`: valid on row and column.

Labels and buttons accept `text`; images and buttons accept `icon`; buttons accept
`selected`; progress accepts `value`. Each may be a typed binding where applicable.
Static text is treated as a localization key and static icons as asset keys.

## Typed bindings

A binding is always explicit:

```text
visible = { bind = can_reform }
text = { bind = capital.display_name }
```

The compiler resolves each path segment against the current schema. Intermediate
segments must be entity values, and the final type must match the widget property.
The compiled form is a `CompiledUiBinding` plus contiguous
`CompiledUiBindingStep` entries. Each step contains only a compact context ID and
property slot. Consequently a frame does not hash, intern, or compare property
names.

Current target types are:

| Widget property | Required data type |
| --- | --- |
| `visible`, `enabled`, `selected` | Boolean |
| `text` | Text or LocalizationKey |
| `value` | Number |
| `icon` | Asset |
| list/grid `items` | Collection |
| chart `series` | NumberSeries |
| chart `labels` | TextSeries |

## Templates and collection views

Template records are sorted by their stable key before compact IDs are assigned.
A normal template instance must have the same context as the caller. A list/grid
item template must have the collection's element context. All template references,
including list/grid item references, participate in cycle detection.

List metadata contains the resolved items binding, item-template ID, row height,
overscan count, and virtualization flag. Grid metadata adds column count and row/
column gaps. Chart metadata contains resolved series and optional label bindings,
chart kind, point cap, and zero-baseline choice. Widget nodes refer to these typed
metadata arrays by integer index.

Virtualized view calculation remains in `StrategyUi` (`virtualize_rows` and
`virtualize_variable_rows`). It consumes numeric metadata and collection length;
it does not need the GUI compiler or the symbol table in the frame loop.

## Stable identity and compact runtime layout

- Template and screen names are FNV-1a stable keys and their records are sorted by
  those keys before compact IDs are assigned.
- An explicit widget `id` is combined with its owning template/screen stable key,
  so it is stable without requiring globally unique local names.
- Commands retain both a compact schema ID and a stable command key.
- Tooltip, localization, static-text, and asset references are stored as stable
  keys, not string objects or symbol-table insertion-order IDs.
- Widget relationships are `UiNodeId` links (`parent`, `first_child`,
  `next_sibling`). Bindings, constants, and specialized metadata are contiguous
  arrays addressed by ranges or indices.
- Reordering top-level source objects does not change the blueprint checksum.

The compiled records are trivially copyable. They own no `std::string`, map, or
callback object. Diagnostics keep strings because they exist only during tooling
and content load.

## Diagnostics

Compilation reports source locations and stable diagnostic codes for:

- parser errors;
- duplicate objects, fields, and widget IDs;
- stable-key collisions;
- missing or unknown fields and widget kinds;
- unknown contexts, commands, templates, and binding path segments;
- binding and template-context type mismatches;
- invalid numeric/boolean/value shapes;
- direct or indirect template cycles.

A result with any diagnostic has `ok() == false`. The partially produced blueprint
is for tooling inspection only and must not be installed as live UI content.

## Content-pipeline integration boundary

`ScriptedGuiCompiler` accepts either source text or an existing
`ScriptParseResult`, so the mod/content loader can parse each merged virtual file
once and feed the same syntax tree to multiple definition compilers. The intended
integration sequence is:

1. register engine/game data contexts and commands in deterministic order;
2. parse merged GUI files through the existing ThunderScript parser;
3. compile one blueprint and reject the content transaction on diagnostics;
4. publish the immutable blueprint beside localization and other definition data;
5. bind renderer-side context providers by compact context/property IDs.

The foundation deliberately does not modify `DefinitionDatabase` yet: the owning
game must first define which data-context providers and command dispatcher are
installed. Adding a blueprint field to the definition database before that owner
exists would couple generic content validation to an incomplete runtime contract.

## Retained UI runtime (ScriptedGuiRuntime)

`ScriptedGuiRuntime` is the presentation-side retained state machine built from a
compiled `ScriptedGuiBlueprint`:

```cpp
thunder::ScriptedGuiRuntime runtime{blueprint};
runtime.instantiate_screen(thunder::ui_stable_key("country_overview"), root_country_entity);

// Per-frame or per-tick evaluation
thunder::UiRuntimeDiff diff = runtime.refresh(dataProvider, viewports);
if (diff.dirty_nodes > 0) {
    // Process updated/dirty nodes
}
```

Key runtime properties:
- **Zero Game-Entity Coupling**: Communicates exclusively via `UiDataEntityRef`, `UiDataCollectionRef`, and `ScriptedGuiDataProvider`.
- **Generational Dirty Tracking**: Nodes track property mutations (`UiRuntimeDirty`) with monotonically increasing generation numbers so the renderer only re-emits modified batches.
- **Virtualized Viewports**: List and Grid containers consume `UiCollectionViewport` offsets and evaluate visible ranges without allocating per-element strings.
- **Outlier-Resistant Chart Sampling**: Series bindings downsample dense time series data to configured vertex budgets while preserving zero baselines.
