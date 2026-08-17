# Hardware & BOM

Everything needed to build one dash: the reference parts, with an indicative price and a supplier for each region. Pick where you ship to and the table re-prices.

| Part                    | Spec                                      |     USD |     EUR |     GBP |        CHF |
| ----------------------- | ----------------------------------------- | ------: | ------: | ------: | ---------: |
| Elecrow CrowPanel 2.8″  | ESP32-WROOM · ILI9341 SPI · XPT2046 touch |     $19 |     €20 |     £18 |     CHF 20 |
| NXP TJA1051T/3 breakout | 3.3 V CAN transceiver                     |      $4 |      €4 |      £3 |      CHF 5 |
| OBD-II to DB9 cable     | vehicle CAN tap                           |      $9 |      €9 |      £8 |     CHF 10 |
| Dupont + JST wire kit   | jumpers and crimps                        |      $6 |      €6 |      £5 |      CHF 7 |
| USB-C 5 V supply        | 1 A bench power                           |      $7 |      €7 |      £6 |      CHF 8 |
| 120 Ω terminators ×2    | bus termination, if needed                |      $1 |      €1 |      £1 |      CHF 2 |
| Printed bezel           | 2.8″ panel mount                          |      $6 |      €6 |      £5 |      CHF 7 |
| **Bench kit total**     |                                           | **$52** | **€53** | **£46** | **CHF 59** |

Optional extras:

| Part              | Spec                       |     USD |     EUR |     GBP |         CHF |
| ----------------- | -------------------------- | ------: | ------: | ------: | ----------: |
| OBD-II Y-splitter | keep the port usable       |     $12 |     €12 |     £10 |      CHF 13 |
| Dashboard pod     | A-pillar or dash-top mount |     $18 |     €18 |     £16 |      CHF 20 |
| Inline fuse tap   | add-a-circuit, 3 A         |      $6 |      €6 |      £5 |       CHF 7 |
| Panel-mount USB-C | bulkhead extension         |      $5 |      €5 |      £4 |       CHF 6 |
| **In-car total**  |                            | **$93** | **€94** | **£81** | **CHF 105** |

Default suppliers — US: [Mouser](https://www.mouser.com) · EU: [Reichelt](https://www.reichelt.de) · UK: [Pimoroni](https://shop.pimoroni.com) · CH: [Distrelec](https://www.distrelec.ch). The panel ships from [Elecrow](https://www.elecrow.com/esp32-display-2-8-inch-hmi-display-spi-tft-lcd-touch-screen.html) in the US and EU; the transceiver from [Adafruit](https://www.adafruit.com/product/5708) in the US and [Berrybase](https://www.berrybase.de) in the EU.

Prices checked August 2026, excluding tax and shipping.

## Substitutions that work

**Any ESP32 + ILI9341 panel.** Keep the GPIO contract from [Pinout](../reference/pinout.md) and the firmware builds unchanged. WROVER boards work too — the PSRAM is simply left unused.

**SN65HVD230 instead of the TJA1051.** Cheaper and widely cloned. Ground the slew-rate pin; expect more errors above 500 kbit/s.

**A Western distributor instead of AliExpress.** Mouser and DigiKey stock equivalent ESP32 panels — a little more money, a couple of weeks less waiting.

> [!WARNING]
> Do not fit the 120 Ω terminators unless the dash sits at a physical end of the bus. Most cars are already terminated at both ends — a third resistor drops the bus to ~40 Ω and the ECU stops talking.
> [!NOTE]
> Prices are indicative and were not taken from a live feed — treat them as a starting point, not a quote, and always excluding tax and shipping. Found a better source, or a part that went out of stock? [Open an issue](https://github.com/CANShift/canshift-firmware/issues).
