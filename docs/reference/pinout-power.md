# Power rails

Source: [`include/board_config.h`](../../include/board_config.h)

The CrowPanel is powered and programmed over a single USB-C connector — there is no separate power jack.

## In: USB-C

USB-C brings in 5 V and, on the same cable, the host serial link. Data goes through the board's CH340 bridge on UART0 at 115200 baud (`USB_SERIAL_BAUD`); that link is what [canshift-tuner](https://canshift.app) speaks to over Web Serial.

## Logic: 3.3 V

The ESP32-WROOM and the panel run at 3.3 V, regulated from the incoming 5 V on the board itself. Nothing in the firmware touches that rail — it is fixed hardware — which is why the [pinout](../reference/pinout.md) is all 3.3 V logic and why a 3.3 V-tolerant CAN transceiver is on the [BOM](../reference/hardware-bom.md).

## The one rail firmware controls: the backlight

The backlight is the only rail the firmware modulates. `lcd.pin_bl` (GPIO 27) drives it through an LEDC PWM channel — channel 0 at 5 kHz — with the duty running 0–255 (default 200). Turning the panel brightness up or down is a software write, not a hardware change. The pin assignment is in [Display bus](../reference/pinout-display.md).

> [!NOTE]
> Powering the board from a weak USB source shows up as brownouts under load — the backlight and the CAN transceiver are the two draws that matter. If the dash resets when the backlight ramps up, feed it from a supply that can hold 5 V at the current the panel pulls.
