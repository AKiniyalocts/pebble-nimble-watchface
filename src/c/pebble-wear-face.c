#include <pebble.h>

static Window *s_main_window;

static TextLayer *s_time_layer;
static char s_time_buffer[8];
static Layer *s_bottom_bg_layer;
static GColor s_bottom_bg_color;
static GColor s_bottom_fg_color;

static TextLayer *s_date_layer;
static char s_date_buffer[16];

static TextLayer *s_weather_layer;
static char s_weather_buffer[32];

static TextLayer *s_steps_layer;
static char s_steps_buffer[16];
static GBitmap *s_steps_bmp;
static BitmapLayer *s_steps_bmp_layer;
static Layer *s_steps_progress_layer;
static int s_step_goal = 0;

static TextLayer *s_hr_layer;
static char s_hr_buffer[8];
static GBitmap *s_hr_bmp;
static BitmapLayer *s_hr_bmp_layer;
static bool s_hr_available = false;

static int s_border_progress = 100;
static int s_border_anim_start = 0;
static Animation *s_border_anim = NULL;
static bool s_animate_seconds = true;

#define ICON_SIZE 28
#define ICON_GAP  4
#define DEFAULT_STEP_GOAL 10000

#define THEME_DARK 0
#define THEME_LIGHT 1
static int s_theme = THEME_DARK;
static GColor s_color_bg;
static GColor s_color_time;
static GColor s_color_weather;
static GColor s_color_steps;
static GColor s_color_hr;
static GColor s_color_track;

static void compute_theme_colors(int theme) {
  s_theme = theme;
  if (theme == THEME_LIGHT) {
    s_color_bg      = GColorWhite;
    s_color_time    = GColorBlack;
    s_color_weather = GColorBlueMoon;
    s_color_steps   = GColorIslamicGreen;
    s_color_hr      = GColorDarkCandyAppleRed;
    s_color_track   = PBL_IF_COLOR_ELSE(GColorLightGray, GColorBlack);
  } else {
    s_color_bg      = GColorBlack;
    s_color_time    = GColorChromeYellow;
    s_color_weather = GColorPictonBlue;
    s_color_steps   = GColorBrightGreen;
    s_color_hr      = GColorRed;
    s_color_track   = PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite);
  }
}

static GBitmap *load_themed_icon(int theme, uint32_t dark_id, uint32_t light_id) {
  return gbitmap_create_with_resource(theme == THEME_LIGHT ? light_id : dark_id);
}

// Overlay persisted user color picks (Clay color picker) on top of whatever
// `compute_theme_colors` produced. Theme defaults survive only until the
// user chooses a color; once persisted, the user's pick wins on every theme
// switch.
static void apply_user_widget_colors(void) {
  if (persist_exists(MESSAGE_KEY_WEATHER_COLOR)) {
    s_color_weather = GColorFromHEX(persist_read_int(MESSAGE_KEY_WEATHER_COLOR));
  }
  if (persist_exists(MESSAGE_KEY_STEPS_COLOR)) {
    s_color_steps = GColorFromHEX(persist_read_int(MESSAGE_KEY_STEPS_COLOR));
  }
  if (persist_exists(MESSAGE_KEY_HR_COLOR)) {
    s_color_hr = GColorFromHEX(persist_read_int(MESSAGE_KEY_HR_COLOR));
  }
}

static void apply_theme(int theme) {
  compute_theme_colors(theme);
  apply_user_widget_colors();
  if (s_main_window) {
    gbitmap_destroy(s_steps_bmp);
    s_steps_bmp = load_themed_icon(theme, RESOURCE_ID_ICON_STEPS_DARK, RESOURCE_ID_ICON_STEPS_LIGHT);
    bitmap_layer_set_bitmap(s_steps_bmp_layer, s_steps_bmp);

    if (s_hr_available) {
      gbitmap_destroy(s_hr_bmp);
      s_hr_bmp = load_themed_icon(theme, RESOURCE_ID_ICON_HEART_DARK, RESOURCE_ID_ICON_HEART_LIGHT);
      bitmap_layer_set_bitmap(s_hr_bmp_layer, s_hr_bmp);
      text_layer_set_text_color(s_hr_layer, s_color_hr);
    }

    window_set_background_color(s_main_window, s_color_bg);
    text_layer_set_text_color(s_steps_layer, s_color_steps);
    text_layer_set_text_color(s_weather_layer, s_color_weather);
    layer_mark_dirty(window_get_root_layer(s_main_window));
  }
}

static void update_time(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  strftime(s_time_buffer, sizeof(s_time_buffer),
           clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
  text_layer_set_text(s_time_layer, s_time_buffer);
}

static void update_date(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char month[4];
  strftime(month, sizeof(month), "%b", t);
  snprintf(s_date_buffer, sizeof(s_date_buffer), "%d %s", t->tm_mday, month);
  text_layer_set_text(s_date_layer, s_date_buffer);
}

static void format_steps(int steps, char *out, size_t out_size) {
  if (steps < 1000) {
    snprintf(out, out_size, "%d", steps);
  } else {
    snprintf(out, out_size, "%d,%03d", steps / 1000, steps % 1000);
  }
}

static void update_steps(void) {
  HealthValue steps = health_service_sum_today(HealthMetricStepCount);
  format_steps((int)steps, s_steps_buffer, sizeof(s_steps_buffer));
  text_layer_set_text(s_steps_layer, s_steps_buffer);
  layer_mark_dirty(s_steps_progress_layer);
}

static void draw_progress_border(GContext *ctx, GRect bounds, int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  const int x0 = bounds.origin.x;
  const int y0 = bounds.origin.y;
  const int w = bounds.size.w;
  const int h = bounds.size.h;
  const int r = (w < h ? w : h) / 2;
  const int curve = (314 * r) / 100;

  if (h <= w) {
    // Horizontal pill: straights along top/bottom, semicircles on left/right.
    const int straight = w - 2 * r;
    const int perimeter = 2 * straight + 2 * curve;
    const int filled = (perimeter * percent) / 100;

    int seg = filled < straight ? filled : straight;
    if (seg > 0) {
      graphics_draw_line(ctx, GPoint(x0 + r, y0), GPoint(x0 + r + seg, y0));
    }

    int rem = filled - straight;
    if (rem > 0) {
      int cs = rem < curve ? rem : curve;
      int end_deg = (180 * cs) / curve;
      GRect arc_b = GRect(x0 + w - 2 * r, y0, 2 * r, 2 * r);
      graphics_draw_arc(ctx, arc_b, GOvalScaleModeFitCircle,
                        0, DEG_TO_TRIGANGLE(end_deg));
    }

    rem = filled - straight - curve;
    if (rem > 0) {
      int s3 = rem < straight ? rem : straight;
      graphics_draw_line(ctx, GPoint(x0 + r + straight, y0 + h - 1),
                         GPoint(x0 + r + straight - s3, y0 + h - 1));
    }

    rem = filled - 2 * straight - curve;
    if (rem > 0) {
      int cs = rem < curve ? rem : curve;
      int end_deg = 180 + (180 * cs) / curve;
      GRect arc_b = GRect(x0, y0, 2 * r, 2 * r);
      graphics_draw_arc(ctx, arc_b, GOvalScaleModeFitCircle,
                        DEG_TO_TRIGANGLE(180), DEG_TO_TRIGANGLE(end_deg));
    }
  } else {
    // Vertical pill: straights down left/right, semicircles on top/bottom.
    // Walk clockwise from top-center.
    const int straight = h - 2 * r;
    const int half_curve = curve / 2;
    const int perimeter = 2 * straight + 2 * curve;
    const int filled = (perimeter * percent) / 100;

    int rem = filled;

    if (rem > 0) {
      int seg = rem < half_curve ? rem : half_curve;
      int end_deg = (90 * seg) / half_curve;
      GRect arc_b = GRect(x0, y0, 2 * r, 2 * r);
      graphics_draw_arc(ctx, arc_b, GOvalScaleModeFitCircle,
                        0, DEG_TO_TRIGANGLE(end_deg));
      rem -= seg;
    }

    if (rem > 0 && straight > 0) {
      int seg = rem < straight ? rem : straight;
      graphics_draw_line(ctx, GPoint(x0 + w - 1, y0 + r),
                         GPoint(x0 + w - 1, y0 + r + seg));
      rem -= seg;
    }

    if (rem > 0) {
      int seg = rem < curve ? rem : curve;
      int end_deg = 90 + (180 * seg) / curve;
      GRect arc_b = GRect(x0, y0 + h - 2 * r, 2 * r, 2 * r);
      graphics_draw_arc(ctx, arc_b, GOvalScaleModeFitCircle,
                        DEG_TO_TRIGANGLE(90), DEG_TO_TRIGANGLE(end_deg));
      rem -= seg;
    }

    if (rem > 0 && straight > 0) {
      int seg = rem < straight ? rem : straight;
      graphics_draw_line(ctx, GPoint(x0, y0 + h - r),
                         GPoint(x0, y0 + h - r - seg));
      rem -= seg;
    }

    if (rem > 0) {
      int seg = rem < half_curve ? rem : half_curve;
      int end_deg = 270 + (90 * seg) / half_curve;
      GRect arc_b = GRect(x0, y0, 2 * r, 2 * r);
      graphics_draw_arc(ctx, arc_b, GOvalScaleModeFitCircle,
                        DEG_TO_TRIGANGLE(270), DEG_TO_TRIGANGLE(end_deg));
    }
  }
}

static void draw_rounded_rect_border(GContext *ctx, GRect bounds, int r, int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  const int x0 = bounds.origin.x;
  const int y0 = bounds.origin.y;
  const int w = bounds.size.w;
  const int h = bounds.size.h;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;

  const int top_w  = w - 2 * r;
  const int side_h = h - 2 * r;
  const int corner = (314 * r) / 200;
  const int perimeter = 2 * top_w + 2 * side_h + 4 * corner;
  const int filled = (perimeter * percent) / 100;
  int rem = filled;

  if (rem > 0 && top_w > 0) {
    int seg = rem < top_w ? rem : top_w;
    graphics_draw_line(ctx, GPoint(x0 + r, y0), GPoint(x0 + r + seg, y0));
    rem -= seg;
  }
  if (rem > 0 && corner > 0) {
    int seg = rem < corner ? rem : corner;
    int end_deg = (90 * seg) / corner;
    GRect arc_b = GRect(x0 + w - 2 * r, y0, 2 * r, 2 * r);
    graphics_draw_arc(ctx, arc_b, GOvalScaleModeFitCircle,
                      0, DEG_TO_TRIGANGLE(end_deg));
    rem -= seg;
  }
  if (rem > 0 && side_h > 0) {
    int seg = rem < side_h ? rem : side_h;
    graphics_draw_line(ctx, GPoint(x0 + w - 1, y0 + r),
                       GPoint(x0 + w - 1, y0 + r + seg));
    rem -= seg;
  }
  if (rem > 0 && corner > 0) {
    int seg = rem < corner ? rem : corner;
    int end_deg = 90 + (90 * seg) / corner;
    GRect arc_b = GRect(x0 + w - 2 * r, y0 + h - 2 * r, 2 * r, 2 * r);
    graphics_draw_arc(ctx, arc_b, GOvalScaleModeFitCircle,
                      DEG_TO_TRIGANGLE(90), DEG_TO_TRIGANGLE(end_deg));
    rem -= seg;
  }
  if (rem > 0 && top_w > 0) {
    int seg = rem < top_w ? rem : top_w;
    graphics_draw_line(ctx, GPoint(x0 + w - r, y0 + h - 1),
                       GPoint(x0 + w - r - seg, y0 + h - 1));
    rem -= seg;
  }
  if (rem > 0 && corner > 0) {
    int seg = rem < corner ? rem : corner;
    int end_deg = 180 + (90 * seg) / corner;
    GRect arc_b = GRect(x0, y0 + h - 2 * r, 2 * r, 2 * r);
    graphics_draw_arc(ctx, arc_b, GOvalScaleModeFitCircle,
                      DEG_TO_TRIGANGLE(180), DEG_TO_TRIGANGLE(end_deg));
    rem -= seg;
  }
  if (rem > 0 && side_h > 0) {
    int seg = rem < side_h ? rem : side_h;
    graphics_draw_line(ctx, GPoint(x0, y0 + h - r),
                       GPoint(x0, y0 + h - r - seg));
    rem -= seg;
  }
  if (rem > 0 && corner > 0) {
    int seg = rem < corner ? rem : corner;
    int end_deg = 270 + (90 * seg) / corner;
    GRect arc_b = GRect(x0, y0, 2 * r, 2 * r);
    graphics_draw_arc(ctx, arc_b, GOvalScaleModeFitCircle,
                      DEG_TO_TRIGANGLE(270), DEG_TO_TRIGANGLE(end_deg));
  }
}

static void steps_progress_update_proc(Layer *layer, GContext *ctx) {
  if (s_step_goal > 0) {
    GRect b = layer_get_bounds(layer);
    GRect path = GRect(b.origin.x + 2, b.origin.y + 2,
                       b.size.w - 4, b.size.h - 4);

    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_stroke_color(ctx, s_color_track);
    draw_progress_border(ctx, path, 100);

    HealthValue steps = health_service_sum_today(HealthMetricStepCount);
    int percent = ((int)steps * 100) / s_step_goal;
    graphics_context_set_stroke_width(ctx, 5);
    graphics_context_set_stroke_color(ctx, s_color_steps);
    draw_progress_border(ctx, path, percent);
  }
}

static void bottom_bg_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, s_bottom_bg_color);
  graphics_fill_rect(ctx, b, 8, GCornersTop);
}

static void update_hr(void) {
  if (!s_hr_available) return;
  HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
  if (hr > 0) {
    snprintf(s_hr_buffer, sizeof(s_hr_buffer), "%d", (int)hr);
  } else {
    snprintf(s_hr_buffer, sizeof(s_hr_buffer), "--");
  }
  text_layer_set_text(s_hr_layer, s_hr_buffer);
}

static void border_anim_update(Animation *anim, const AnimationProgress dist_normalized) {
  int range = 100 - s_border_anim_start;
  s_border_progress = s_border_anim_start + ((int)dist_normalized * range) / ANIMATION_NORMALIZED_MAX;
}

static void border_anim_teardown(Animation *anim) {
  s_border_anim = NULL;
}

static const AnimationImplementation s_border_impl = {
  .update = border_anim_update,
  .teardown = border_anim_teardown,
};

static void start_border_animation(void) {
  if (s_border_anim) {
    animation_unschedule(s_border_anim);
  }
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  s_border_anim_start = (t->tm_sec * 100) / 60;
  s_border_progress = s_border_anim_start;
  uint32_t duration_ms = (60 - t->tm_sec) * 1000;
  if (duration_ms < 100) duration_ms = 100;

  s_border_anim = animation_create();
  animation_set_implementation(s_border_anim, &s_border_impl);
  animation_set_duration(s_border_anim, duration_ms);
  animation_set_curve(s_border_anim, AnimationCurveLinear);
  animation_schedule(s_border_anim);
}

static void apply_animate_seconds(bool enabled) {
  s_animate_seconds = enabled;
  // Border render is disabled for now; preserve the persisted setting but
  // skip scheduling/unscheduling animations.
  if (s_border_anim) {
    animation_unschedule(s_border_anim);
  }
  s_border_progress = 100;
}

static void request_weather(void) {
  DictionaryIterator *iter;
  AppMessageResult res = app_message_outbox_begin(&iter);
  if (res == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
    app_message_outbox_send();
  }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
  update_date();
  if (tick_time->tm_min % 30 == 0) {
    request_weather();
  }
}

static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventMovementUpdate) {
    update_steps();
  }
  if (event == HealthEventHeartRateUpdate) {
    update_hr();
  }
}

// When a notification (or any obstruction) shrinks the visible watchface,
// hide the steps value text so it doesn't overlap the time at the bottom.
// Only the value is hidden — the steps progress circle stays visible.
static void unobstructed_did_change(void *context) {
  if (!s_main_window || !s_steps_layer) return;
  Layer *root = window_get_root_layer(s_main_window);
  GRect full = layer_get_bounds(root);
  GRect unob = layer_get_unobstructed_bounds(root);
  bool obstructed = unob.size.h < full.size.h;
  layer_set_hidden(text_layer_get_layer(s_steps_layer), obstructed);
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *temp_tuple = dict_find(iter, MESSAGE_KEY_TEMPERATURE);
  if (temp_tuple) {
    snprintf(s_weather_buffer, sizeof(s_weather_buffer), "%d°",
             (int)temp_tuple->value->int32);
    if (s_weather_layer) text_layer_set_text(s_weather_layer, s_weather_buffer);
  }
  Tuple *goal_tuple = dict_find(iter, MESSAGE_KEY_STEP_GOAL);
  if (goal_tuple) {
    if (goal_tuple->type == TUPLE_CSTRING) {
      s_step_goal = atoi(goal_tuple->value->cstring);
    } else {
      s_step_goal = (int)goal_tuple->value->int32;
    }
    persist_write_int(MESSAGE_KEY_STEP_GOAL, s_step_goal);
    APP_LOG(APP_LOG_LEVEL_INFO, "step goal updated: %d", s_step_goal);
    layer_mark_dirty(s_steps_progress_layer);
  }
  Tuple *theme_tuple = dict_find(iter, MESSAGE_KEY_THEME);
  if (theme_tuple) {
    int new_theme;
    if (theme_tuple->type == TUPLE_CSTRING) {
      new_theme = atoi(theme_tuple->value->cstring);
    } else {
      new_theme = (int)theme_tuple->value->int32;
    }
    persist_write_int(MESSAGE_KEY_THEME, new_theme);
    apply_theme(new_theme);
  }
  Tuple *anim_tuple = dict_find(iter, MESSAGE_KEY_ANIMATE_SECONDS);
  if (anim_tuple) {
    bool enabled;
    switch (anim_tuple->type) {
      case TUPLE_CSTRING:
        enabled = atoi(anim_tuple->value->cstring) != 0;
        break;
      case TUPLE_INT:
      case TUPLE_UINT:
        enabled = anim_tuple->value->int32 != 0;
        break;
      default:
        enabled = anim_tuple->value->uint8 != 0;
        break;
    }
    persist_write_bool(MESSAGE_KEY_ANIMATE_SECONDS, enabled);
    apply_animate_seconds(enabled);
  }
  Tuple *bg_tuple = dict_find(iter, MESSAGE_KEY_BOTTOM_BG_COLOR);
  if (bg_tuple) {
    int hex = (int)bg_tuple->value->int32;
    persist_write_int(MESSAGE_KEY_BOTTOM_BG_COLOR, hex);
    s_bottom_bg_color = GColorFromHEX(hex);
    if (s_bottom_bg_layer) layer_mark_dirty(s_bottom_bg_layer);
  }
  Tuple *fg_tuple = dict_find(iter, MESSAGE_KEY_BOTTOM_FG_COLOR);
  if (fg_tuple) {
    int hex = (int)fg_tuple->value->int32;
    persist_write_int(MESSAGE_KEY_BOTTOM_FG_COLOR, hex);
    s_bottom_fg_color = GColorFromHEX(hex);
    if (s_time_layer) text_layer_set_text_color(s_time_layer, s_bottom_fg_color);
    if (s_date_layer) text_layer_set_text_color(s_date_layer, s_bottom_fg_color);
  }
  Tuple *weather_tuple = dict_find(iter, MESSAGE_KEY_WEATHER_COLOR);
  if (weather_tuple) {
    int hex = (int)weather_tuple->value->int32;
    persist_write_int(MESSAGE_KEY_WEATHER_COLOR, hex);
    s_color_weather = GColorFromHEX(hex);
    if (s_weather_layer) text_layer_set_text_color(s_weather_layer, s_color_weather);
  }
  Tuple *steps_tuple = dict_find(iter, MESSAGE_KEY_STEPS_COLOR);
  if (steps_tuple) {
    int hex = (int)steps_tuple->value->int32;
    persist_write_int(MESSAGE_KEY_STEPS_COLOR, hex);
    s_color_steps = GColorFromHEX(hex);
    if (s_steps_layer) text_layer_set_text_color(s_steps_layer, s_color_steps);
    if (s_steps_progress_layer) layer_mark_dirty(s_steps_progress_layer);
  }
  Tuple *hr_tuple = dict_find(iter, MESSAGE_KEY_HR_COLOR);
  if (hr_tuple) {
    int hex = (int)hr_tuple->value->int32;
    persist_write_int(MESSAGE_KEY_HR_COLOR, hex);
    s_color_hr = GColorFromHEX(hex);
    if (s_hr_available && s_hr_layer) text_layer_set_text_color(s_hr_layer, s_color_hr);
  }
}


static TextLayer *make_text_layer(GRect rect, GTextAlignment align, const char *font_key, GColor color) {
  TextLayer *layer = text_layer_create(rect);
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_color(layer, color);
  text_layer_set_font(layer, fonts_get_system_font(font_key));
  text_layer_set_text_alignment(layer, align);
  return layer;
}

static BitmapLayer *make_icon_layer(GRect rect, GBitmap *bmp) {
  BitmapLayer *layer = bitmap_layer_create(rect);
  bitmap_layer_set_bitmap(layer, bmp);
  bitmap_layer_set_compositing_mode(layer, GCompOpSet);
  bitmap_layer_set_background_color(layer, GColorClear);
  return layer;
}

static void animate_layer_to(Layer *layer, GRect to, uint32_t delay, uint32_t duration) {
  GRect from = layer_get_frame(layer);
  PropertyAnimation *anim = property_animation_create_layer_frame(layer, &from, &to);
  animation_set_duration((Animation *)anim, duration);
  animation_set_delay((Animation *)anim, delay);
  animation_set_curve((Animation *)anim, AnimationCurveEaseOut);
  animation_schedule((Animation *)anim);
}

static void slide_in_from(Layer *layer, GPoint from_origin, uint32_t delay, uint32_t duration) {
  GRect to = layer_get_frame(layer);
  GRect from = GRect(from_origin.x, from_origin.y, to.size.w, to.size.h);
  layer_set_frame(layer, from);
  animate_layer_to(layer, to, delay, duration);
}

static void run_entrance_animations(void) {
  Layer *steps_icon_l = bitmap_layer_get_layer(s_steps_bmp_layer);
  GRect sif = layer_get_frame(steps_icon_l);
  slide_in_from(steps_icon_l, GPoint(-sif.size.w, sif.origin.y), 200, 400);

  Layer *steps_text_l = text_layer_get_layer(s_steps_layer);
  GRect stf = layer_get_frame(steps_text_l);
  slide_in_from(steps_text_l, GPoint(-stf.size.w, stf.origin.y), 200, 400);

  GRect spf = layer_get_frame(s_steps_progress_layer);
  slide_in_from(s_steps_progress_layer, GPoint(-spf.size.w, spf.origin.y), 200, 400);

  if (s_hr_available) {
    Layer *hr_icon_l = bitmap_layer_get_layer(s_hr_bmp_layer);
    GRect hif = layer_get_frame(hr_icon_l);
    slide_in_from(hr_icon_l, GPoint(-hif.size.w, hif.origin.y), 300, 400);

    Layer *hr_text_l = text_layer_get_layer(s_hr_layer);
    GRect htf = layer_get_frame(hr_text_l);
    slide_in_from(hr_text_l, GPoint(-htf.size.w, htf.origin.y), 300, 400);
  }
}

static void main_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(root);

  // Safe content area: bigger inset on round to clear the curved corners,
  // small inset on rect to give the steps progress pill room to breathe.
  GRect content = grect_inset(bounds, GEdgeInsets(PBL_IF_ROUND_ELSE(20, 4)));

  time_t now_t = time(NULL);
  HealthServiceAccessibilityMask hr_mask = health_service_metric_accessible(
      HealthMetricHeartRateBPM, now_t, now_t);
  s_hr_available = (hr_mask & HealthServiceAccessibilityMaskAvailable) != 0;

  s_steps_bmp = load_themed_icon(s_theme, RESOURCE_ID_ICON_STEPS_DARK, RESOURCE_ID_ICON_STEPS_LIGHT);
  if (s_hr_available) {
    s_hr_bmp = load_themed_icon(s_theme, RESOURCE_ID_ICON_HEART_DARK, RESOURCE_ID_ICON_HEART_LIGHT);
  }

  // Layout: time + date form a horizontal row at the bottom; the temperature
  // sits directly above the date inside the bottom bar; HR (left) and steps
  // (right) sit in the middle band sharing a common top y. The bordered
  // "second-progress" time box is disabled for now.
  const int16_t widget_h = 28;
  const int16_t date_h = 22;
  const int16_t intra_gap = 2;
  const int16_t left_col_w = 60;
  const int16_t center_pad = 12;
  const int16_t progress_pad = 8;
  const int16_t stroke_allowance = 2;
  const int16_t total_pad = progress_pad + stroke_allowance;
  const int16_t steps_circle_size = ICON_SIZE + 2 * total_pad;
  const int16_t steps_value_pad = 4;

  // Two columns mirroring across the screen centerline: HR on the left,
  // weather + steps stacked on the right. Each column anchors `center_pad`
  // away from the centerline; if the left column would clip off the edge,
  // fall back to an edge-anchored layout.
  const int16_t center_x = content.origin.x + content.size.w / 2;
  const int16_t candidate_left_x = center_x - center_pad - left_col_w;
  int16_t left_x;
  if (candidate_left_x >= total_pad) {
    left_x = candidate_left_x;
  } else {
    left_x = content.origin.x < total_pad ? total_pad : content.origin.x;
  }
  const int16_t stack_icon_x = left_x + (left_col_w - ICON_SIZE) / 2;

  const int16_t right_col_x = center_x + center_pad;
  const int16_t right_col_w = content.origin.x + content.size.w - right_col_x;
  const int16_t right_stack_icon_x = right_col_x + (right_col_w - ICON_SIZE) / 2;

  // Bottom row: time (Roboto Bold Subset 49, ~56px line height) on the left,
  // date (Gothic 18 Bold) on the right, both bottom-anchored to content.
  // Time rect spans full content width so it doesn't wrap on narrow rect
  // targets where "12:34" (~130px) approaches content.w; the date overlays
  // its right edge.
  const int16_t time_h = 56;
  const int16_t date_w = 80;
  const int16_t bottom_row_y = content.origin.y + content.size.h - time_h;

  // Bottom bar background — full screen width, from bottom_row_y to the
  // bottom edge of the screen (bleeds past `content` so it fills the curved
  // corners on round). Drawn first so time/date render on top.
  GRect bottom_bg_rect = GRect(0, bottom_row_y, bounds.size.w, bounds.size.h - bottom_row_y);
  s_bottom_bg_layer = layer_create(bottom_bg_rect);
  layer_set_update_proc(s_bottom_bg_layer, bottom_bg_update_proc);
  layer_add_child(root, s_bottom_bg_layer);

  GRect time_rect = GRect(content.origin.x, bottom_row_y, content.size.w, time_h);
  s_time_layer = text_layer_create(time_rect);
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, s_bottom_fg_color);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentLeft);
  layer_add_child(root, text_layer_get_layer(s_time_layer));

  GRect date_rect = GRect(content.origin.x + content.size.w - date_w,
                          bottom_row_y + (time_h - date_h),
                          date_w,
                          date_h);
  s_date_layer = make_text_layer(date_rect, GTextAlignmentRight, FONT_KEY_GOTHIC_18_BOLD, s_bottom_fg_color);
  layer_add_child(root, text_layer_get_layer(s_date_layer));

  // Temperature — right-aligned, directly above the date inside the bottom
  // bar. Same width and font sizing as the date so the two read as a stack.
  GRect weather_rect = GRect(date_rect.origin.x,
                             date_rect.origin.y - date_h,
                             date_w,
                             date_h);
  strcpy(s_weather_buffer, "...");
  s_weather_layer = text_layer_create(weather_rect);
  text_layer_set_background_color(s_weather_layer, GColorClear);
  text_layer_set_text_color(s_weather_layer, s_color_weather);
  text_layer_set_font(s_weather_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_weather_layer, GTextAlignmentRight);
  text_layer_set_text(s_weather_layer, s_weather_buffer);
  layer_add_child(root, text_layer_get_layer(s_weather_layer));

  // HR (left) and steps (right) each vertically center themselves in the
  // available space between `content.top` and the top of the bottom bar.
  // They have different heights (steps stack is taller because of the
  // progress circle), so they share a vertical midpoint rather than a top.
  const int16_t steps_stack_h = steps_circle_size + steps_value_pad + widget_h;
  const int16_t hr_stack_h = ICON_SIZE + intra_gap + widget_h;
  const int16_t avail_top = content.origin.y;
  const int16_t avail_h = bottom_row_y - avail_top;
  const int16_t stack_top_y = avail_top + (avail_h - steps_stack_h) / 2;
  const int16_t hr_y = avail_top + (avail_h - hr_stack_h) / 2;

  s_steps_bmp_layer = make_icon_layer(GRect(right_stack_icon_x, stack_top_y + total_pad, ICON_SIZE, ICON_SIZE), s_steps_bmp);
  layer_add_child(root, bitmap_layer_get_layer(s_steps_bmp_layer));
  s_steps_layer = make_text_layer(GRect(right_col_x, stack_top_y + steps_circle_size + steps_value_pad, right_col_w, widget_h), GTextAlignmentCenter, FONT_KEY_GOTHIC_24_BOLD, s_color_steps);
  layer_add_child(root, text_layer_get_layer(s_steps_layer));

  GRect steps_progress_rect = GRect(right_stack_icon_x - total_pad,
                                    stack_top_y,
                                    steps_circle_size,
                                    steps_circle_size);
  s_steps_progress_layer = layer_create(steps_progress_rect);
  layer_set_update_proc(s_steps_progress_layer, steps_progress_update_proc);
  layer_add_child(root, s_steps_progress_layer);

  if (s_hr_available) {
    s_hr_bmp_layer = make_icon_layer(GRect(stack_icon_x, hr_y, ICON_SIZE, ICON_SIZE), s_hr_bmp);
    layer_add_child(root, bitmap_layer_get_layer(s_hr_bmp_layer));
    s_hr_layer = make_text_layer(GRect(left_x, hr_y + ICON_SIZE + intra_gap, left_col_w, widget_h), GTextAlignmentCenter, FONT_KEY_GOTHIC_24_BOLD, s_color_hr);
    layer_add_child(root, text_layer_get_layer(s_hr_layer));
  }

  update_time();
  update_date();
  update_steps();
  update_hr();

  run_entrance_animations();
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_weather_layer);
  layer_destroy(s_bottom_bg_layer);
  layer_destroy(s_steps_progress_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_steps_layer);
  bitmap_layer_destroy(s_steps_bmp_layer);
  gbitmap_destroy(s_steps_bmp);
  if (s_hr_available) {
    text_layer_destroy(s_hr_layer);
    bitmap_layer_destroy(s_hr_bmp_layer);
    gbitmap_destroy(s_hr_bmp);
  }
}

static void prv_init(void) {
  s_step_goal = persist_read_int(MESSAGE_KEY_STEP_GOAL);
  if (s_step_goal <= 0 || s_step_goal > 1000000) {
    s_step_goal = DEFAULT_STEP_GOAL;
  }

  int saved_theme = persist_read_int(MESSAGE_KEY_THEME);
  compute_theme_colors(saved_theme == THEME_LIGHT ? THEME_LIGHT : THEME_DARK);
  apply_user_widget_colors();

  s_animate_seconds = persist_exists(MESSAGE_KEY_ANIMATE_SECONDS)
      ? persist_read_bool(MESSAGE_KEY_ANIMATE_SECONDS)
      : true;
  s_border_progress = 100;

  s_bottom_bg_color = persist_exists(MESSAGE_KEY_BOTTOM_BG_COLOR)
      ? GColorFromHEX(persist_read_int(MESSAGE_KEY_BOTTOM_BG_COLOR))
      : GColorBlack;
  s_bottom_fg_color = persist_exists(MESSAGE_KEY_BOTTOM_FG_COLOR)
      ? GColorFromHEX(persist_read_int(MESSAGE_KEY_BOTTOM_FG_COLOR))
      : GColorWhite;

  s_main_window = window_create();
  window_set_background_color(s_main_window, s_color_bg);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  health_service_events_subscribe(health_handler, NULL);
  unobstructed_area_service_subscribe(
      (UnobstructedAreaHandlers){ .did_change = unobstructed_did_change },
      NULL);
  // Subscribe only fires on changes — apply the current state once at
  // startup so we don't miss an obstruction that's already on screen.
  unobstructed_did_change(NULL);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(app_message_inbox_size_maximum(),
                   app_message_outbox_size_maximum());

  request_weather();
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  health_service_events_unsubscribe();
  unobstructed_area_service_unsubscribe();
  if (s_border_anim) {
    animation_unschedule(s_border_anim);
  }
  window_destroy(s_main_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
