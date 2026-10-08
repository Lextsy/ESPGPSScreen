// ESP32-CAM + 1.8" 128x160 ST7735S TFT + GY-GPSV3-NEO GPS receiver.
//
// Reads NMEA from the NEO receiver on a RX-only hardware UART and shows the fix
// on the TFT: status, latitude, longitude, satellites, HDOP, altitude, speed and
// UTC date/time. Acquisition progress is shown while there is no fix.

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <TinyGPSPlus.h>

// ---------------------------------------------------------------- pin map
// ESP32-CAM: the camera and the microSD slot share GPIO 2/4/12/13/15, so the
// display uses those pins and the microSD slot must stay empty.
#define TFT_MOSI        2   // TFT SDA
#define TFT_SCLK       12   // TFT SCL
#define TFT_CS          4   // TFT CS
#define TFT_DC         13   // TFT DC
#define TFT_RST        15   // TFT RST  (-1 = hardwired to 3V3)
#define TFT_BLK        26   // TFT BLK  (-1 = hardwired to GND, always on)
#define TFT_BLK_INVERT  1   // 1 = backlight lights when BLK is LOW
#define TFT_ROTATION    1   // 1 = 160x128 landscape
#define GPS_RX         34   // input-only pin; GPS TX -> this pin
#define GPS_TX         -1   // the NEO receiver never needs to be addressed
#define GPS_BAUD     9600   // GY-GPSV3-NEO factory default
#define DEBUG_RAW       0   // 1 = echo raw NMEA on the USB serial port

// ---------------------------------------------------------------- objects
static lgfx::Bus_SPI bus;
static lgfx::Panel_ST7735S panel;
static lgfx::Light_PWM backlight;
static lgfx::LGFX_Device gfx;
static TinyGPSPlus gps;

static const uint16_t COL_BG     = 0x0000;  // black
static const uint16_t COL_BAR    = 0x18E6;  // dark grey header bar
static const uint16_t COL_TEXT   = 0xFFFF;  // white
static const uint16_t COL_LABEL  = 0x7BE1;  // grey
static const uint16_t COL_OK     = 0x07E0;  // green
static const uint16_t COL_WARN   = 0xFBE4;  // amber
static const uint16_t COL_ERR    = 0xF800;  // red
static const uint16_t COL_ACCENT = 0x051D;  // teal

static const int16_t W = 160, H = 128;

static uint32_t lastByteMs = 0;
static uint32_t lastSerialMs = 0;
static uint32_t lastDrawMs = 0;
static char prev[4][21];

// ---------------------------------------------------------------- display
static void initDisplay(void) {
  auto b = bus.config();
  b.pin_sclk   = TFT_SCLK;
  b.pin_mosi   = TFT_MOSI;
  b.pin_miso   = -1;              // write-only bus, MISO not wired
  b.pin_dc     = TFT_DC;
  b.freq_write = 40000000;
  b.spi_mode   = 0;
  b.spi_3wire  = true;            // DC carried on the MOSI line
  bus.config(b);

  auto p = panel.config();
  p.pin_cs        = TFT_CS;
  p.pin_rst       = TFT_RST;      // -1 = RST tied to 3V3
  p.pin_busy      = -1;
  p.memory_width  = 128;          // ST7735S wired for the 128x160 window
  p.memory_height = 160;
  p.panel_width   = 128;
  p.panel_height  = 160;
  p.offset_x      = 1;            // this glass starts at (1,2) in DRAM
  p.offset_y      = 2;
  p.offset_rotation = TFT_ROTATION;
  p.dummy_read_pixel = 9;
  p.dummy_read_bits  = 1;
  p.readable       = false;       // no readback on a write-only bus
  p.invert         = true;        // ST7735S needs display inversion
  p.rgb_order      = false;
  p.dlen_16bit     = false;
  p.bus_shared     = false;       // the SPI bus belongs to the display alone
  panel.config(p);

  panel.setBus(&bus);
  if (TFT_BLK >= 0) {
    auto l = backlight.config();
    l.pin_bl = TFT_BLK;
    l.invert = TFT_BLK_INVERT;
    l.freq   = 1000;
    backlight.config(l);
    panel.setLight(&backlight);
  }

  gfx.setPanel(&panel);
  gfx.init();
  gfx.setBrightness(255);

  gfx.fillScreen(COL_BG);
  gfx.setFont(&fonts::Font0);
  gfx.setTextColor(COL_TEXT, COL_BG);
  gfx.setCursor(4, 4);
  gfx.printf("ST7735S %dx%d", (int)gfx.width(), (int)gfx.height());
}

// ---------------------------------------------------------------- helpers
// Draw a fixed-width field, clearing its own band so text never smears.
static void field(int16_t x, int16_t y, int16_t w, int16_t h, const char* text,
                  const lgfx::IFont* font, uint16_t fg, uint16_t bg) {
  gfx.setFont(font);
  gfx.setTextDatum(top_left);
  gfx.fillRect(x, y, w, h, bg);
  gfx.setTextColor(fg, bg);
  gfx.drawString(text, x, y);
}

static const char* fixText(TinyGPSPlus& g) {
  if (!g.location.isValid()) return "NO FIX";
  switch (g.location.FixQuality()) {
    case TinyGPSLocation::GPS:  return "2D FIX";
    case TinyGPSLocation::DGPS: return "DGPS";
    case TinyGPSLocation::PPS:  return "PPS";
    case TinyGPSLocation::RTK:  return "RTK";
    case TinyGPSLocation::FloatRTK: return "RTK FLOAT";
    default: return "3D FIX";
  }
}

// ---------------------------------------------------------------- screen
static void render(uint32_t now) {
  const bool hasBytes = lastByteMs != 0 && (now - lastByteMs) < 6000;
  const bool hasFix   = hasBytes && gps.location.isValid();
  const uint8_t sats  = gps.satellites.isValid() ? (uint8_t)gps.satellites.value() : 0;

  // --- header bar: status + satellite progress
  const char* status = !hasBytes ? "NO DATA" : (hasFix ? fixText(gps) : "ACQUIRING");
  uint16_t fg = !hasBytes ? COL_ERR : (hasFix ? COL_OK : COL_WARN);
  gfx.fillRect(0, 0, W, 16, COL_BAR);
  gfx.setFont(&fonts::Font0);
  gfx.setTextDatum(top_left);
  gfx.setTextColor(COL_TEXT, COL_BAR);
  gfx.drawString("GPS", 2, 1);
  gfx.setTextDatum(top_right);
  gfx.setTextColor(fg, COL_BAR);
  gfx.drawString(status, W - 2, 1);

  int16_t bar = (sats > 12 ? 12 : sats) * W / 12;   // 12 satellites = full
  gfx.fillRect(0, 16, W, 4, COL_BG);
  gfx.fillRect(0, 16, bar, 4, hasFix ? COL_OK : COL_ACCENT);

  // --- coordinates
  char lat[16], lon[16];
  if (hasFix) {
    snprintf(lat, sizeof(lat), "%+9.6f", gps.location.lat());
    snprintf(lon, sizeof(lon), "%+10.6f", gps.location.lng());
  } else {
    snprintf(lat, sizeof(lat), " --.------");
    snprintf(lon, sizeof(lon), "  --.------");
  }
  field(2, 21, 26, 17, "LAT", &fonts::Font0, COL_LABEL, COL_BG);
  field(28, 21, W - 28, 17, lat, &fonts::DejaVu12, hasFix ? COL_TEXT : COL_LABEL, COL_BG);
  field(2, 39, 26, 17, "LON", &fonts::Font0, COL_LABEL, COL_BG);
  field(28, 39, W - 28, 17, lon, &fonts::DejaVu12, hasFix ? COL_TEXT : COL_LABEL, COL_BG);

  gfx.fillRect(0, 58, W, 1, COL_ACCENT);

  // --- detail rows, 8x16 cells = 20 characters
  char row[4][21];
  snprintf(row[0], 21, "SAT %2u HDOP %4.2f",
           gps.satellites.isValid() ? gps.satellites.value() : 0,
           gps.hdop.isValid() ? gps.hdop.value() / 100.0 : 0.0);

  uint32_t ageS = hasFix ? gps.location.age() / 1000 : 0;
  snprintf(row[1], 21, "ALT%6.1fm AGE %us",
           gps.altitude.isValid() ? gps.altitude.meters() : 0.0,
           ageS > 999 ? 999 : ageS);

  snprintf(row[2], 21, "SPD %5.1f km/h %2u",
           gps.speed.isValid() ? gps.speed.kmph() : 0.0,
           gps.course.isValid() ? (unsigned)(gps.course.deg() / 10) : 0);

  if (gps.time.isValid() && gps.date.isValid()) {
    snprintf(row[3], 21, "%04u-%02u-%02u %02u:%02u:%02u",
             (unsigned)gps.date.year(), (unsigned)gps.date.month(), (unsigned)gps.date.day(),
             (unsigned)gps.time.hour(), (unsigned)gps.time.minute(), (unsigned)gps.time.second());
  } else {
    snprintf(row[3], 21, "UTC --:--:--  ----");
  }

  const int16_t rowY[4] = {60, 76, 92, 108};
  for (int i = 0; i < 4; i++) {
    if (strcmp(prev[i], row[i]) != 0) {
      strcpy(prev[i], row[i]);
      field(0, rowY[i], W, 16, row[i], &fonts::AsciiFont8x16, COL_TEXT, COL_BG);
    }
  }
}

// ---------------------------------------------------------------- main
void setup(void) {
  Serial.begin(115200);
  initDisplay();

  Serial2.setRxBufferSize(1024);
  Serial2.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);

  Serial.println();
  Serial.println("ESP32-CAM GPS/TFT - ST7735S 128x160 + GY-GPSV3-NEO");
  Serial.printf("GPS UART: RX=GPIO%d %d 8N1 (RX only)\n", GPS_RX, GPS_BAUD);
}

void loop(void) {
  uint32_t now = millis();

  while (Serial2.available() > 0) {
    char c = (char)Serial2.read();
    lastByteMs = now;
    gps.encode(c);
#if DEBUG_RAW
    Serial.write(c);
#endif
  }

  // One status line per second on the USB serial port.
  if (now - lastSerialMs >= 1000) {
    lastSerialMs = now;
    if (gps.location.isValid()) {
      Serial.printf("FIX %s sats=%u hdop=%u lat=%+9.6f lon=%+10.6f alt=%5.1f spd=%4.1f utc=%02u:%02u:%02u\n",
                    fixText(gps),
                    (unsigned)gps.satellites.value(), (unsigned)gps.hdop.value(),
                    gps.location.lat(), gps.location.lng(),
                    gps.altitude.meters(), gps.speed.kmph(),
                    (unsigned)gps.time.hour(), (unsigned)gps.time.minute(), (unsigned)gps.time.second());
    } else {
      Serial.printf("NO FIX sats=%u bytes=%s\n",
                    gps.satellites.isValid() ? gps.satellites.value() : 0,
                    lastByteMs == 0 ? "none" : ((now - lastByteMs) < 6000 ? "yes" : "stale"));
    }
  }

  if (now - lastDrawMs >= 250) {
    lastDrawMs = now;
    render(now);
  }

  delay(20);
}
