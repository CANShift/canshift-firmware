# Transports

The dash talks to the outside world over two transports. Both carry the same newline-delimited JSON, but they serve different clients and only one of them is always present.

## USB CDC — the primary link

USB is the dash's main host link and it is always available. It is what [canshift-tuner](https://tuner.canshift.app) speaks to: command/response lines, a ~10 Hz telemetry stream, and structured log lines. It runs at 115200 baud over the board's CH340 UART0 bridge, and the protocol version is pinned by `USB_PROTOCOL_VERSION = 2` in `include/app_config.h`.

Incoming lines land in `handleCommand()` (`src/hal/usb/usb_dispatch.cpp`); replies go back out through `UsbComm::sendLine()` (`src/hal/usb/usb_comm.cpp`). The full command surface, framing and burn flow live on [USB CDC](../architecture/usb-transport.md).

## BLE GATT — the optional link

BLE is a secondary transport for the mobile companion, and it only exists when the firmware is built with `APP_BLE_ENABLED` (every BLE call site in `src/main.cpp` sits behind that guard). It exposes a single NimBLE GATT service: telemetry and status go out over notify, and a small, fixed command set (day/night, calibration, reboot, timer) comes in over a write characteristic handled in `src/hal/ble/ble_server.cpp`. The GATT layout and pairing flow are on [BLE GATT](../architecture/ble-transport.md).

> [!NOTE]
> BLE and its NimBLE stack reserve their heap arena early in `main()`, before the display comes up — a no-PSRAM WROOM cannot claim a large contiguous block once LVGL owns its pool. That ordering is the subject of [Boot sequence](../architecture/boot-sequence.md).

## What they share

Both transports move JSON, so telemetry and status use the same shapes on either link. What they do **not** share is the command handler: USB routes lines through the full dispatcher, while BLE's write characteristic services only its own short command list. Treat USB as the complete control surface and BLE as the in-car convenience link.
