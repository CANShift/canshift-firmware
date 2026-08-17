# Add a new dashboard widget

How to introduce a brand-new widget type (e.g. `tacho_arc`, `lap_delta`)
across the three repositories it has to land in: the schema in
`canshift-core`, the LVGL renderer in `canshift-firmware`, and the editor
surface in `canshift-tuner`.

Defer to the existing implementations as the primary reference — this guide
spells out the contract between repositories and the touch-points each new
widget must hit, not a line-by-line recipe.

## Overview

A "widget" in CANShift is a single piece of on-screen data: a gauge, a
warning lamp, a tap button, a numeric label, an image. It exists in three
repositories at once:

1. **`canshift-core`** owns the Zod schema for its config block. Every
   consumer reads the same shape (firmware, tuner, mobile). Core is published
   to npm as `@canshift/core`, so a schema change lands and is released here
   first, then the consumers bump their dependency.
2. **`canshift-firmware`** has the LVGL renderer that turns the config block
   into pixels on the dash, plus the per-frame update tick.
3. **`canshift-tuner`** has the editor UI (palette tile + property panel) and
   the canvas preview rendering.

Adding a new type means visiting all three repositories plus their tests. None
of the steps are optional — TypeScript exhaustiveness checks in the tuner and
the firmware factory switch catch most misses, but the schema is the gating
contract.

Pick the closest existing implementation and mirror it. In the firmware,
`warning_widget` is the canonical "binary signal" widget, `button_widget` is
the canonical "no signal, runs an action" widget, and `image_widget` is the
simplest (no signal binding, no per-frame update). `gauge_widget` shows the
multi-sub-style pattern.

## Step 1 — Schema (`canshift-core`)

The widget discriminated union lives in
[`src/schemas/dashboard.ts`](https://github.com/CANShift/canshift-core/blob/main/src/schemas/dashboard.ts).

### 1.1 Add the per-type config schema

Define a strict `z.object({...}).strict()` with a `type` literal discriminant
and every widget-specific field. Reuse the shared helpers already in the file
(label position, sensor icon name, decimal-places bounds, colour ramp) rather
than re-declaring them.

Keep field names in `camelCase` — the wire-format JSON snake_case conversion
lives at the boundary, not in the schema.

### 1.2 Wire it into the union

Add the schema to the widget config union in `dashboard.ts`. The derived
widget-type enum updates automatically from the union options, so the
discriminant string is declared once. Export the inferred type alongside the
schema and add it to the `WidgetConfig` union alias near the bottom of the
file.

### 1.3 Cross-field invariants (optional)

If the widget has relationships between fields (e.g. `minValue < maxValue`),
express them in the `.superRefine()` block — schema-level checks beat runtime
checks in every consumer.

### 1.4 Schema tests

Add tests to the schema suite covering: valid config parses; missing required
fields rejected; extra fields rejected (`.strict()`); cross-field invariants
rejected. Run `npm test` inside `canshift-core`.

### 1.5 Migrations

If you renamed or removed a field on an existing widget, write a migration in
`src/migrations/` and bump `CURRENT_SCHEMA_VERSION`. A brand-new widget type
does not need a migration — old configs simply do not contain it.

### 1.6 Publish before consuming

The tuner and mobile app pull `@canshift/core` from npm. Land and publish the
schema change first, then bump the dependency in the consumer PR. See the
[Release process](../contributing/release-process.md) for the publish
flow.

## Step 2 — Firmware renderer (`canshift-firmware`)

The widgets live in
[`src/ui/widgets/`](https://github.com/CANShift/canshift-firmware/tree/main/src/ui/widgets).
Every widget has the same layout:

1. New files `<name>_widget.h` + `<name>_widget.cpp` under `src/ui/widgets/`.
2. Enum entry in `WidgetType` (`src/config/config_types.h`).
3. String → enum row in `parseWidgetType()` (`src/config/config_loader.cpp`).
4. Config struct + parser if the widget needs typed runtime state.
5. Dispatch entries in `widget_factory.cpp` (both `create()` and
   `updateWidget()`).
6. `pio run -t format` before committing — clang-format gates every PR.

### 2.1 Header

Mirror an existing header such as `warning_widget.h`: a static-only namespace,
no classes. Stateless from the outside — per-widget state lives in a Tag struct
attached via `lv_obj_set_user_data`.

### 2.2 Implementation

Mirror an existing renderer such as `warning_widget.cpp`. The non-negotiable
pieces:

- **Tag struct** in an anonymous namespace, holding every nullable child LVGL
  pointer, every cached layout value, and every "last painted" guard variable.
  Keep it within the tag-pool slot budget (`WidgetTagPool::kSlotBytes`).
- **Allocate the Tag** via `WidgetTagPool::alloc<FooTag>()`. Never `new`, never
  `malloc`. Pool exhaustion returns `nullptr`; log and bail.
- **Attach the deleter** so the slot is released on teardown:
  ```cpp
  lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<FooTag>,
                      LV_EVENT_DELETE, tag);
  ```
- **`WidgetHelpers::initContainer()`** creates the outer container with the
  right size/position/border. Use it.
- **No string class.** Fixed `char[N]` + `snprintf`/`strlcpy` only.
- **No magic numbers.** Promote pixel ratios to `static constexpr` at the top
  of the anonymous namespace.
- **Function length cap ~40 lines.** Split `create()` into phase helpers if it
  grows past that.
- **`update()` must be cheap.** Cache the last value / last fill colour on the
  Tag and guard against redundant `lv_obj_set_style_*` calls — every widget
  re-renders once per frame and each redundant style call is a UI mutex
  round-trip.

### 2.3 Enum + parser

Add the new entry to `WidgetType` in `config_types.h` and the matching
`strcmp` arm in `parseWidgetType()` in `config_loader.cpp`. The string must
match the Zod literal exactly.

### 2.4 Factory dispatch

In `widget_factory.cpp`, add a `case` to the `switch` in
`WidgetFactory::create()` that calls a thin adapter, and a `case` to
`updateWidget()` that calls your `update()`. If the widget has no per-tick
value, follow `image_widget` or `button_widget`'s pattern instead.

### 2.5 Native tests (optional)

Pure helpers (value scaling, range clamping, label formatting) can be
exercised under `test/native/`. Widgets that touch LVGL are hard to unit-test;
if you don't add tests, document the manual on-device verification in the PR
body.

## Step 3 — Tuner editor surface (`canshift-tuner`)

Three touch-points, all under
[`src/components/editor/`](https://github.com/CANShift/canshift-tuner/tree/main/src/components/editor).

### 3.1 Palette tile

In `WidgetPalette.tsx`, add an entry to the palette items with a default size
token, icon, and default signal, plus a matching branch to the base-config
builder that returns the initial config block. The `type` literal must match
the Zod discriminant.

### 3.2 Property-panel fields

Create `property-panel/<name>-fields.tsx`, mirroring an existing one such as
`gauge-fields.tsx` or `button-fields.tsx`. The contract:

- Export a component with the shared `ConfigFieldsProps` signature from
  `property-panel/shared.tsx`.
- Narrow `widget.config.type === '<name>'` once at the top, `return null`
  otherwise — TypeScript narrows the rest.
- Mutate config by spreading `cfg`, applying the change, and calling
  `onChange({ config: next })`. Never mutate the live store value.
- Reuse `Field`, `Row`, `inputStyle`, `IconPicker`, and the unit list from
  `shared.tsx` — do not import direct hex literals.

Then wire the component into `PropertyPanel.tsx` by adding it to the
config-fields map. The map is typed against the widget-type union, so a missing
key fails typecheck.

### 3.3 Canvas preview

`WidgetPreview.tsx` holds the canvas renderer. Add a `memo`'d preview component
and register it in the renderers map. The map is typed as a record over the
widget-type union — TypeScript refuses to compile if any type is missing.

If the preview reads a manual unit override, extend the resolved-unit hook so
the new type participates in unit resolution.

### 3.4 Preview tests

The tuner uses Vitest. Add a render-without-crash test colocated with the
component: supply a minimal `Widget` and assert the renderer returns a non-null
element across valid and invalid signal states.

## Step 4 — Documentation

If the widget changes a top-level capability (a new "lap delta" widget, not a
new gauge sub-style), surface it in these docs and in any tuner-facing hint or
empty-state copy. Do not duplicate code snippets — link to the schema and the
reference widget instead. Snippets rot.

## Common pitfalls

### LVGL mutex contract

Every `lv_*` call must run on the UI task or under `lv_lock()`/`lv_unlock()`.
The factory `create()` and `update()` paths already run from the LVGL task, so
widgets do not lock themselves — but if you spawn a FreeRTOS task that mutates
a widget you must take the lock.

### Tag pool budget

`WidgetTagPool::kSlotBytes` caps every Tag struct; a `static_assert` inside
`alloc<T>()` fails the build if you cross it. Trim fields (prefer
`uint8_t`/`int16_t` over `int`/`float`) or bump the constant in
`widget_tag_pool.h` and accept the BSS cost.

### Never `new`/`delete`

No dynamic allocation in hot paths. The Tag pool exists to satisfy this rule
for widgets — if you reach for `new` to allocate per-widget state, the answer
is "add a field to the Tag struct" or "extend the pool".

### Firmware string ↔ schema literal must match

The Zod literal, the palette entry, the `parseWidgetType` strcmp, and the
firmware `WidgetType` enum must all use the same exact string. A mismatch means
a silent `WidgetType::UNKNOWN` and a missing widget on the dash with only a log
warning to debug from.

### Discriminated-union exhaustiveness

Every consumer of the widget-config union narrows on `config.type`. The
most-frequently-missed touchpoint is the resolved-unit hook in the tuner —
confirm every site handles the new variant.

### Suffix / unit resolution

Numeric widgets resolve their unit in two stages: the widget config's manual
suffix override wins, otherwise the bound signal's `unit` (from `signals.json`)
is used. Both the firmware and the tuner implement this — route through the
existing helpers, do not invent a new lookup path.

## Related

- [Config contract](https://github.com/CANShift/canshift-core/blob/main/docs/config-contract.md) — JSON config shape
- [Firmware architecture overview](../architecture/overview.md) — package boundaries
- [`canshift-firmware`](https://github.com/CANShift/canshift-firmware) — firmware repository
- [`canshift-core`](https://github.com/CANShift/canshift-core) — shared contracts
- [`canshift-tuner`](https://github.com/CANShift/canshift-tuner) — editor + flasher
