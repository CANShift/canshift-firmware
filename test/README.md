# Native unit tests

Unity-based host tests for firmware logic that has no hardware dependencies.

## Run

```bash
cd canshift-firmware
pio test -e native
```

The `[env:native]` PlatformIO environment compiles a tiny subset of the source
tree (config loader, JSON reader, signal store, MaxxECU parser, signal map)
against in-memory shims under `test/native/shim/`. No ESP32 toolchain or
hardware is required.

## Layout

```
test/
├── native/shim/                 # Host-only stand-ins for Arduino, FreeRTOS,
│                                # logger, storage, error_store
├── test_maxxecu_parser/         # Multi-byte CAN decoder (LE/BE/signed/mask)
├── test_signal_store/           # Thread-safe signal value store
└── test_config_loader/          # JSON → CfgDashboard / CfgSignalConfig
    └── fixtures/                # Embedded JSON payloads
```

The shim layer is added to the include search path **before** `include/` and
`src/`, so production headers like `freertos/FreeRTOS.h` resolve to the
host-friendly versions during the native build only. The on-device PlatformIO
environments (`crowpanel_28`, `sim`, `debug`, `debug-perf`) never see `test/`.
