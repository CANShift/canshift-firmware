# Migration status (monorepo → CANShift org)

Split from `tburkhalterr/CANShift` with history. Remaining cutover steps:

1. Move the `OTA_HMAC_SECRET` secret to this repo (CI falls back to a per-run value; release builds need the real one).
2. Port `release.yml` (version-bump-triggered firmware release) from the monorepo — releases restart fresh here.
3. Point the tuner flasher at this repo's releases once the first release is cut.
4. Transfer `scope:firmware` issues; flip public.
