# Waveshare ESP32-C6 Touch AMOLED 2.16 Hardware Baseline

Verified on 2026-09-14 against the Waveshare product documentation, the
official Waveshare example repository, and the board configuration in the
official XiaoZhi example.

## Primary Sources

- Product documentation:
  https://docs.waveshare.net/ESP32-C6-Touch-AMOLED-2.16/
- Official examples and firmware:
  https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16
- Official XiaoZhi board configuration:
  https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16/tree/main/02_Example/XiaoZhi-v2.2.5/main/boards/waveshare/esp32-c6-touch-amoled-2.16
- Schematic link published by Waveshare:
  https://www.waveshare.net/w/upload/4/47/ESP32-C6-Touch-AMOLED-2.16-Schematic.pdf

The Waveshare file server applies a browser challenge to the schematic PDF.
The pin map below is therefore cross-checked against the official interface
diagram and the complete board definition used by the official firmware.

## Board Summary

| Item | Specification |
| --- | --- |
| Board | Waveshare ESP32-C6-Touch-AMOLED-2.16 |
| SKU | 34202; English variant 34201 |
| MCU | ESP32-C6, single-core 32-bit RISC-V |
| CPU clock | Up to 160 MHz |
| Internal memory | 512 KB HP SRAM, 16 KB LP SRAM, 320 KB ROM |
| External flash | 16 MB NOR flash |
| External PSRAM | None stated |
| Wireless | 2.4 GHz Wi-Fi 6, Bluetooth 5 LE, IEEE 802.15.4 |
| Antenna | Onboard antenna; IPEX Gen 1 connector selected by board rework |
| USB | USB Type-C connected to the ESP32-C6 USB interface |

The board is intended for compact display and AI voice prototypes. Its main
constraint is that the C6 has no external PSRAM while driving a 480 x 480
AMOLED display.

## Display and Touch

| Item | Specification |
| --- | --- |
| Panel | 2.16-inch AMOLED |
| Resolution | 480 x 480 |
| Color | 16.7M colors, RGB565 in the examples |
| Brightness | 600 cd/m2 maximum |
| Contrast | 100000:1 |
| Display interface | QSPI |
| Display driver | CO5300 |
| Touch controller | CST9220, capacitive |
| Touch interface | I2C |

The official display code uses the `esp_lcd_sh8601` component with a
CO5300-compatible initialization sequence. The official touch code uses
`esp_lcd_touch_cst9217` for the CST9220. These are the driver paths selected by
Waveshare; they do not change the physical part names in the documentation.

The display has no dedicated reset or backlight GPIO:

- Display reset is controlled by cycling AXP2101 ALDO3.
- Brightness is written to the panel through command `0x51`.
- Speaker amplifier power is controlled by AXP2101 ALDO2.

## Other Onboard Hardware

| Device | Part | Interface | Notes |
| --- | --- | --- | --- |
| Power management | AXP2101 | I2C, address `0x34` | Charging, battery, ALDO/DCDC rails, power-off |
| Six-axis IMU | QMI8658 | I2C | 3-axis accelerometer and 3-axis gyroscope |
| Real-time clock | PCF85063 | I2C | Battery-backed through the AXP2101 power path |
| Audio output codec | ES8311 | I2C and I2S | Speaker output path |
| Audio ADC | ES7210 | I2C and I2S | Dual-microphone capture and AEC path |
| Storage | MicroSD socket | SPI | FAT32 in the official example |
| Battery | 3.7 V lithium battery | MX1.25 2-pin | Charge and discharge through AXP2101 |

## Confirmed GPIO Map

The complete XiaoZhi board definition is the most reliable pin source because
it is used to build a working firmware image.

### Display QSPI

| Signal | GPIO |
| --- | --- |
| LCD_CS | 15 |
| LCD_PCLK / SCLK | 0 |
| LCD_D0 | 1 |
| LCD_D1 | 2 |
| LCD_D2 | 3 |
| LCD_D3 | 4 |
| LCD_RST | AXP2101 ALDO3, not GPIO |
| Backlight | Panel command, not GPIO |

### Touch and Shared I2C

| Signal | GPIO |
| --- | --- |
| TP_RST | 11 |
| TP_INT | 5 |
| I2C_SCL | 7 |
| I2C_SDA | 8 |

AXP2101, QMI8658, PCF85063, CST9220, ES8311, and ES7210 share this I2C bus.

### Buttons

| Button | GPIO | Notes |
| --- | --- | --- |
| BOOT | 9 | Used by the official XiaoZhi firmware |
| KEY | 10 | Available custom button; not used by the basic examples |
| PWR | 18 | Shown by the official interface diagram; mainly tied to AXP2101 power control |

Do not configure PWR as a normal output without checking the power-management
behavior first.

### Audio I2S

| Signal | GPIO |
| --- | --- |
| I2S_MCLK | 19 |
| I2S_SCLK / BCLK | 20 |
| I2S_DSIN / DIN | 21 |
| I2S_LRCK / WS | 22 |
| I2S_DOUT | 23 |
| PA_CTRL | AXP2101 ALDO2, not GPIO |

### MicroSD SPI

| Signal | GPIO |
| --- | --- |
| SD_SCK | 0 |
| SD_MOSI | 1 |
| SD_MISO | 2 |
| SD_CS | 6 |

### QMI8658 Interrupts

| Signal | GPIO |
| --- | --- |
| INT1 | 16 |
| INT2 | 17 |

The basic examples poll the IMU instead of using these interrupt pins.

## Important Constraints

1. MicroSD shares GPIO0, GPIO1, and GPIO2 with the display QSPI bus. Display and
   SD access must be coordinated; do not initialize them as unrelated buses and
   assume simultaneous access will work.
2. The board has 16 MB flash and no external PSRAM. A full 480 x 480 RGB565
   framebuffer is about 450 KiB, leaving little room in the 512 KiB HP SRAM for
   duplicate buffers, audio, networking, and application state.
3. The official documentation recommends ESP-IDF 5.5.2. The current Waveshare
   example directory is named `ESP-IDF-v5.5.3`, but its defaults file was
   generated with 5.5.2. Start with the local `esp-idf-v5.5.2` toolchain unless
   a selected project explicitly requires another version.
4. The official Arduino examples require arduino-esp32 3.3.0 or newer. The
   vendor package currently uses arduino-esp32 3.3.3.
5. LVGL 8 and LVGL 9 examples have separate driver dependencies. Keep the LVGL
   major version and adapter libraries consistent within each firmware project.
6. The default display and touch configuration requires QSPI plus a shared
   I2C bus. Preserve the official initialization order and power-rail setup.

## Official Example Inventory

| Example | Framework | Notes |
| --- | --- | --- |
| `01_AXP2101_Test` | Arduino and ESP-IDF | Power, charging, and battery information |
| `02_I2C_QMI8658` | Arduino and ESP-IDF | IMU data |
| `03_I2C_PCF85063` | Arduino and ESP-IDF | RTC time |
| `04_SD_Card` | Arduino and ESP-IDF | MicroSD over SPI |
| `05_WIFI_STA` | Arduino and ESP-IDF | Station mode |
| `06_WIFI_AP` | Arduino and ESP-IDF | Access point mode |
| `07_Audio_Test` | Arduino and ESP-IDF | ES8311, ES7210, microphone, speaker |
| `08_LVGL_V8_Test` | Arduino and ESP-IDF | LVGL 8 |
| `09_LVGL_V9_Test` | Arduino and ESP-IDF | LVGL 9 |
| `10_FactoryProgram` | ESP-IDF | Factory-style multi-app firmware |
| `XiaoZhi-v2.2.5` | ESP-IDF | Complete voice assistant board port |

## Flashing Notes

- Connect through the board's Type-C ESP32-C6 USB interface.
- If flashing cannot enter download mode, power off, hold BOOT, then power on.
- Do not assume the board enters ROM download mode automatically after a bad
  firmware image.
