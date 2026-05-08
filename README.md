# pebble-wear-face

A Pebble watchface written in C with a PebbleKit JS companion. Shows a
horizontal `HH:MM` clock baselined at the bottom of the screen with the date
on the right end, a weather widget pinned near the top, and Steps + Heart
Rate widgets stacked in the middle band on the left. Configurable via Clay
(step goal, dark/light theme, second-hand animation).

## Layout

```
   +-----------------------+
   |    [cloud] 72°        |     <- weather pinned near the top, full width
   |                       |
   |  ( [steps] )          |
   |     8,432             |     <- steps + HR column on the left, centered
   |                       |        in the middle band between weather and
   |   [heart]             |        the bottom time row
   |    72                 |
   |                       |
   |  12:34       6 May    |     <- bottom row: time on the left, date on
   +-----------------------+        the right, baselined at content bottom
```

- The bottom row is anchored to `content.bottom`. Time is left-aligned at
  `content.left` with Roboto Bold Subset 49 (~56px line height); date is
  right-aligned at `content.right` with Gothic 18 Bold inside a fixed 80px
  rect.
- A solid background fills behind the bottom row, edge-to-edge across the
  full screen width (bleeding past `content` so it covers the curved corners
  on round). Both the fill and the time/date text colors are independent
  Clay color-picker settings (`BOTTOM_BG_COLOR`, `BOTTOM_FG_COLOR`) — they
  do not follow the dark/light Theme.
- Weather is pinned 4px below `content.top`, anchored in the right column
  (mirror of the steps/HR left column across the centerline) and centered
  inside that rect.
- Steps + HR are vertically centered in the middle band between the weather
  row (top) and the time row (bottom), with `widget_gap` (clamped 4–50px)
  between them. If the group is taller than the band, HR's bottom anchors
  to the band edge so any overflow goes upward into the weather area
  instead of colliding with the time row below.
- The Steps widget has a circular progress border around its icon only: a
  1px gray "track" circle is always visible (when a step goal is set), and
  the filled portion is drawn 5px thick over the track, sweeping the
  circumference clockwise from the top via `graphics_draw_arc`. The step
  count sits 4px below the circle.

## Features

### Time
- Horizontal `HH:MM` rendered in `FONT_KEY_ROBOTO_BOLD_SUBSET_49` (digits +
  colon), left-aligned at the bottom-left of `content`. 24h vs 12h follows
  the system `clock_is_24h_style()`. Sits inside the bottom bar — text color
  is driven by `BOTTOM_FG_COLOR` (Clay color picker), independent of the
  Theme setting.
- Updates on `MINUTE_UNIT` ticks via `text_layer_set_text`.
- The bordered "second-progress" frame around the time is **disabled for
  now**. The Clay toggle and persistence still work, but `apply_animate_seconds`
  no longer schedules the perimeter animation. The infrastructure
  (`s_border_anim`, `start_border_animation`, `draw_rounded_rect_border`)
  is left in place as dead code so it can be re-wired later.

### Date
- Right-end of the bottom row, sharing its baseline with the time. Format
  `"%-d %b"` (e.g. `6 May`).
- Right-aligned, Gothic 18 Bold, inside a fixed 80px rect anchored to
  `content.right`. Same `BOTTOM_FG_COLOR` as the time.

### Weather
- Cloud icon + temperature (e.g. `72°`), centered as a unit above the time
  pill via a custom `Layer` whose `update_proc` measures text width with
  `graphics_text_layout_get_content_size` and recenters every refresh.
- Phone-side fetch from [Open-Meteo](https://open-meteo.com) (no API key
  needed) using `navigator.geolocation`. Triggered once on `Pebble.ready`,
  again whenever the C side sends `REQUEST_WEATHER` (every 30 minutes from
  the tick handler).
- Conditions string is sent over AppMessage but currently unused on the
  watchface; only the temperature is displayed.

### Steps
- Pulled from `health_service_sum_today(HealthMetricStepCount)` on every
  `HealthEventMovementUpdate`.
- Formatted with thousands separator (e.g. `8,432`).
- Circular progress border around the icon only, drawn as two semicircle
  arcs via `graphics_draw_arc`. Sweeps from 0% (top of the circle) to 100%
  (full circumference) clockwise. The step count sits 4px below the circle.
- Goal default is `10000`; configurable via Clay (see Settings).

### Heart rate
- Pulled from `health_service_peek_current_value(HealthMetricHeartRateBPM)`
  on `HealthEventHeartRateUpdate`. Displays `--` when no reading is
  available.
- Heart-rate sensors only exist on Pebble Time 2 (emery). On other targets
  the widget will read `--` indefinitely.

### Themes (dark / light)
- **Dark mode** (default): black bg, chrome yellow time/date, picton blue
  weather, bright green steps, red HR.
- **Light mode**: white bg, black time/date, plus *darker variants* of the
  accent colors so they read on white — `BlueMoon`, `IslamicGreen`,
  `DarkCandyAppleRed`.
- Each icon ships in two pre-rendered PNG variants (`*_dark.png` /
  `*_light.png`) that are loaded from the right resource based on theme. The
  C `apply_theme()` swaps both bitmaps and text colors live without
  reloading the watchface.

### Entrance animations
- On `main_window_load`, the steps and HR widgets cascade in from the left
  via `property_animation_create_layer_frame` with `AnimationCurveEaseOut`:
  steps stack at +200 ms delay, HR stack at +300 ms (each 400 ms).
- The weather widget is created off-screen (above the screen) and stays
  hidden until the first `TEMPERATURE` AppMessage arrives — at which point
  it drops down into place (400 ms `EaseOut`). Subsequent weather refreshes
  just update the temperature in place; the slide-in only fires once per
  watchface load (gated by `s_weather_animated`).
- Time and date are static. The "second-hand" perimeter animation is
  disabled for now (see the Time section).
- Helpers: `slide_in_from(layer, from_origin, delay, duration)` snaps the
  layer to the off-screen origin then animates back to its current frame;
  `animate_layer_to(layer, to, delay, duration)` animates from the current
  frame to an explicit target. Both schedule auto-destroyed
  `PropertyAnimation`s.

## Settings (Clay)

Tap the gear icon next to the watchface in the official Pebble app to open
the Clay-rendered configuration page. Fields:

| Field | Type | Effect |
|---|---|---|
| Theme | Radio (Dark / Light) | Switches the rest of the watchface (weather/steps/HR/bg) — does not touch the bottom bar |
| Animate seconds | Toggle | (currently a no-op — second-hand rendering is disabled; the setting is still persisted) |
| Bottom bar background color | Native color picker | Fills the bottom strip behind the time + date (default black) |
| Bottom bar text color | Native color picker | Color of the time and date text (default white) |
| Daily step goal | Number input (step 500) | Sets the circumference the progress circle fills against |

Values are persisted by Clay in phone-side `localStorage` (so the form
remembers what you set last) and on the watch via `persist_write_int` (so
they survive reboots).

## Build & install

```sh
# Install JS deps (Clay). Use a sandbox-writable cache dir:
npm install --cache="$TMPDIR/npm-cache"

# Build for all 5 target platforms
pebble build

# Install on an emulator
pebble install --emulator basalt   # or aplite, chalk, diorite, emery

# Install on a paired phone
pebble install --phone <ip>

# Tail logs
pebble logs --emulator basalt
```

### npm-cache caveat

The default npm cache (`~/.npm`) is often blocked by sandboxing or has stale
permissions on macOS. Routing the cache through `$TMPDIR/npm-cache` avoids
both.

### Target platforms

Limited to **aplite, basalt, chalk, diorite, emery** — Clay 1.0.4 (the
latest published version on npm) doesn't ship binaries for the newer
`flint` (Pebble 2 Duo) or `gabbro` (Pebble Round 2) platforms. If you need
those, you'd have to drop Clay and hand-roll the configuration page (the
project did this earlier — see git history for the inline-HTML approach).

### Color caveat

Pebble's color hardware is 6-bit (4 levels per RGB channel = 64 colors).
All chosen accent colors are picked from Pebble's named palette so they
quantize cleanly. On aplite/diorite (1-bit B&W) everything dithers toward
black or white; the layout still reads but the theming and accent colors
collapse.

## Project layout

```
src/c/pebble-wear-face.c   Watchface implementation
src/pkjs/index.js          PebbleKit JS: weather fetch + Clay bootstrap
src/pkjs/config.js         Clay schema (Theme, Animate seconds, Bottom bar colors, Daily step goal)
resources/icons/           Pre-rendered 28x28 PNGs (dark & light variants)
package.json               Project metadata, message keys, resources, deps
wscript                    Pebble waf build rules
```

### Icon generation

Icons come from
[pebble-dev/iconography](https://github.com/pebble-dev/iconography) (cloned
locally, not vendored). The repo's SVGs are designed for white fill +
black stroke; for this watchface they're recolored on render via a small
Python pipeline (`resvg-py` for SVG → PNG, `Pillow` for resize) using
`uv run --with` so no permanent Python deps are needed.

To re-render with different colors, modify the `(svg, output, color)`
tuples in the snippet and run via `uv`. Output goes to
`resources/icons/<name>_<theme>.png`.

## Message keys

Defined in `package.json` under `pebble.messageKeys`; the SDK turns each
into a `MESSAGE_KEY_*` runtime symbol on the C side and an AppMessage key
on the JS side.

| Key | Direction | Type | Purpose |
|---|---|---|---|
| `REQUEST_WEATHER` | C → JS | uint8 (1) | Asks JS to refetch weather |
| `TEMPERATURE` | JS → C | int32 | Latest temperature in °F |
| `CONDITIONS` | JS → C | cstring | Short condition label (Clear/Cloudy/...) |
| `STEP_GOAL` | JS → C | cstring | Daily step goal (parsed via `atoi`) |
| `THEME` | JS → C | cstring | `"0"` = dark, `"1"` = light |
| `ANIMATE_SECONDS` | JS → C | bool | Enables the time-box border second-hand animation |
| `BOTTOM_BG_COLOR` | JS → C | int32 | 0xRRGGBB hex — bottom bar fill color (`GColorFromHEX`) |
| `BOTTOM_FG_COLOR` | JS → C | int32 | 0xRRGGBB hex — bottom bar text color (`GColorFromHEX`) |

Clay sends form values as strings; the C side branches on
`tuple->type == TUPLE_CSTRING` and falls back to `int32` for safety.

## SDK reference

Pebble SDK docs and tutorials live at
<https://developer.repebble.com>.
