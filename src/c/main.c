// Pebble SDK header — provides all Pebble API functions and types
#include <pebble.h>

// Persistent storage key for saving/loading settings from flash
#define SETTINGS_KEY 1
// Version identifier to detect stale settings and trigger reset
#define SETTINGS_VERSION 3
// Percentage of screen height allocated to the bottom quadrant area (rectangular only)
#define QUAD_AREA_PCT 45

// Holds the current display data for one of the four corner quadrants
typedef struct {
  uint8_t quad_id;     // 0=off, 1=battery, 2=steps, 3=heart rate, 4=rain %, 5=temp, 6=weather icon
  char label[10];      // Short label displayed above the value
  char value[20];      // Formatted value string (e.g. "72%", "85 bpm")
  uint8_t icon_char;   // Simple icon marker character (reserved for future use)
  bool valid;          // Whether this quadrant has valid data to show
} QuadrantData;

// ---- Window & Layers ----
static Window *s_window;           // Main full-screen window
static Layer *s_bt_layer;          // Bluetooth disconnect banner (top strip)
#ifdef PBL_ROUND
static Layer *s_main_layer;        // Single layer for round displays — everything drawn here
#else
static Layer *s_top_layer;         // Upper half — time & date (rectangular)
static Layer *s_bot_layer;         // Lower half — four quadrant info tiles (rectangular)
#endif

// ---- State flags ----
static bool s_bluetooth_connected = true;

// ---- Display buffers ----
static char s_time_text[12];       // Formatted time string (e.g. "9:41" or "09:41")
static char s_date_text[40];       // Formatted date string (e.g. "Mon, Jan 01")

// ---- Dynamic data ----
static QuadrantData s_quads[4];    // Four corner quadrants (TL, TR, BL, BR)
static int16_t s_batt_pct = 100;   // Battery charge percentage
static int16_t s_steps = 0;        // Step count from Pebble Health
static int16_t s_hr = 0;           // Average heart rate (BPM)
static bool s_hr_available = false; // Whether heart rate sensor has returned data

// AppMessage dictionary keys — used for communication between Pebble C and JavaScript companion
enum {
  KEY_REQUEST_WEATHER  = 0,  // Flag to request a weather fetch (JS-side)
  KEY_TEMP             = 1,  // Current temperature (tenths of a degree)
  KEY_TEMP_HI          = 2,  // Today's high temperature
  KEY_TEMP_LO          = 3,  // Today's low temperature
  KEY_CONDITION        = 4,  // WMO weather condition code
  KEY_PRECIP_PROB      = 5,  // Precipitation probability percentage
  KEY_SUNRISE          = 6,  // Sunrise time as minutes since midnight
  KEY_SUNSET           = 7,  // Sunset time as minutes since midnight
  KEY_LOCATION_NAME    = 8,  // Human-readable location name
  KEY_ERROR            = 9,  // Error message string
  KEY_USE_24H          = 10, // 24-hour clock toggle
  KEY_DATE_FORMAT      = 11, // strftime-style date format string
  KEY_TEMP_UNIT        = 12, // 0=Celsius, 1=Fahrenheit, 2=Kelvin
  KEY_QUAD_TL          = 13, // Top-left quadrant type
  KEY_QUAD_TR          = 14, // Top-right quadrant type
  KEY_QUAD_BL          = 15, // Bottom-left quadrant type
  KEY_QUAD_BR          = 16, // Bottom-right quadrant type
  KEY_FG_DAY           = 17, // Day foreground color (GColor8)
  KEY_BG_DAY           = 18, // Day background color (GColor8)
  KEY_USE_DAY_NIGHT    = 19, // Enable auto day/night color switching
  KEY_FG_NIGHT         = 20, // Night foreground color
  KEY_BG_NIGHT         = 21, // Night background color
  KEY_USE_GPS          = 22, // Use GPS vs. manual location
  KEY_LATITUDE         = 23, // Latitude * 10000 (int32)
  KEY_LONGITUDE        = 24, // Longitude * 10000 (int32)
  KEY_LOCATION_STR     = 25, // Manual location name string
  KEY_SETTINGS_BLOB    = 26, // Full packed settings struct
  KEY_REQUEST_SETTINGS = 27, // Flag requesting settings sent from JS
  KEY_REQUEST_RESEND   = 28, // Flag to re-request settings from JS
};

// Packed binary settings struct — written directly to persistent storage
// and also sent as a blob via AppMessage to the JS side
typedef struct __attribute__((__packed__)) {
  uint8_t version;                // Settings version for migration detection
  bool use_24h;                   // 12h vs 24h time display
  char date_format[20];           // strftime-compatible date format pattern
  uint8_t temp_unit;              // 0=Celsius, 1=Fahrenheit, 2=Kelvin
  uint8_t weather_interval;       // Minutes between weather refreshes
  uint8_t quad_tl;                // Top-left quadrant mode
  uint8_t quad_tr;                // Top-right quadrant mode
  uint8_t quad_bl;                // Bottom-left quadrant mode
  uint8_t quad_br;                // Bottom-right quadrant mode
  GColor8 fg_day;                 // Daytime foreground (text/icons) color
  GColor8 bg_day;                 // Daytime background color
  bool use_day_night;             // Enable automatic day/night palette switching
  GColor8 fg_night;               // Nighttime foreground color
  GColor8 bg_night;               // Nighttime background color
  bool use_gps;                   // True=GPS location, False=manual city name
  int32_t latitude;               // Latitude * 10000 (for weather API)
  int32_t longitude;              // Longitude * 10000
  char location_name[40];         // Manual city/location string
} Settings;

// Weather data received from the JavaScript companion (via Open-Meteo API)
typedef struct {
  int16_t temp;           // Current temperature (tenths °C)
  int16_t temp_hi;        // Today's forecast high (tenths °C)
  int16_t temp_lo;        // Today's forecast low (tenths °C)
  uint16_t condition;     // WMO weather code (0=clear, 1-2=partly cloudy, etc.)
  uint8_t precip_prob;    // Precipitation probability (%)
  uint16_t sunrise_min;   // Sunrise time as minutes past midnight
  uint16_t sunset_min;    // Sunset time as minutes past midnight
  char location[40];      // Location name from geocoding or timezone
  bool valid;             // True if weather data has been received at least once
} WeatherData;

// ---- Application-wide globals ----
static Settings s_settings;             // Current user settings (persisted to flash)
static WeatherData s_weather;           // Most recent weather data
static bool s_is_night = false;         // Whether the watch thinks it's currently nighttime
static time_t s_last_weather_fetch = 0; // Timestamp of last successful weather fetch

// Populates the settings struct with sensible defaults for first-time use
static void set_default_settings(void) {
  s_settings.version = SETTINGS_VERSION;
  s_settings.use_24h = false;                   // 12-hour clock by default
  strcpy(s_settings.date_format, "%a, %b %d"); // e.g. "Mon, Jan 01"
  s_settings.temp_unit = 0;                     // Celsius
  s_settings.weather_interval = 60;             // Fetch weather every 60 minutes
  s_settings.quad_tl = 1;                       // Battery (top-left)
  s_settings.quad_tr = 4;                       // Rain % (top-right)
  s_settings.quad_bl = 2;                       // Steps (bottom-left)
  s_settings.quad_br = 3;                       // Heart rate (bottom-right)
  s_settings.fg_day = GColorWhite;              // White text on
  s_settings.bg_day = GColorBlack;              // Black background
  s_settings.use_day_night = false;             // No auto day/night switching
  s_settings.fg_night = GColorBlack;            // (Inverted for night)
  s_settings.bg_night = GColorWhite;
  s_settings.use_gps = true;                    // Prefer GPS location
  s_settings.latitude = 0;
  s_settings.longitude = 0;
  s_settings.location_name[0] = 0;
  s_weather.valid = false;
}

// Returns the effective foreground color based on day/night mode
static GColor8 get_fg_color(void) {
  if (s_settings.use_day_night && s_is_night) return s_settings.fg_night;
  return s_settings.fg_day;
}

// Returns the effective background color based on day/night mode
static GColor8 get_bg_color(void) {
  if (s_settings.use_day_night && s_is_night) return s_settings.bg_night;
  return s_settings.bg_day;
}

// Tracks whether settings have changed since last save
static bool s_settings_dirty = false;

// Persists the current settings to flash storage via Pebble's persist API
static void save_settings(void) {
  if (persist_write_data(SETTINGS_KEY, &s_settings, sizeof(Settings)) == 0) {
    s_settings_dirty = false;
  }
}

// Flags settings as needing a save (saved later in inbox_received or deinit)
static void mark_settings_dirty(void) {
  s_settings_dirty = true;
}

// Loads settings from persistent storage, falling back to defaults if missing or version-mismatched
static void load_settings(void) {
  if (persist_exists(SETTINGS_KEY)) {
    persist_read_data(SETTINGS_KEY, &s_settings, sizeof(Settings));
    if (s_settings.version != SETTINGS_VERSION) {
      set_default_settings();
      mark_settings_dirty();
      save_settings();
    }
  } else {
    set_default_settings();
    mark_settings_dirty();
    save_settings();
  }
}


// Sends the full settings blob to the JavaScript companion.
// If include_weather is true, also requests a fresh weather fetch.
static void send_settings_with_blob(bool include_weather) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, KEY_REQUEST_SETTINGS, 1);
  dict_write_data(iter, KEY_SETTINGS_BLOB, (const uint8_t *)&s_settings, sizeof(Settings));
  if (include_weather) {
    dict_write_uint8(iter, KEY_REQUEST_WEATHER, 1);
  }
  app_message_outbox_send();
}

// Determines whether the current time is before sunrise or after sunset (night mode)
static void check_update_night(void) {
  if (!s_settings.use_day_night || !s_weather.valid) {
    s_is_night = false;
    return;
  }
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  uint16_t now_min = t->tm_hour * 60 + t->tm_min;
  s_is_night = (now_min < s_weather.sunrise_min || now_min >= s_weather.sunset_min);
}

// Converts a temperature from tenths-of-Celsius to the user's preferred scale.
// Returns value in tenths of the target unit (e.g., 237 = 23.7°C).
static int16_t convert_temp(int16_t temp_tenths_c) {
  switch (s_settings.temp_unit) {
    case 1: return (temp_tenths_c * 9 / 5 + 320);   // Celsius -> Fahrenheit (*10)
    case 2: return (temp_tenths_c + 2731);            // Celsius -> Kelvin (*10)
    default: return temp_tenths_c;                     // Stay in Celsius (*10)
  }
}

// Formats a temperature with unit symbol into a string buffer (e.g. "23°C")
static void format_temp(int16_t temp_tenths, char *buf, size_t len) {
  int16_t t = convert_temp(temp_tenths);
  char unit = 'C';
  if (s_settings.temp_unit == 1) unit = 'F';
  else if (s_settings.temp_unit == 2) unit = 'K';
  snprintf(buf, len, "%d°%c", t / 10, unit);
}

// Draws a simple pixel-art weather icon based on WMO weather code.
// x,y = top-left corner, s = bounding square size, night = use moon variant for clear skies.
static void draw_weather_icon(GContext *ctx, int x, int y, int s, uint16_t code, bool night) {
  graphics_context_set_fill_color(ctx, get_fg_color());
  graphics_context_set_stroke_color(ctx, get_fg_color());

  // WMO code 0 = clear sky — draw sun or crescent moon
  if (code == 0) {
    if (night) {
      // Crescent moon: large circle with a smaller background-colored circle to create the crescent cutout
      graphics_fill_circle(ctx, GPoint(x + s/2, y + s/2), s/3);
      graphics_context_set_fill_color(ctx, get_bg_color());
      graphics_fill_circle(ctx, GPoint(x + s/2 + s/6, y + s/2 - s/6), s/4);
      graphics_context_set_fill_color(ctx, get_fg_color());
    } else {
      // Sun: filled circle with 8 radial rays
      int r = s/3;
      graphics_fill_circle(ctx, GPoint(x + s/2, y + s/2), r);
      for (int i = 0; i < 8; i++) {
        int a = i * 45;
        int lx = x + s/2 + (int)((float)(r + 2) * cos_lookup(a * TRIG_MAX_ANGLE / 360) / TRIG_MAX_RATIO);
        int ly = y + s/2 + (int)((float)(r + 2) * sin_lookup(a * TRIG_MAX_ANGLE / 360) / TRIG_MAX_RATIO);
        int lx2 = x + s/2 + (int)((float)(r + 6) * cos_lookup(a * TRIG_MAX_ANGLE / 360) / TRIG_MAX_RATIO);
        int ly2 = y + s/2 + (int)((float)(r + 6) * sin_lookup(a * TRIG_MAX_ANGLE / 360) / TRIG_MAX_RATIO);
        graphics_draw_line(ctx, GPoint(lx, ly), GPoint(lx2, ly2));
      }
    }
    return;
  }

  // Cloud base (shared among cloudy/rain/snow/thunder icons)
  int cx = x + s/2;
  int cy = y + s/2 + 2;
  graphics_fill_circle(ctx, GPoint(cx - 4, cy), 6);
  graphics_fill_circle(ctx, GPoint(cx + 4, cy + 1), 5);
  graphics_fill_circle(ctx, GPoint(cx, cy - 3), 5);
  graphics_fill_rect(ctx, GRect(cx - 8, cy - 2, 16, 4), 0, GCornerNone);

  // WMO codes 1-2 = partly cloudy — small sun peeking behind cloud, with 6 rays
  if (code >= 1 && code <= 2) {
    graphics_context_set_fill_color(ctx, get_bg_color());
    graphics_fill_circle(ctx, GPoint(x + s/2 + 4, y + s/2 - 6), 5);
    graphics_context_set_fill_color(ctx, get_fg_color());
    int r = 4;
    graphics_fill_circle(ctx, GPoint(x + s/2 + 2, y + s/2 - 8), r);
    for (int i = 0; i < 6; i++) {
      int a = i * 60;
      int lx = x + s/2 + 2 + (int)((float)(r + 1) * cos_lookup(a * TRIG_MAX_ANGLE / 360) / TRIG_MAX_RATIO);
      int ly = y + s/2 - 8 + (int)((float)(r + 1) * sin_lookup(a * TRIG_MAX_ANGLE / 360) / TRIG_MAX_RATIO);
      int lx2 = x + s/2 + 2 + (int)((float)(r + 3) * cos_lookup(a * TRIG_MAX_ANGLE / 360) / TRIG_MAX_RATIO);
      int ly2 = y + s/2 - 8 + (int)((float)(r + 3) * sin_lookup(a * TRIG_MAX_ANGLE / 360) / TRIG_MAX_RATIO);
      graphics_context_set_stroke_color(ctx, get_fg_color());
      graphics_draw_line(ctx, GPoint(lx, ly), GPoint(lx2, ly2));
    }
    graphics_context_set_fill_color(ctx, get_fg_color());
  }

  graphics_context_set_fill_color(ctx, get_fg_color());

  // WMO 61-82 = rain showers — three vertical dashed lines below cloud
  if (code >= 61 && code <= 82) {
    graphics_draw_line(ctx, GPoint(cx - 4, cy + 5), GPoint(cx - 4, cy + 10));
    graphics_draw_line(ctx, GPoint(cx + 0, cy + 5), GPoint(cx + 0, cy + 10));
    graphics_draw_line(ctx, GPoint(cx + 4, cy + 5), GPoint(cx + 4, cy + 10));
  // WMO 71-77 = snow — asterisk/cross pattern below cloud
  } else if (code >= 71 && code <= 77) {
    graphics_draw_line(ctx, GPoint(cx - 5, cy + 3), GPoint(cx + 2, cy + 10));
    graphics_draw_line(ctx, GPoint(cx + 2, cy + 3), GPoint(cx - 5, cy + 10));
    graphics_draw_line(ctx, GPoint(cx - 2, cy + 3), GPoint(cx + 4, cy + 8));
    graphics_draw_line(ctx, GPoint(cx + 4, cy + 3), GPoint(cx - 2, cy + 8));
  // WMO 95-99 = thunderstorm — lightning bolt shape below cloud
  } else if (code >= 95 && code <= 99) {
    GPathInfo lightning = {
      .num_points = 4,
      .points = (GPoint[]) {
        {cx - 2, cy + 2},
        {cx + 1, cy + 2},
        {cx + 0, cy + 8},
        {cx - 3, cy + 8}
      }
    };
    GPath *path = gpath_create(&lightning);
    gpath_draw_filled(ctx, path);
    gpath_destroy(path);
  // WMO 45-48 = fog — three horizontal lines across the icon area
  } else if (code >= 45 && code <= 48) {
    for (int i = 0; i < 3; i++) {
      graphics_draw_line(ctx, GPoint(cx - 6, cy + 2 + i * 3), GPoint(cx + 6, cy + 2 + i * 3));
    }
  }
}

// Draws one quadrant tile at position (x,y) with size (w,h).
// type: 0=off, 1=battery, 2=steps, 3=heart rate, 4=rain %, 5=temperature, 6=weather icon+temp
static void draw_quadrant(GContext *ctx, int x, int y, int w, int h, uint8_t type, int index) {
  if (type == 0) return;
  graphics_context_set_fill_color(ctx, get_fg_color());

  switch (type) {
    // Battery indicator: small battery icon outline filled proportionally, low battery turns red
    case 1: {
      int bw = 18, bh = 10;
      int bx = x + (w - bw) / 2 - 2;
      int by = y + 2;
      graphics_draw_rect(ctx, GRect(bx, by, bw, bh));
      graphics_fill_rect(ctx, GRect(bx + bw, by + 2, 2, bh - 4), 0, GCornerNone);
      int fill = s_batt_pct * (bw - 2) / 100;
      if (s_batt_pct > 20) {
        graphics_fill_rect(ctx, GRect(bx + 1, by + 1, fill, bh - 2), 0, GCornerNone);
      } else {
        graphics_context_set_fill_color(ctx, GColorRed);
        graphics_fill_rect(ctx, GRect(bx + 1, by + 1, fill, bh - 2), 0, GCornerNone);
        graphics_context_set_fill_color(ctx, get_fg_color());
      }
      snprintf(s_quads[index].value, sizeof(s_quads[index].value), "%d%%", s_batt_pct);
      graphics_draw_text(ctx, s_quads[index].value,
        fonts_get_system_font(FONT_KEY_GOTHIC_14),
        GRect(x, by + bh + 1, w, 14), GTextOverflowModeTrailingEllipsis,
        GTextAlignmentCenter, NULL);
      break;
    }
    // Step counter: bold step number with "steps" label below
    case 2: {
      snprintf(s_quads[index].value, sizeof(s_quads[index].value), "%d", s_steps);
      graphics_draw_text(ctx, s_quads[index].value,
        fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
        GRect(x, y, w, 20), GTextOverflowModeTrailingEllipsis,
        GTextAlignmentCenter, NULL);
      graphics_draw_text(ctx, "steps",
        fonts_get_system_font(FONT_KEY_GOTHIC_14),
        GRect(x, y + 20, w, 14), GTextOverflowModeTrailingEllipsis,
        GTextAlignmentCenter, NULL);
      break;
    }
    // Heart rate: BPM number with "bpm" label, or "--" with "HR" if unavailable
    case 3: {
      if (s_hr_available) {
        snprintf(s_quads[index].value, sizeof(s_quads[index].value), "%d", s_hr);
        graphics_draw_text(ctx, s_quads[index].value,
          fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
          GRect(x, y, w, 20), GTextOverflowModeTrailingEllipsis,
          GTextAlignmentCenter, NULL);
        graphics_draw_text(ctx, "bpm",
          fonts_get_system_font(FONT_KEY_GOTHIC_14),
          GRect(x, y + 20, w, 14), GTextOverflowModeTrailingEllipsis,
          GTextAlignmentCenter, NULL);
      } else {
        graphics_draw_text(ctx, "--",
          fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
          GRect(x, y, w, 20), GTextOverflowModeTrailingEllipsis,
          GTextAlignmentCenter, NULL);
        graphics_draw_text(ctx, "HR",
          fonts_get_system_font(FONT_KEY_GOTHIC_14),
          GRect(x, y + 20, w, 14), GTextOverflowModeTrailingEllipsis,
          GTextAlignmentCenter, NULL);
      }
      break;
    }
    // Rain probability: simple raindrop icon with percentage
    case 4: {
      if (s_weather.valid) {
        int cx = x + w/2, cy = y + 6;
        graphics_fill_circle(ctx, GPoint(cx, cy), 3);
        graphics_draw_line(ctx, GPoint(cx, cy + 2), GPoint(cx, cy + 8));
        graphics_draw_line(ctx, GPoint(cx - 1, cy + 5), GPoint(cx + 1, cy + 5));
        snprintf(s_quads[index].value, sizeof(s_quads[index].value), "%u%%", s_weather.precip_prob);
        graphics_draw_text(ctx, s_quads[index].value,
          fonts_get_system_font(FONT_KEY_GOTHIC_14),
          GRect(x, y + 16, w, 14), GTextOverflowModeTrailingEllipsis,
          GTextAlignmentCenter, NULL);
      } else {
        graphics_draw_text(ctx, "--",
          fonts_get_system_font(FONT_KEY_GOTHIC_14),
          GRect(x + w/2 - 10, y + 8, 20, 14), GTextOverflowModeTrailingEllipsis,
          GTextAlignmentCenter, NULL);
      }
      break;
    }
    // Temperature: bold current temp with "temp" label
    case 5: {
      if (s_weather.valid) {
        char tmp[12];
        format_temp(s_weather.temp, tmp, sizeof(tmp));
        snprintf(s_quads[index].value, sizeof(s_quads[index].value), "%s", tmp);
        graphics_draw_text(ctx, s_quads[index].value,
          fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
          GRect(x, y, w, 20), GTextOverflowModeTrailingEllipsis,
          GTextAlignmentCenter, NULL);
        graphics_draw_text(ctx, "temp",
          fonts_get_system_font(FONT_KEY_GOTHIC_14),
          GRect(x, y + 20, w, 14), GTextOverflowModeTrailingEllipsis,
          GTextAlignmentCenter, NULL);
      } else {
        graphics_draw_text(ctx, "--",
          fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
          GRect(x, y, w, 20), GTextOverflowModeTrailingEllipsis,
          GTextAlignmentCenter, NULL);
        graphics_draw_text(ctx, "temp",
          fonts_get_system_font(FONT_KEY_GOTHIC_14),
          GRect(x, y + 20, w, 14), GTextOverflowModeTrailingEllipsis,
          GTextAlignmentCenter, NULL);
      }
      break;
    }
    // Weather combo: small weather icon (drawn via draw_weather_icon) + temperature below
    case 6: {
      if (s_weather.valid) {
        draw_weather_icon(ctx, x + w/2 - 10, y + 1, 14, s_weather.condition, s_is_night);
        char tmp[12];
        format_temp(s_weather.temp, tmp, sizeof(tmp));
        snprintf(s_quads[index].value, sizeof(s_quads[index].value), "%s", tmp);
        graphics_draw_text(ctx, s_quads[index].value,
          fonts_get_system_font(FONT_KEY_GOTHIC_14),
          GRect(x, y + 20, w, 14), GTextOverflowModeTrailingEllipsis,
          GTextAlignmentCenter, NULL);
      } else {
        graphics_draw_text(ctx, "--",
          fonts_get_system_font(FONT_KEY_GOTHIC_14),
          GRect(x + w/2 - 10, y + 8, 20, 14), GTextOverflowModeTrailingEllipsis,
          GTextAlignmentCenter, NULL);
      }
      break;
    }
  }
}

// Draws the current time in large Bitham 42 Bold font.
// In 12h mode, strips leading zero and appends AM/PM in the top-right corner.
static void draw_time_text(GContext *ctx, int w, int y) {
  GFont time_font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  if (s_settings.use_24h) {
    strftime(s_time_text, sizeof(s_time_text), "%H:%M", localtime(&(time_t){time(NULL)}));
  } else {
    strftime(s_time_text, sizeof(s_time_text), "%I:%M", localtime(&(time_t){time(NULL)}));
    if (s_time_text[0] == '0') memmove(s_time_text, s_time_text+1, strlen(s_time_text));
  }
  GRect time_box = GRect(0, y, w, 48);
  graphics_draw_text(ctx, s_time_text, time_font, time_box,
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  if (!s_settings.use_24h) {
    char ampm[4];
    strftime(ampm, sizeof(ampm), "%p", localtime(&(time_t){time(NULL)}));
    graphics_draw_text(ctx, ampm,
      fonts_get_system_font(FONT_KEY_GOTHIC_14),
      GRect(w - 32, PBL_IF_ROUND_ELSE(y + 2, 2), 28, 16), GTextOverflowModeTrailingEllipsis,
      GTextAlignmentCenter, NULL);
  }
}

// Draws the date string using the user's configured date format (Gothic 24 Bold centered)
static void draw_date_text(GContext *ctx, int w, int y) {
  GRect date_box = GRect(4, y, w - 8, 26);
  strftime(s_date_text, sizeof(s_date_text), s_settings.date_format, localtime(&(time_t){time(NULL)}));
  graphics_draw_text(ctx, s_date_text,
    fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), date_box,
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// Top layer update for rectangular displays — draws time above date on a filled background
#ifndef PBL_ROUND
static void top_layer_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int h = bounds.size.h;
  GColor8 fg = get_fg_color();
  GColor8 bg = get_bg_color();
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_text_color(ctx, fg);
  graphics_context_set_fill_color(ctx, fg);
  int time_y = h * 12 / 100;
  int gap = (h - time_y - 42 - 24) / 2;
  draw_time_text(ctx, bounds.size.w, time_y);
  draw_date_text(ctx, bounds.size.w, time_y + 42 + gap);
}
#endif

// Full-screen layer update for round displays — quadrants at top and bottom, time+date centered
#ifdef PBL_ROUND
static void round_main_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int w = bounds.size.w, h = bounds.size.h;
  GColor8 fg = get_fg_color();
  GColor8 bg = get_bg_color();

  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_text_color(ctx, fg);
  graphics_context_set_fill_color(ctx, fg);

  int qh = h * 24 / 100;
  int qw = w / 2;

  s_quads[0] = (QuadrantData){.quad_id = s_settings.quad_tl};
  s_quads[1] = (QuadrantData){.quad_id = s_settings.quad_tr};
  draw_quadrant(ctx, 0, 2, qw, qh, s_settings.quad_tl, 0);
  draw_quadrant(ctx, qw, 2, qw, qh, s_settings.quad_tr, 1);

  int time_y = 2 + qh + (h - 2 * (qh + 2) - 48) / 3;
  draw_time_text(ctx, w, time_y);
  draw_date_text(ctx, w, time_y + 50);

  s_quads[2] = (QuadrantData){.quad_id = s_settings.quad_bl};
  s_quads[3] = (QuadrantData){.quad_id = s_settings.quad_br};
  draw_quadrant(ctx, 0, h - qh - 2, qw, qh, s_settings.quad_bl, 2);
  draw_quadrant(ctx, qw, h - qh - 2, qw, qh, s_settings.quad_br, 3);
}
#endif

// Bottom layer update for rectangular displays — draws 2x2 grid of quadrant info tiles
#ifndef PBL_ROUND
static void bot_layer_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GColor8 fg = get_fg_color();
  GColor8 bg = get_bg_color();
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_text_color(ctx, fg);

  int w = bounds.size.w;
  int h = bounds.size.h;
  int qw = w / 2;
  int qh = h / 2;

  s_quads[0] = (QuadrantData){.quad_id = s_settings.quad_tl};
  s_quads[1] = (QuadrantData){.quad_id = s_settings.quad_tr};
  s_quads[2] = (QuadrantData){.quad_id = s_settings.quad_bl};
  s_quads[3] = (QuadrantData){.quad_id = s_settings.quad_br};

  for (int i = 0; i < 4; i++) {
    int qx = (i % 2) * qw;
    int qy = (i / 2) * qh;
    if (s_quads[i].quad_id != 0) {
      draw_quadrant(ctx, qx, qy, qw, qh, s_quads[i].quad_id, i);
    }
  }

}
#endif

// Red banner drawn at the top of the screen when Bluetooth is disconnected
static void bt_layer_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  if (!s_bluetooth_connected) {
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    graphics_draw_text(ctx, "NO BT",
      fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
      bounds, GTextOverflowModeTrailingEllipsis,
      GTextAlignmentCenter, NULL);
  }
}

// Forces a full redraw of all display layers on next frame
static void mark_dirty_all(void) {
#ifdef PBL_ROUND
  layer_mark_dirty(s_main_layer);
#else
  layer_mark_dirty(s_top_layer);
  layer_mark_dirty(s_bot_layer);
#endif
}

// Bluetooth connection state change handler — shows/hides the red "NO BT" banner
static void bt_handler(bool connected) {
  s_bluetooth_connected = connected;
  layer_set_hidden(s_bt_layer, connected);
  if (!connected) layer_mark_dirty(s_bt_layer);
}

// Fetches today's step count from Pebble Health and averages heart rate from recent minute data
static void update_health(void) {
  HealthServiceAccessibilityMask mask = health_service_metric_accessible(HealthMetricStepCount,
    time_start_of_today(), time(NULL));
  if (mask & HealthServiceAccessibilityMaskAvailable) {
    s_steps = (int)health_service_sum_today(HealthMetricStepCount);
  }
  // Average heart rate from up to 10 recent minute-history records
  HealthMetric hr_metric = HealthMetricHeartRateBPM;
  mask = health_service_metric_accessible(hr_metric, time_start_of_today(), time(NULL));
  if (mask & HealthServiceAccessibilityMaskAvailable) {
    HealthMinuteData minute_data[10];
    time_t now = time(NULL);
    time_t time_end = 0;
    uint32_t count = health_service_get_minute_history(minute_data, 10, &now, &time_end);
    s_hr = 0;
    int samples = 0;
    for (uint32_t i = 0; i < count; i++) {
      if (minute_data[i].heart_rate_bpm > 0) {
        s_hr += minute_data[i].heart_rate_bpm;
        samples++;
      }
    }
    if (samples > 0) {
      s_hr /= samples;
      s_hr_available = true;
    } else {
      s_hr_available = false;
    }
  } else {
    s_hr_available = false;
  }
}

// Called every minute by the tick timer service — updates time, health, weather, and redraws
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  check_update_night();
  update_health();

  time_t now = time(NULL);
  bool need_weather = (now - s_last_weather_fetch > s_settings.weather_interval * 60);
  if (need_weather) s_last_weather_fetch = now;
  send_settings_with_blob(need_weather);

  s_batt_pct = battery_state_service_peek().charge_percent;
  mark_dirty_all();
}

// Processes incoming AppMessage from the JavaScript companion.
// Handles weather data, quadrant config, full settings blobs, and resend requests.
static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;

  // Extract weather data from the incoming message
  t = dict_find(iter, KEY_TEMP);
  if (t) {
    s_weather.temp = t->value->int16;
    s_weather.valid = true;
  }
  t = dict_find(iter, KEY_TEMP_HI);
  if (t) s_weather.temp_hi = t->value->int16;
  t = dict_find(iter, KEY_TEMP_LO);
  if (t) s_weather.temp_lo = t->value->int16;
  t = dict_find(iter, KEY_CONDITION);
  if (t) s_weather.condition = t->value->uint16;
  t = dict_find(iter, KEY_PRECIP_PROB);
  if (t) s_weather.precip_prob = t->value->uint8;
  t = dict_find(iter, KEY_SUNRISE);
  if (t) s_weather.sunrise_min = t->value->uint16;
  t = dict_find(iter, KEY_SUNSET);
  if (t) s_weather.sunset_min = t->value->uint16;
  t = dict_find(iter, KEY_LOCATION_NAME);
  if (t) strncpy(s_weather.location, t->value->cstring, sizeof(s_weather.location));
  t = dict_find(iter, KEY_ERROR);
  if (t) {
    s_weather.valid = false;
  }

  // Update quadrant type settings from individual keys (legacy individual messages)
  t = dict_find(iter, KEY_QUAD_TL);
  if (t && t->value->uint8 <= 6) { s_settings.quad_tl = t->value->uint8; mark_settings_dirty(); }
  t = dict_find(iter, KEY_QUAD_TR);
  if (t && t->value->uint8 <= 6) { s_settings.quad_tr = t->value->uint8; mark_settings_dirty(); }
  t = dict_find(iter, KEY_QUAD_BL);
  if (t && t->value->uint8 <= 6) { s_settings.quad_bl = t->value->uint8; mark_settings_dirty(); }
  t = dict_find(iter, KEY_QUAD_BR);
  if (t && t->value->uint8 <= 6) { s_settings.quad_br = t->value->uint8; mark_settings_dirty(); }

  // Full settings blob from JS (preferred — contains all settings at once)
  t = dict_find(iter, KEY_SETTINGS_BLOB);
  if (t && t->length == sizeof(Settings)) {
    memcpy(&s_settings, t->value->data, sizeof(Settings));
    s_settings.version = SETTINGS_VERSION;
    mark_settings_dirty();
  }

  // If JS requests a resend, send our current settings back
  t = dict_find(iter, KEY_REQUEST_RESEND);
  if (t) {
    send_settings_with_blob(false);
  }

  // Persist settings if they changed, re-evaluate night mode, and redraw
  if (s_settings_dirty) save_settings();
  check_update_night();

  if (s_weather.valid) s_last_weather_fetch = time(NULL);
  mark_dirty_all();
}

// Stub: required by app_message_register but not currently used
static void inbox_dropped(AppMessageResult reason, void *context) {}
static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {}
static void outbox_sent(DictionaryIterator *iter, void *context) {}

// Updates battery percentage and triggers a full redraw
static void battery_handler(BatteryChargeState charge) {
  s_batt_pct = charge.charge_percent;
  mark_dirty_all();
}

// Sets up layers and loads initial state when the main window appears
static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

#ifdef PBL_ROUND
  s_main_layer = layer_create(bounds);
  layer_set_update_proc(s_main_layer, round_main_update);
  layer_add_child(window_layer, s_main_layer);
#else
  int bot_h = bounds.size.h * QUAD_AREA_PCT / 100;
  int top_h = bounds.size.h - bot_h;

  s_top_layer = layer_create(GRect(0, 0, bounds.size.w, top_h));
  layer_set_update_proc(s_top_layer, top_layer_update);
  layer_add_child(window_layer, s_top_layer);

  s_bot_layer = layer_create(GRect(0, top_h, bounds.size.w, bot_h));
  layer_set_update_proc(s_bot_layer, bot_layer_update);
  layer_add_child(window_layer, s_bot_layer);
#endif

  s_bt_layer = layer_create(GRect(0, 0, bounds.size.w, 16));
  layer_set_update_proc(s_bt_layer, bt_layer_update);
  layer_add_child(window_layer, s_bt_layer);

  load_settings();
  check_update_night();
  update_health();

  s_batt_pct = battery_state_service_peek().charge_percent;
  s_bluetooth_connected = bluetooth_connection_service_peek();
  layer_set_hidden(s_bt_layer, s_bluetooth_connected);

  time_t now = time(NULL);
  s_last_weather_fetch = now - s_settings.weather_interval * 60 + 30;
  send_settings_with_blob(true);
}

// Frees layer memory when the main window is unloaded
static void window_unload(Window *window) {
#ifdef PBL_ROUND
  layer_destroy(s_main_layer);
#else
  layer_destroy(s_top_layer);
  layer_destroy(s_bot_layer);
#endif
  layer_destroy(s_bt_layer);
}

// Initializes the app: creates the window, registers callbacks, subscribes to services
static void init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload
  });

  // Set up AppMessage for communication with the JavaScript companion
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  app_message_register_outbox_sent(outbox_sent);
  app_message_open(792, 128);

  // Subscribe to system services
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);         // Fires every minute
  battery_state_service_subscribe(battery_handler);                 // Battery level changes
  bluetooth_connection_service_subscribe(bt_handler);               // BT connect/disconnect

  window_set_background_color(s_window, get_bg_color());
  window_stack_push(s_window, true);
}

// Cleans up all subscriptions, saves pending settings, and destroys the window
static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();
  app_message_deregister_callbacks();
  if (s_settings_dirty) save_settings();
  window_destroy(s_window);
}

// Standard Pebble app entry point
int main(void) {
  init();
  app_event_loop();
  deinit();
}
