# Migration status (monorepo → CANShift org)

Split from `tburkhalterr/CANShift` with history. Remaining cutover steps:

0. `core-schema-version.txt` pins `CURRENT_SCHEMA_VERSION` for standalone builds — a sibling `canshift-core` checkout still takes precedence; the cross-repo parity job must keep the pin in sync on schema bumps.
1. Move the `OTA_HMAC_SECRET` secret to this repo (CI falls back to a per-run value; release builds need the real one).
2. ~~Port `release.yml`~~ DONE — version-bump-triggered release on main, identical artifact set (merged/firmware/spiffs + optional Ed25519 sigs). Needs `OTA_HMAC_SECRET` (required) and `FIRMWARE_SIGNING_PRIVATE_KEY` (optional) secrets before the first release.
3. Point the tuner flasher at this repo's releases once the first release is cut.
4. Transfer `scope:firmware` issues; flip public.
