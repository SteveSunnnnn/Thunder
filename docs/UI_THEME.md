# UI theme and typography

The engine ships one default visual language for all UI: an original
nineteenth-century grand-strategy aesthetic in the same genre tradition as
modern historical strategy games — deep desaturated greens, aged brass and
gold trim, warm ivory text, and wood/leather/parchment materials. All
materials and ornaments are original engine implementations; nothing is
taken from any commercial title. Every drawing primitive,
scripted GUI widget, tooltip and HUD surface reads its colors, metrics and
font sizes from a single `UiTheme` (`src/thunder/presentation/ui/UiTheme.hpp`). Business code
and `.thundergui` scripts describe *what* to show; the theme decides *how it
looks*.

## Ownership

| Layer | Responsibility |
| --- | --- |
| `thunder::UiTheme` | Design tokens: colors, materials, typography, metrics, motion |
| `thunder::UiDrawList` | Materials + generic components; reads `theme()` (default `UiTheme::victorian()`) |
| `thunder::ScriptedGuiPainter` | Scripted GUI widget styling from `ScriptedGuiPaintTheme` (mapped from `UiTheme`) |
| `thunder::TooltipStack` | Tooltip chrome (parchment, leather title plaque, term links) |

A custom theme is installed per draw list (`draw_list.set_theme(&my_theme)`)
or per scripted painter (`make_scripted_gui_paint_theme(my_theme)`). A null
theme pointer always resolves to the Victorian default, so dynamically
generated UI can never fall back to unstyled controls.

## Token groups

- **Backgrounds**: `bg_base`, `bg_deep`, `bg_panel`, `bg_panel_raised`,
  `bg_panel_recessed`, `bg_header`, `bg_floating`, `bg_modal`.
- **Borders**: `border_dark/normal/light`, `border_gold`, `border_brass`,
  `border_selected`.
- **Accents**: emerald family, `gold`, `brass`, `burgundy`, `steel`
  (subdued blue-gray).
- **Text**: `text_primary` (warm ivory), `text_secondary`, `text_muted`,
  `text_disabled`, `text_gold`, `text_positive`, `text_negative`,
  `text_warning`.
- **States**: `state_hover` (brightening overlay), `state_pressed`
  (darkening overlay), `state_selected`, `state_disabled`, `state_focus`.
- **Shadows**: `shadow_raised`, `shadow_floating`, `shadow_modal` map to the
  BASE / RAISED / FLOATING / MODAL depth tiers.
- **Materials**: wood, parchment, leather, brass and wax tokens, including
  parchment text colors.
- **Metrics**: compact grand-strategy spacing (`space_xxs`–`space_xl`),
  border thicknesses, `header_height`, `top_bar_height`, `tab_height`,
  `row_height`, `table_header_height`, `button_height`, `scrollbar_thickness`,
  `slider_track_height`, `control_size`, `tooltip_width`.
- **Motion**: `hover_ms`, `tooltip_ms`, `panel_ms`, `notification_ms` —
  short, restrained, functional. Hosts animate with these budgets; the engine
  does not bounce or zoom.

Color packing is `0xAARRGGBB`. `ui_blend` and `ui_apply_overlay` produce
shaded/highlighted variants so callers never invent intermediate colors.

## Component catalog (UiDrawList)

Smooth-shading primitives: `quad_gradient` (per-vertex color ramp, no
banding) and `drop_shadow` (stacked penumbra layers). All materials below are
built from them, so gradients are smooth and shadows soft at any size.

Materials: `panel`, `wood_panel`, `parchment_panel`, `leather_panel`,
`brass_button`, `wax_seal`, `progress_bar`, `parliament_arc`, `gauge_balance`,
`ink_chart`, `construction_queue_row`, `tariff_slider_input_row`.

Generic components: `v_gradient`, `corner_ornaments`, `divider_ornament`,
`separator`, `ornate_header`, `window_frame`, `tab`, `dropdown_row`,
`checkbox`, `radio`, `slider`, `scrollbar`, `input_box`, `list_row`,
`table_header_cell`, `stat_row`, `notification_card` (5 severity tiers),
`modal_window`.

Interaction states: hover, pressed/active, selected, disabled and focus are
all expressed; hover brightens edges, selection keeps persistent gold
emphasis, disabled sinks surfaces into the background. Numeric columns in
`list_row`/`stat_row` are right-aligned to keep columns scannable.

## Declaring surfaces in scripted GUI content

`.thundergui` panels declare their material role, never a color:

```
panel = { id = top_bar style = wood children = { ... } }
panel = { id = page    style = parchment text = hud.page.market children = { ... } }
```

Valid styles: `standard`, `wood`, `parchment`, `leather`, `recessed`. A panel
with a `text` field renders it as an ornate header band and lays its children
out below the header.

## Bundled fonts

`assets/fonts/` contains baked MSDF atlases (OFL-licensed; see
`THIRD_PARTY.md`):

- `ui_display` — Playfair Display Bold, for display roles;
- `ui_body` — EB Garamond Medium, the default body/number face.

An engine host may auto-load `ui_body` when `THUNDER_UI_FONT_ATLAS` is unset
(searched from `assets/fonts`, `../assets/fonts`, `../../assets/fonts`). Bake
alternates with `tools/assets/cook_fonts.py` and `tools/assets/charset_ui.txt`
(ASCII plus em/en dash, middle dot, multiplication sign, ellipsis). CJK
content bakes a supplementary atlas with a charset file listing the needed
code points; the runtime falls back per glyph.

## Typography

Font sizes and line heights come from `UiThemeTypography`; no caller passes a
raw size. Roles:

| Role | Size | Use |
| --- | --- | --- |
| `display` | 22 | splash, event titles |
| `window_title` | 16 | window header titles |
| `major_header` | 14 | section headers |
| `body` | 13 | default body text |
| `secondary` | 12 | secondary descriptions |
| `caption` | 11 | badges, hints |
| `stat_value` | 14 | right-aligned stat numbers |
| `stat_delta` | 11 | +/− deltas |

The engine rasterizes MSDF `.thunderfont` atlases supplied by the game; it
intentionally bundles no font binaries. Recommended choices for the default
look (any license-compatible substitute with similar weight and x-height
works):

- **Display / window titles**: a 19th-century display serif (Playfair
  Display, IM Fell English or EB Garamond Bold).
- **Body**: a humanist serif or calm sans with high legibility at 12–14 px
  (Alegreya, Source Serif).
- **Stat values**: the same family with tabular figures where available, to
  stop numeric columns from jittering as values change.

Bake with the shipped cooker (requires `msdf-atlas-gen`):

```sh
python tools/assets/cook_fonts.py --font <path.ttf> --out-dir assets/font \
    --name ui_serif --atlas-key ui/serif --size 48
```

An engine host loads the atlas through `THUNDER_UI_FONT_ATLAS` /
`THUNDER_UI_FONT_METRICS` (`.thunderimg` + `.thunderfont`). CJK games bake a second
atlas with a `--charset` file; the runtime falls back per glyph.

## Number presentation

`ui_format_number` (thousands separators, fixed decimals, compact K/M/B) and
`ui_format_delta` (+/− sign) keep strategy numbers uniform. `ui_delta_color`
maps a delta to the theme's positive/negative/neutral text colors.

## Density and scaling

The default metrics target 1920×1080 information density: 24 px list rows,
22 px table headers, 38 px top bar, 8 px scrollbars, tight padding. Because
all geometry is resolution-independent quad/text commands, hosts scale by
multiplying `UiThemeMetrics` and rendering at the higher resolution — MSDF
text stays sharp at 1440p/2160p.
