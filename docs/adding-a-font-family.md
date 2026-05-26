# Adding a font family

`FontManager` is the on-device runtime that resolves a dashboard's
`fontFamily` field (issues #971 + #500) to a typeface bundle loaded from
SPIFFS at boot. v1 ships a single family — `orbitron` — but the loader is
already structured around a `FontFamilyAssets` catalog so adding a second
family is a focused edit, not a refactor.

## v1 state

- Source of truth for the id enum: `canshift-core/src/schemas/font-family.ts`
  (`FontFamilyIdSchema`, `FONT_FAMILIES`).
- Firmware catalog row: `kOrbitronAssets` in
  `canshift-firmware/src/ui/font_manager.cpp`.
- Lookup: `resolveFamily()` returns the matching `FontFamilyAssets*` or
  `nullptr`. `FontManager::init(family)` falls back to `kDefaultFamily`
  (`orbitron`) on `nullptr`, with a `LOG_WARN` and an entry pushed onto
  `ErrorStore` so the dash surfaces the misconfiguration.

## Adding family `<id>` — checklist

Estimated effort: ~50 lines of firmware + the SPIFFS bundle.

1. **canshift-core** (separate PR, owned by core)
   - Extend `FontFamilyIdSchema` with `'<id>'`.
   - Append an entry to `FONT_FAMILIES` with `displayName` + `description`.

2. **Font binaries**
   - Generate `<id>_<weight>_<N>.bin` for every tier size with
     `lv_font_conv` (mirror the Orbitron sizes today: primary 32/48,
     secondary 20/24, label 12/14/16).
   - Drop the files into `canshift-firmware/data/fonts/`.
   - Run `pio run -t uploadfs` to provision them on the device.

3. **firmware catalog** (one file: `src/ui/font_manager.cpp`)
   - Append:
     ```cpp
     constexpr uint8_t k<Id>PrimarySizes[]   = {32, 48};
     constexpr uint8_t k<Id>SecondarySizes[] = {20, 24};
     constexpr uint8_t k<Id>LabelSizes[]     = {12, 14, 16};
     constexpr FontFamilyAssets::InFlashOverride k<Id>PrimaryFlash[] = {
         {0, nullptr, nullptr},
     };
     constexpr FontFamilyAssets k<Id>Assets = {
         "<id>", "black", "bold", "medium",
         k<Id>PrimarySizes,   sizeof(k<Id>PrimarySizes)/sizeof(uint8_t),
         k<Id>SecondarySizes, sizeof(k<Id>SecondarySizes)/sizeof(uint8_t),
         k<Id>LabelSizes,     sizeof(k<Id>LabelSizes)/sizeof(uint8_t),
         k<Id>PrimaryFlash, nullptr, nullptr,
     };
     ```
   - Add the branch to `resolveFamily()`:
     ```cpp
     if (strcmp(family, "<id>") == 0) return &k<Id>Assets;
     ```
   - If the family has a larger tier than `kMaxPrimaryCount` /
     `kMaxSecondaryCount` / `kMaxLabelCount`, bump those constants. The
     `static_assert` block right below the constants guarantees the build
     fails loudly when a tier overruns its slot count.

4. **Optional — in-flash overrides**
   - The Orbitron family hosts `black_32`, `black_48`, and `medium_14`
     in flash to keep the LVGL pool free for the bold / SPIFFS loads
     (see comments above `kOrbitronAssets`). Mirror the same pattern when
     a binary would not fit in the runtime pool; otherwise leave the
     override list at `{{0, nullptr, nullptr}}` and all sizes stream from
     SPIFFS.

5. **Tests**
   - `pio test -e native -f test_config_loader` — the existing
     `fontFamily` round-trip test exercises the parser; add a fixture
     with the new id if you want to assert the literal lands on
     `CfgDashboard.fontFamily` byte-identically.
   - `pio run -e crowpanel_28` — the link step catches forgotten symbols
     (in-flash overrides).
   - `pio run -e sim` — exercises the boot path under the host SPIFFS
     shim with no display dependency.

## Backward compatibility

Every dashboard authored before #1132 lacks the `fontFamily` field. The
config parser substitutes `"orbitron"` (`config_loader.cpp::loadDashboard`)
and `FontManager` keeps loading the v0.* bundle byte-for-byte. A
hand-edited config with an unknown id falls back to the same default
with a single `LOG_WARN` + `ErrorStore` entry — no boot failure.

## Out of scope today (post-#971)

- Live family swap (would require freeing the active SPIFFS-loaded tiers
  and reloading without restarting LVGL). Today the family is read once
  at boot from the persisted dashboard config.
- Per-widget family overrides — the schema field lives on the dashboard
  root and applies globally.
