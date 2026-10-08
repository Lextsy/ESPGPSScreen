# ESP32-CAM GPS readout (ST7735S 1.8" TFT + GY-GPSV3-NEO)

Firmware for an ESP32-CAM that acquires a GPS fix from a GY-GPSV3-NEO receiver
and shows latitude/longitude, fix quality, satellites, HDOP, altitude, speed and
UTC time on a 1.8" 128x160 ST7735S TFT.

## Hardware

| TFT pin | ESP32-CAM GPIO | Notes |
|---|---|---|
| VDD  | 3V3 | 3.3 V, not 5 V |
| GND  | GND | common ground with the GPS module |
| SDA  | GPIO 2  | SPI MOSI (the on-board camera LED flickers, harmless) |
| SCL  | GPIO 12 | SPI clock |
| CS   | GPIO 4  | SPI chip select |
| DC   | GPIO 13 | data/command |
| RST  | GPIO 15 | may be tied to 3V3 instead; then set `TFT_RST -1` |
| BLK  | GPIO 26 | backlight; on most 8-pin modules BLK is active LOW |
| GND  | GND | |

| GPS (GY-GPSV3-NEO) | ESP32-CAM GPIO | Notes |
|---|---|---|
| TX   | GPIO 34 | GPIO 34/35/36 are input-only, ideal for a receive-only link |
| VCC  | 3V3 | module runs at 3.3 V (5 V also works, TX is 3.3 V level) |
| GND  | GND | |
| RX   | — | not wired: the NEO receiver is read-only, nothing is transmitted to it |

Wiring constraints on the ESP32-CAM:

- GPIO 2, 4, 12, 13, 15 are the on-board microSD SPI pins. **Do not insert a
  microSD card** while the display is wired to them.
- The camera is not used; its pins are free.
- GPIO 34 has no internal pull-up. If the GPS module is unpowered, the input
  floats; that only produces noise, not damage.
- Power: display + GPS draw ~80 mA together. Feed the board from a 5 V supply
  on VIN/5V, not from a weak USB port, or the WiFi/brown-out reset loop appears.

## Build and flash

```
pio run -t upload        # build + flash over the board's USB-TTL
pio device monitor       # 115200, one status line per second
```

The ESP32-CAM needs to be put in download mode: pull **GPIO 0 low** (IO0 → GND),
then press the board's reset button. Flashing starts after `Connecting .....`
appears. Return IO0 to floating and reset to run.

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

Colours: red `NO DATA` = nothing arriving on GPIO 34 (wiring/baud problem),
amber `ACQUIRING` = NMEA arriving, no fix yet, green = fix.

## Configuration

All pins and options are `#define`s at the top of `src/main.cpp`:

| Define | Default | Meaning |
|---|---|---|
| `TFT_MOSI/SCLK/CS/DC` | 2/12/4/13 | SPI pins |
| `TFT_RST` | 15 | `-1` if RST is tied to 3V3 |
| `TFT_BLK` | 26 | `-1` if BLK is tied to GND (always on) |
| `TFT_BLK_INVERT` | 1 | set `0` if the screen stays dark with BLK on GPIO 26 |
| `TFT_ROTATION` | 1 | 0..3 |
| `GPS_RX` | 34 | any input-capable free GPIO |
| `GPS_TX` | -1 | transmit pin, unused: the receiver is read-only |
| `GPS_BAUD` | 9600 | NEO factory default |
| `DEBUG_RAW` | 0 | `1` echoes raw NMEA on the USB serial port |

If the image is offset by a pixel or two, adjust `offset_x` / `offset_y` in
`initDisplay()` (defaults 1 / 2 for the 128x160 ST7735S).

## Bring-up checklist

1. Screen dark → check VDD/GND, then try `TFT_BLK_INVERT 0`, or wire BLK to GND
   and set `TFT_BLK -1`.
2. Garbage/shifted image → lower `b.freq_write` in `initDisplay()` to 20 MHz and
   keep the wires short.
3. `NO DATA` on screen → set `DEBUG_RAW 1`; with the GPS wired you should see
   `$GNGGA,...` lines. No lines = GPS TX not on GPIO 34, module unpowered, or the
   module was reconfigured to a non-9600 baud rate.
4. `ACQUIRING` forever → the antenna needs sky view; a first fix indoors takes
   minutes, outdoors 20–60 s.
5. Coordinates look wrong by whole degrees → the module is outputting a
   non-standard sentence set; check the raw NMEA with `DEBUG_RAW 1`.

Notes on the parsed values: the date comes from the RMC 2-digit year, which
TinyGPSPlus maps to `2000 + yy`; speed, HDOP and altitude are stored with two
decimals (0.046 kn is kept as 0.04 kn). Latitude/longitude keep full NMEA
precision (about 0.1 m).
