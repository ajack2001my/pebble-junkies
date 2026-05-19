#include <pebble.h>

#define SETTINGS_KEY 1
#define SETTINGS_VERSION 3
#define QUAD_AREA_PCT 45

typedef struct {
  uint8_t quad_id;     // 0=off,1=batt,2=steps,3=hr,4=rain
  char label[10];
  char value[20];
  uint8_t icon_char;   // simple icon marker
  bool valid;
} QuadrantData;

static Window *s_window;
static Layer *s_bt_layer;
#ifdef PBL_ROUND
static Layer *s_main_layer;
#else
static Layer *s_top_layer;
static Layer *s_bot_layer;
#endif

static bool s_bluetooth_connected = true;

static char s_time_text[12];
static char s_date_text[40];

static QuadrantData s_quads[4];
static int16_t s_batt_pct = 100;
static int16_t s_steps = 0;
static int16_t s_hr = 0;
static bool s_hr_available = false;

enum {
  KEY_REQUEST_WEATHER = 0,
  KEY_TEMP = 1,
  KEY_TEMP_HI = 2,
  KEY_TEMP_LO = 3,
  KEY_CONDITION = 4,
  KEY_PRECIP_PROB = 5,
  KEY_SUNRISE = 6,
  KEY_SUNSET = 7,
  KEY_LOCATION_NAME = 8,
  KEY_ERROR = 9,
  KEY_USE_24H = 10,
  KEY_DATE_FORMAT = 11,
  KEY_TEMP_UNIT = 12,
  KEY_QUAD_TL = 13,
  KEY_QUAD_TR = 14,
  KEY_QUAD_BL = 15,
  KEY_QUAD_BR = 16,
  KEY_FG_DAY = 17,
  KEY_BG_DAY = 18,
  KEY_USE_DAY_NIGHT = 19,
  KEY_FG_NIGHT = 20,
  KEY_BG_NIGHT = 21,
  KEY_USE_GPS = 22,
  KEY_LATITUDE = 23,
  KEY_LONGITUDE = 24,
  KEY_LOCATION_STR = 25,
  KEY_SETTINGS_BLOB = 26,
  KEY_REQUEST_SETTINGS = 27,
  KEY_REQUEST_RESEND = 28,
};

typedef struct __attribute__((__packed__)) {
  uint8_t version;
  bool use_24h;
  char date_format[20];
  uint8_t temp_unit;
  uint8_t weather_interval;
  uint8_t quad_tl;
  uint8_t quad_tr;
  uint8_t quad_bl;
  uint8_t quad_br;
  GColor8 fg_day;
  GColor8 bg_day;
  bool use_day_night;
  GColor8 fg_night;
  GColor8 bg_night;
  bool use_gps;
  int32_t latitude;
  int32_t longitude;
  char location_name[40];
} Settings;

typedef struct {
  int16_t temp;
  int16_t temp_hi;
  int16_t temp_lo;
  uint16_t condition;
  uint8_t precip_prob;
  uint16_t sunrise_min;
  uint16_t sunset_min;
  char location[40];
  bool valid;
} WeatherData;

static Settings s_settings;
static WeatherData s_weather;
static bool s_is_night = false;
static time_t s_last_weather_fetch = 0;

static void set_default_settings(void) {
  s_settings.version = SETTINGS_VERSION;
  s_settings.use_24h = false;
  strcpy(s_settings.date_format, "%a, %b %d");
  s_settings.temp_unit = 0;
  s_settings.weather_interval = 60;
  s_settings.quad_tl = 1;
  s_settings.quad_tr = 4;
  s_settings.quad_bl = 2;
  s_settings.quad_br = 3;
  s_settings.fg_day = GColorWhite;
  s_settings.bg_day = GColorBlack;
  s_settings.use_day_night = false;
  s_settings.fg_night = GColorBlack;
  s_settings.bg_night = GColorWhite;
  s_settings.use_gps = true;
  s_settings.latitude = 0;
  s_settings.longitude = 0;
  s_settings.location_name[0] = 0;
  s_weather.valid = false;
}

static GColor8 get_fg_color(void) {
  if (s_settings.use_day_night && s_is_night) return s_settings.fg_night;
  return s_settings.fg_day;
}

static GColor8 get_bg_color(void) {
  if (s_settings.use_day_night && s_is_night) return s_settings.bg_night;
  return s_settings.bg_day;
}

static bool s_settings_dirty = false;

static void save_settings(void) {
  persist_write_data(SETTINGS_KEY, &s_settings, sizeof(Settings));
  s_settings_dirty = false;
}

static void mark_settings_dirty(void) {
  s_settings_dirty = true;
}

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

static void send_request(uint8_t key) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, key, 1);
  app_message_outbox_send();
}

static void send_settings_with_blob(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, KEY_REQUEST_SETTINGS, 1);
  dict_write_data(iter, KEY_SETTINGS_BLOB, (const uint8_t *)&s_settings, sizeof(Settings));
  app_message_outbox_send();
}

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

static int16_t convert_temp(int16_t temp_tenths_c) {
  switch (s_settings.temp_unit) {
    case 1: return (temp_tenths_c * 9 / 5 + 320);
    case 2: return (temp_tenths_c + 2731);
    default: return temp_tenths_c;
  }
}

static void format_temp(int16_t temp_tenths, char *buf, size_t len) {
  int16_t t = convert_temp(temp_tenths);
  char unit = 'C';
  if (s_settings.temp_unit == 1) unit = 'F';
  else if (s_settings.temp_unit == 2) unit = 'K';
  snprintf(buf, len, "%d°%c", t / 10, unit);
}

static void draw_weather_icon(GContext *ctx, int x, int y, int s, uint16_t code, bool night) {
  graphics_context_set_fill_color(ctx, get_fg_color());
  graphics_context_set_stroke_color(ctx, get_fg_color());

  if (code == 0) {
    if (night) {
      graphics_fill_circle(ctx, GPoint(x + s/2, y + s/2), s/3);
      graphics_context_set_fill_color(ctx, get_bg_color());
      graphics_fill_circle(ctx, GPoint(x + s/2 + s/6, y + s/2 - s/6), s/4);
      graphics_context_set_fill_color(ctx, get_fg_color());
    } else {
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
  int cx = x + s/2;
  int cy = y + s/2 + 2;
  graphics_fill_circle(ctx, GPoint(cx - 4, cy), 6);
  graphics_fill_circle(ctx, GPoint(cx + 4, cy + 1), 5);
  graphics_fill_circle(ctx, GPoint(cx, cy - 3), 5);
  graphics_fill_rect(ctx, GRect(cx - 8, cy - 2, 16, 4), 0, GCornerNone);

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

  if (code >= 61 && code <= 82) {
    graphics_draw_line(ctx, GPoint(cx - 4, cy + 5), GPoint(cx - 4, cy + 10));
    graphics_draw_line(ctx, GPoint(cx + 0, cy + 5), GPoint(cx + 0, cy + 10));
    graphics_draw_line(ctx, GPoint(cx + 4, cy + 5), GPoint(cx + 4, cy + 10));
  } else if (code >= 71 && code <= 77) {
    graphics_draw_line(ctx, GPoint(cx - 5, cy + 3), GPoint(cx + 2, cy + 10));
    graphics_draw_line(ctx, GPoint(cx + 2, cy + 3), GPoint(cx - 5, cy + 10));
    graphics_draw_line(ctx, GPoint(cx - 2, cy + 3), GPoint(cx + 4, cy + 8));
    graphics_draw_line(ctx, GPoint(cx + 4, cy + 3), GPoint(cx - 2, cy + 8));
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
  } else if (code >= 45 && code <= 48) {
    for (int i = 0; i < 3; i++) {
      graphics_draw_line(ctx, GPoint(cx - 6, cy + 2 + i * 3), GPoint(cx + 6, cy + 2 + i * 3));
    }
  }
}

static void draw_quadrant(GContext *ctx, int x, int y, int w, int h, uint8_t type, int index) {
  if (type == 0) return;
  graphics_context_set_fill_color(ctx, get_fg_color());

  switch (type) {
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

static void draw_date_text(GContext *ctx, int w, int y) {
  GRect date_box = GRect(4, y, w - 8, 26);
  strftime(s_date_text, sizeof(s_date_text), s_settings.date_format, localtime(&(time_t){time(NULL)}));
  graphics_draw_text(ctx, s_date_text,
    fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), date_box,
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

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

static void mark_dirty_all(void) {
#ifdef PBL_ROUND
  layer_mark_dirty(s_main_layer);
#else
  layer_mark_dirty(s_top_layer);
  layer_mark_dirty(s_bot_layer);
#endif
}

static void bt_handler(bool connected) {
  s_bluetooth_connected = connected;
  layer_set_hidden(s_bt_layer, connected);
  if (!connected) layer_mark_dirty(s_bt_layer);
}

static void update_health(void) {
  HealthServiceAccessibilityMask mask = health_service_metric_accessible(HealthMetricStepCount,
    time_start_of_today(), time(NULL));
  if (mask & HealthServiceAccessibilityMaskAvailable) {
    s_steps = (int)health_service_sum_today(HealthMetricStepCount);
  }
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

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  check_update_night();
  update_health();
  send_settings_with_blob();

  time_t now = time(NULL);
  if (now - s_last_weather_fetch > s_settings.weather_interval * 60) {
    s_last_weather_fetch = now;
    send_request(KEY_REQUEST_WEATHER);
  }

  s_batt_pct = battery_state_service_peek().charge_percent;
  mark_dirty_all();
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;

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

  t = dict_find(iter, KEY_QUAD_TL);
  if (t && t->value->uint8 <= 6) { s_settings.quad_tl = t->value->uint8; mark_settings_dirty(); }
  t = dict_find(iter, KEY_QUAD_TR);
  if (t && t->value->uint8 <= 6) { s_settings.quad_tr = t->value->uint8; mark_settings_dirty(); }
  t = dict_find(iter, KEY_QUAD_BL);
  if (t && t->value->uint8 <= 6) { s_settings.quad_bl = t->value->uint8; mark_settings_dirty(); }
  t = dict_find(iter, KEY_QUAD_BR);
  if (t && t->value->uint8 <= 6) { s_settings.quad_br = t->value->uint8; mark_settings_dirty(); }

  t = dict_find(iter, KEY_SETTINGS_BLOB);
  if (t && t->length == sizeof(Settings)) {
    memcpy(&s_settings, t->value->data, sizeof(Settings));
    s_settings.version = SETTINGS_VERSION;
    mark_settings_dirty();
  }

  t = dict_find(iter, KEY_REQUEST_RESEND);
  if (t) {
    send_settings_with_blob();
  }

  if (s_settings_dirty) save_settings();
  check_update_night();

  if (s_weather.valid) s_last_weather_fetch = time(NULL);
  mark_dirty_all();
}

static void inbox_dropped(AppMessageResult reason, void *context) {}
static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {}
static void outbox_sent(DictionaryIterator *iter, void *context) {}

static void battery_handler(BatteryChargeState charge) {
  s_batt_pct = charge.charge_percent;
  mark_dirty_all();
}

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
  send_request(KEY_REQUEST_WEATHER);
  send_settings_with_blob();
}

static void window_unload(Window *window) {
#ifdef PBL_ROUND
  layer_destroy(s_main_layer);
#else
  layer_destroy(s_top_layer);
  layer_destroy(s_bot_layer);
#endif
  layer_destroy(s_bt_layer);
}

static void init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload
  });

  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  app_message_register_outbox_sent(outbox_sent);
  app_message_open(792, 128);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  bluetooth_connection_service_subscribe(bt_handler);

  window_set_background_color(s_window, get_bg_color());
  window_stack_push(s_window, true);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();
  app_message_deregister_callbacks();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
