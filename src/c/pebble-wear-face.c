#include <pebble.h>

static Window *s_main_window;

static Layer *s_time_box_layer;
static char s_time_buffer[8];

static TextLayer *s_date_layer;
static char s_date_buffer[16];

static Layer *s_weather_layer;
static char s_weather_buffer[32];
static GBitmap *s_weather_bmp;
static GRect s_weather_target_frame;
static bool s_weather_animated;

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

#define ICON_SIZE 28
#define ICON_GAP  4
#define TIME_BOX_H 50
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

static void apply_theme(int theme) {
  compute_theme_colors(theme);
  if (s_main_window) {
    gbitmap_destroy(s_weather_bmp);
    gbitmap_destroy(s_steps_bmp);
    s_weather_bmp = load_themed_icon(theme, RESOURCE_ID_ICON_CLOUD_DARK, RESOURCE_ID_ICON_CLOUD_LIGHT);
    s_steps_bmp   = load_themed_icon(theme, RESOURCE_ID_ICON_STEPS_DARK, RESOURCE_ID_ICON_STEPS_LIGHT);
    bitmap_layer_set_bitmap(s_steps_bmp_layer, s_steps_bmp);

    if (s_hr_available) {
      gbitmap_destroy(s_hr_bmp);
      s_hr_bmp = load_themed_icon(theme, RESOURCE_ID_ICON_HEART_DARK, RESOURCE_ID_ICON_HEART_LIGHT);
      bitmap_layer_set_bitmap(s_hr_bmp_layer, s_hr_bmp);
      text_layer_set_text_color(s_hr_layer, s_color_hr);
    }

    window_set_background_color(s_main_window, s_color_bg);
    text_layer_set_text_color(s_date_layer, s_color_time);
    text_layer_set_text_color(s_steps_layer, s_color_steps);
    layer_mark_dirty(window_get_root_layer(s_main_window));
  }
}

static void update_time(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  const char *fmt = clock_is_24h_style() ? "%H:%M" : "%I:%M";
  strftime(s_time_buffer, sizeof(s_time_buffer), fmt, t);
  layer_mark_dirty(s_time_box_layer);
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
  const int straight = w - 2 * r;
  const int curve = (314 * r) / 100;
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

static void time_box_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int border_w = 3;
  const int outer_r = bounds.size.h / 2;

  graphics_context_set_fill_color(ctx, s_color_time);
  graphics_fill_rect(ctx, bounds, outer_r, GCornersAll);

  GRect inner = GRect(bounds.origin.x + border_w,
                      bounds.origin.y + border_w,
                      bounds.size.w - 2 * border_w,
                      bounds.size.h - 2 * border_w);
  graphics_context_set_fill_color(ctx, s_color_bg);
  graphics_fill_rect(ctx, inner, inner.size.h / 2, GCornersAll);

  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  const int font_h = 28;
  const int top_pad = 4;
  GRect text_rect = GRect(bounds.origin.x,
                          bounds.origin.y + (bounds.size.h - font_h) / 2 - top_pad,
                          bounds.size.w,
                          font_h + top_pad);
  graphics_context_set_text_color(ctx, s_color_time);
  graphics_draw_text(ctx, s_time_buffer, font, text_rect,
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
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

static void animate_layer_to(Layer *layer, GRect to, uint32_t delay, uint32_t duration);

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *temp_tuple = dict_find(iter, MESSAGE_KEY_TEMPERATURE);
  if (temp_tuple) {
    snprintf(s_weather_buffer, sizeof(s_weather_buffer), "%d°",
             (int)temp_tuple->value->int32);
    layer_mark_dirty(s_weather_layer);
    if (!s_weather_animated) {
      s_weather_animated = true;
      animate_layer_to(s_weather_layer, s_weather_target_frame, 0, 400);
    }
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
}

static void weather_widget_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GSize text_size = graphics_text_layout_get_content_size(
      s_weather_buffer, font, bounds,
      GTextOverflowModeWordWrap, GTextAlignmentLeft);

  const int total_w = ICON_SIZE + ICON_GAP + text_size.w;
  const int start_x = bounds.origin.x + (bounds.size.w - total_w) / 2;

  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, s_weather_bmp,
      GRect(start_x, bounds.origin.y, ICON_SIZE, ICON_SIZE));

  graphics_context_set_text_color(ctx, s_color_weather);
  graphics_draw_text(ctx, s_weather_buffer, font,
      GRect(start_x + ICON_SIZE + ICON_GAP, bounds.origin.y,
            text_size.w + 4, bounds.size.h),
      GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
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

  s_weather_bmp = load_themed_icon(s_theme, RESOURCE_ID_ICON_CLOUD_DARK, RESOURCE_ID_ICON_CLOUD_LIGHT);
  s_steps_bmp   = load_themed_icon(s_theme, RESOURCE_ID_ICON_STEPS_DARK, RESOURCE_ID_ICON_STEPS_LIGHT);
  if (s_hr_available) {
    s_hr_bmp = load_themed_icon(s_theme, RESOURCE_ID_ICON_HEART_DARK, RESOURCE_ID_ICON_HEART_LIGHT);
  }

  // Two columns inside `content`: a fixed-width widget column on the left
  // (icon-sized) and the time/date column filling whatever's left on the
  // right. Edge-anchored so the layout stretches with the screen.
  const int16_t widget_h = 28;
  const int16_t date_h = 22;
  const int16_t intra_gap = 2;
  const int16_t stack_h = ICON_SIZE + intra_gap + widget_h;
  const int16_t left_col_w = 60;
  const int16_t col_gap = 6;
  const int16_t left_x = content.origin.x;
  const int16_t right_x = left_x + left_col_w + col_gap;
  const int16_t right_col_w = content.origin.x + content.size.w - right_x;
  const int16_t stack_icon_x = left_x + (left_col_w - ICON_SIZE) / 2;

  // Time pill — vertically centered in the content rect.
  const int16_t box_h = TIME_BOX_H;
  GRect time_rect = GRect(right_x,
                          content.origin.y + (content.size.h - box_h) / 2,
                          right_col_w,
                          box_h);
  s_time_box_layer = layer_create(time_rect);
  layer_set_update_proc(s_time_box_layer, time_box_update_proc);
  layer_add_child(root, s_time_box_layer);

  // Date — directly under the time pill.
  GRect date_rect = GRect(time_rect.origin.x,
                          time_rect.origin.y + box_h + 2,
                          time_rect.size.w,
                          date_h);
  s_date_layer = make_text_layer(date_rect, GTextAlignmentCenter, FONT_KEY_GOTHIC_18_BOLD, s_color_time);
  layer_add_child(root, text_layer_get_layer(s_date_layer));

  // Vertical positioning of the left widget column. Group is centered in
  // `content`. Gap between Steps and HR adapts to the available height so
  // small displays don't push Steps off the top.
  int16_t widget_gap = 0;
  if (s_hr_available) {
    int16_t free_h = content.size.h - 2 * stack_h;
    widget_gap = free_h / 2;
    if (widget_gap < 20) widget_gap = 20;
    if (widget_gap > 50) widget_gap = 50;
  }
  const int16_t group_h = s_hr_available ? (2 * stack_h + widget_gap) : stack_h;
  const int16_t steps_y = content.origin.y + (content.size.h - group_h) / 2;
  const int16_t hr_y = steps_y + stack_h + widget_gap;

  // Weather — anchored just above the time pill.
  s_weather_target_frame = GRect(time_rect.origin.x,
                                 time_rect.origin.y - widget_h - 6,
                                 time_rect.size.w,
                                 widget_h);
  GRect weather_off = GRect(time_rect.origin.x, -widget_h - 4,
                            time_rect.size.w, widget_h);
  strcpy(s_weather_buffer, "...");
  s_weather_animated = false;
  s_weather_layer = layer_create(weather_off);
  layer_set_update_proc(s_weather_layer, weather_widget_update_proc);
  layer_add_child(root, s_weather_layer);

  s_steps_bmp_layer = make_icon_layer(GRect(stack_icon_x, steps_y, ICON_SIZE, ICON_SIZE), s_steps_bmp);
  layer_add_child(root, bitmap_layer_get_layer(s_steps_bmp_layer));
  s_steps_layer = make_text_layer(GRect(left_x, steps_y + ICON_SIZE + intra_gap, left_col_w, widget_h), GTextAlignmentCenter, FONT_KEY_GOTHIC_24_BOLD, s_color_steps);
  layer_add_child(root, text_layer_get_layer(s_steps_layer));

  const int16_t progress_pad = 8;
  const int16_t stroke_allowance = 2;
  const int16_t total_pad = progress_pad + stroke_allowance;
  GRect steps_progress_rect = GRect(left_x - total_pad,
                                    steps_y - total_pad,
                                    left_col_w + 2 * total_pad,
                                    stack_h + 2 * total_pad);
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
  layer_destroy(s_time_box_layer);
  layer_destroy(s_weather_layer);
  layer_destroy(s_steps_progress_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_steps_layer);
  bitmap_layer_destroy(s_steps_bmp_layer);
  gbitmap_destroy(s_weather_bmp);
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

  s_main_window = window_create();
  window_set_background_color(s_main_window, s_color_bg);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  health_service_events_subscribe(health_handler, NULL);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(app_message_inbox_size_maximum(),
                   app_message_outbox_size_maximum());

  request_weather();
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  health_service_events_unsubscribe();
  window_destroy(s_main_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
