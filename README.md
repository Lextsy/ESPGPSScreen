# ESP32-CAM GPS readout (ST7735S 1.8" TFT + GY-GPSV3-NEO)

Firmware for an ESP32-CAM that acquires a GPS fix from a GY-GPSV3-NEO receiver
and shows latitude/longitude, fix quality, satellites, HDOP, altitude, speed and
UTC time on a 1.8" 128x160 ST7735S TFT.

## Hardware

The board is the AI-Thinker ESP32-CAM. It exposes **16 header pins**: 14 on the
long edge, 2 (`5V`, `GND`) on the short edge. Ten of them are GPIO.

| Header pin | GPIO | Notes |
|---|---|---|
| `5V` | – | 5 V supply in; the recommended supply point |
| `3V3` | – | 3.3 V in / to peripherals |
| `VCC` | – | 3.3 V **output** from the on-board regulator; never feed power into it |
| `GND` ×3 | – | use the `GND` next to `3V3` as the ground rail |
| `IO0` | 0 | boot strap + the board's IO0 button; also the camera XCLK line |
| `IO1` | 1 | `U0T` – USB-TTL TX, flashing and console |
| `IO3` | 3 | `U0R` – USB-TTL RX, flashing and console |
| `IO2` | 2 | microSD DATA0, strapping pin, shares the camera LED |
| `IO4` | 4 | microSD DATA1, drives the white flash LED |
| `IO12` | 12 | microSD DATA2; `MTDI` strap (flash voltage) |
| `IO13` | 13 | microSD DATA3; `MTCK` |
| `IO14` | 14 | microSD CLK; `MTMS` |
| `IO15` | 15 | microSD CMD; `MTDO` strap (boot log) |
| `IO16` | 16 | PSRAM `CS#` – the module has 4 MB PSRAM and the build enables it |

**There is no GPIO 26, 24, 34, 35, 32, 33 on the header.** Those numbers are
inside the module: GPIO 26/27 are the OV2640's I²C pair, GPIO 34/35/36/39 are
camera data lines, GPIO 32 is the camera power-down, GPIO 33 is the on-board red
LED, and GPIO 24 is consumed by the in-package flash/PSRAM of the ESP32-S.
Wiring guides that print `IO32`–`IO35` on the ESP32-CAM edge header are showing
camera-side nets, not pins you can reach. GPIO 26 as a backlight and GPIO 34 as a
GPS input are not wireable on this board.

The six GPIO left for a project are **GPIO 2, 4, 12, 13, 14, 15** — the microSD
SPI lines, free while the slot stays empty. This build uses all six: five for the
display, one for the GPS, and GPIO 4 is left over and held LOW.

GPIO 4 is also the board's **white flash LED**, and it lights whenever GPIO 4 is
HIGH. SPI chip select idles HIGH, so a CS line on GPIO 4 leaves that LED blazing
all the time. CS therefore sits on `IO15` and the display's RST goes to `3V3`;
the firmware drives GPIO 4 LOW at startup and the LED stays dark.

| TFT pin | ESP32-CAM | Notes |
|---|---|---|
| VDD  | `3V3` | 3.3 V, not 5 V |
| GND  | `GND` | common ground with the GPS module |
| SDA  | `IO2`  | SPI MOSI (the on-board camera LED flickers, harmless) |
| SCL  | `IO12` | SPI clock |
| CS   | `IO15` | SPI chip select; `MTDO` wants HIGH at boot, which is idle CS |
| DC   | `IO13` | data/command |
| RST  | `3V3`  | hardwired high; `TFT_RST -1`, frees `IO15` for CS |
| BLK  | `GND`  | active LOW; tying it to ground leaves the backlight always on |
| VCC  | – | not present on the 8-pin ST7735S breakout |

| GPS (GY-GPSV3-NEO) | ESP32-CAM | Notes |
|---|---|---|
| TX   | `IO14` | the only free GPIO; UART2 maps its RX from any pin |
| VCC  | `3V3` | module runs at 3.3 V (5 V also works, TX is a 3.3 V level) |
| GND  | `GND` | |
| RX   | – | not wired: the NEO receiver is read-only, nothing is transmitted to it |

Wiring constraints on this board:

- GPIO 2, 4, 12, 13, 14, 15 are the microSD SPI pins. **Do not insert a microSD
  card** while the display and GPS are wired to them.
- The camera is not used, and its pins are not on the header at all.
- Strapping pins: `IO12` must be LOW and `IO15` must be HIGH while the chip comes
  out of reset. Both are satisfied by this wiring: the SPI clock is driven by the
  ESP32 and idles low, and CS idles high. Nothing external may pull them the
  other way, so the display's CS line must not be pulled down.
- `IO4` is the white flash LED: HIGH lights it. The firmware drives it LOW at
  startup. Do not wire anything to `IO4` that drives the line.
- `IO16` is the PSRAM chip select in this build: never wire it.
- `IO1`/`IO3` stay on the USB-TTL for flashing and the status log.
- `IO0` stays on the boot button.
- Power: display + GPS draw ~80 mA together. Feed the board from a 5 V supply on
  the `5V` pin, not from a weak USB port, or the WiFi/brown-out reset loop appears.

## Board variants

- **AI-Thinker ESP32-CAM (this project's board, V1.6):** the 16-pin header above.
  GPIO set `0 1 2 3 4 12 13 14 15 16`.
- **ESP32-CAM-MB demo base board:** the same module on a carrier that adds a
  micro-USB, an auto-program (DTR/RTS) circuit and a 3-pin `GND/5V/IO0` header. It
  adds no GPIO.
- **Clones.** Some ESP32-CAM clones print `IO32`–`IO35` on the long edge. If the
  silkscreen on the board in hand really says that, those are input-only pins and
  `IO34` can carry the GPS TX instead of `IO14`; confirm against the silkscreen
  with a multimeter before trusting a blog diagram. The pin set in the table above
  is the AI-Thinker datasheet set and is what the stock boards have.

## Build and flash

```
pio run -t upload        # build + flash over the USB-TTL adapter on COM5
pio device monitor       # 115200, one status line per second
```

`IO0` is the boot strap and the board's IO0 button. Download mode = hold IO0,
press reset, release. A USB-TTL adapter that asserts DTR/RTS (FT232R, CP2102 with
the auto-program wiring) enters download mode by itself, so no button handling is
needed; this board flashes over COM5 that way.

## Display

```
 GPS            3D FIX      <- status: NO DATA / ACQUIRING / 2D FIX / DGPS / RTK
 ████████░░░░░░              <- satellite bar, 12 satellites = full
 LAT  51.506385
 LON  -0.123456
 SAT 09 HDOP 1.10
 ALT 112.4m AGE 3s
 SPD   0.0 km/h  0
 2026-10-07 12:34:56
```

Colours: red `NO DATA` = nothing arriving on GPIO 14 (wiring/baud problem),
amber `ACQUIRING` = NMEA arriving, no fix yet, green = fix.

## Configuration

All pins and options are `#define`s at the top of `src/main.cpp`:

| Define | Default | Meaning |
|---|---|---|
| `TFT_MOSI/SCLK/CS/DC` | 2/12/15/13 | SPI pins |
| `TFT_RST` | -1 | RST is tied to 3V3; `15` only if CS moves off GPIO 15 |
| `TFT_BLK` | -1 | BLK is tied to GND (always on); no spare GPIO for it |
| `TFT_BLK_INVERT` | 1 | only read when `TFT_BLK >= 0` |
| `TFT_ROTATION` | 1 | 0..3 |
| `LED_FLASH` | 4 | the board's white flash LED; driven LOW, never HIGH |
| `GPS_RX` | 14 | the last free header GPIO |
| `GPS_TX` | -1 | transmit pin, unused: the receiver is read-only |
| `GPS_BAUD` | 9600 | NEO factory default |
| `DEBUG_RAW` | 0 | `1` echoes raw NMEA on the USB serial port |

If the image is offset by a pixel or two, adjust `offset_x` / `offset_y` in
`initDisplay()` (defaults 1 / 2 for the 128x160 ST7735S).

Backlight control needs a spare GPIO and this board has none: `IO16` is the PSRAM
chip select in this build (the ESP-IDF SDK in this core is built with
`CONFIG_SPIRAM=y`, so GPIO 16 is claimed at startup), `IO0` is the boot button,
`IO1` is the console TX and `IO14` is the GPS. The one workable mapping is
`TFT_BLK 3`: the display's BLK pin is an input, so it never fights the USB-TTL
driver on `U0R` while flashing, and after boot GPIO 3 drives the backlight (keep
`TFT_BLK_INVERT 1`, BLK is active LOW). Expect the backlight to flicker while the
board sits in download mode, where GPIO 3 is an input.

## Bring-up checklist

1. Screen dark → check VDD/GND. With BLK tied to GND the backlight is always on,
   so a dark screen is a power, wiring or init problem, not a backlight problem.
   If you moved BLK onto a GPIO, try `TFT_BLK_INVERT 0`.
2. Garbage/shifted image → lower `b.freq_write` in `initDisplay()` to 20 MHz and
   keep the wires short.
3. `NO DATA` on screen → set `DEBUG_RAW 1`; with the GPS wired you should see
   `$GNGGA,...` lines. No lines = GPS TX not on GPIO 14, module unpowered, or the
   module was reconfigured to a non-9600 baud rate.
4. `ACQUIRING` forever → the antenna needs sky view; a first fix indoors takes
   minutes, outdoors 20–60 s.
5. Coordinates look wrong by whole degrees → the module is outputting a
   non-standard sentence set; check the raw NMEA with `DEBUG_RAW 1`.
6. White LED glowing → GPIO 4 is HIGH. In this build GPIO 4 is not the chip
   select and the firmware drives it LOW, so a lit LED means CS is wired to `IO4`
   while `TFT_CS` points at `IO15`, or `TFT_CS` was set back to 4.

Notes on the parsed values: the date comes from the RMC 2-digit year, which
TinyGPSPlus maps to `2000 + yy`; speed, HDOP and altitude are stored with two
decimals (0.046 kn is kept as 0.04 kn). Latitude/longitude keep full NMEA
precision (about 0.1 m).
