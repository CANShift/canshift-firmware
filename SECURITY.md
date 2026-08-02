# Security Policy

## Supported Versions

CANShift ships from `main` — only the latest release of each component
receives security fixes. There is no LTS branch.

| Component                   | Supported |
| --------------------------- | --------- |
| Studio-web (latest release) | ✅        |
| Firmware (latest on `main`) | ✅        |
| Mobile (latest release)     | ✅        |
| Anything earlier            | ❌        |

A new release is tagged automatically whenever
`canshift-firmware/package.json` bumps and the change merges to `main`.

## Reporting a Vulnerability

Please **do not** open a public GitHub issue, discussion, or PR for security
reports.

Use the repository's
[**Report a vulnerability**](https://github.com/tburkhalterr/CANShift/security/advisories/new)
button (Security tab → Advisories → "Report a vulnerability"). This opens a
private GitHub Security Advisory visible only to you and the maintainers.

In your report please include:

- Affected component (`canshift-core`, `canshift-studio-web`, `canshift-firmware`,
  or `canshift-mobile`) and the commit SHA or release tag you tested against.
- A short description of the issue and the impact (what an attacker can read,
  write, or run).
- Reproduction steps or a proof-of-concept.

You can expect:

- An acknowledgement within **5 working days**.
- A triage decision (accepted / declined / needs-info) within **14 days**.
- For accepted reports: a coordinated fix, a patched release, and a credited
  advisory published via GitHub Security Advisories.

## Scope

In scope:

- Remote-exploitable issues in firmware HAL surfaces — BLE GATT, USB JSON
  protocol, Wi-Fi AP HTTP server, WS dispatcher, Wi-Fi OTA endpoint, OTA HMAC.
- `canshift-studio-web` payload parsing, WS transport, and outbound network
  calls (release fetch, firmware download).
- Mobile BLE / OTA flows.
- Schema-validation bypasses in `canshift-core` that let a malicious config
  reach the firmware.

Out of scope:

- Findings against forks or third-party hardware not listed in
  `canshift-firmware/include/hardware_profile.h`.
- Issues that require unrestricted physical access to an unlocked device
  (e.g. dumping flash with a soldered programmer).
- Self-XSS in the dash-hosted Studio with no privilege boundary crossed.
- Denial-of-service from malformed local CAN traffic — the device is expected
  to be on a trusted bus.
- Vulnerabilities in unmaintained or end-of-life dependencies that we already
  pin around (see `canshift-mobile/package.json` `//overrides` for examples)
  — open a dependency PR instead.
