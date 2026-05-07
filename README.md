# pebble-wear-face

A Pebble watchface written in C with a PebbleKit JS companion. Shows the time
in a pill-shaped frame with a date caption, plus a weather widget above the
time and a vertically-stacked column of Steps and Heart Rate widgets to the
left of center. Configurable via Clay (step goal, dark/light theme).

## Layout

```
                  +------------+
   [cloud icon]   |   weather  |     <- weather centered above the time pill
   [   72°    ]   +------------+
                  |            |
   ( [steps] )    |     12     |     <- circle around steps icon only;
      8,432       |     34     |        time HH stacked over MM
                  |            |        with animated rounded-rect border
                  +------------+
                  |    6 May   |     <- date caption directly under the pill
                  |            |
    [heart]
     72
```

- The Steps and HR columns sit `center_pad = 12px` left of the screen's
  vertical centerline. The time/date column hugs `center_pad` to the right of
  it.
- Steps and HR are vertical "icon over value" stacks, vertically centered as
  a group with `widget_gap` (clamped 8–50px) between them. The Steps stack is
  taller than the HR stack because its icon sits inside a circular progress
  badge with the value rendered 4px below.
- The Steps widget has a circular progress border around its icon only: a
  1px gray "track" circle is always visible (when a step goal is set), and
  the filled portion is drawn 5px thick over the track, sweeping the
  circumference clockwise from the top via `graphics_draw_arc`.

## Features

### Time
- Rounded-rect frame, 3px outline rendered via stroked arcs and lines so the
  perimeter can animate around the box once per minute as a "second hand"
  (toggleable via Clay).
- `FONT_KEY_ROBOTO_BOLD_SUBSET_49` numerals, hour stacked tight over minutes
  (no gap between cap heights), both center-aligned, with 4px of symmetric
  padding above the hour and below the minute inside the box. 24h vs 12h
  follows the system `clock_is_24h_style()`.
- Updates on `MINUTE_UNIT` ticks; the border animation re-arms each tick.

### Date
- Caption directly under the time pill, format `"%-d %b"` (e.g. `6 May`).
- Center-aligned, 18px bold.

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
- Time numerals and date are static; the time pill border animates around
  its perimeter once per minute as a "second hand" via a custom
  `AnimationImplementation` driving `s_border_progress`. Disabling
  "Animate seconds" in Clay leaves the border drawn at 100%.
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
| Theme | Radio (Dark / Light) | Switches the entire color scheme live |
| Animate seconds | Toggle | Traces the time-box border once per minute as a second hand |
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
src/pkjs/config.js         Clay schema (Theme, Daily step goal)
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

Clay sends form values as strings; the C side branches on
`tuple->type == TUPLE_CSTRING` and falls back to `int32` for safety.

## SDK reference

Pebble SDK docs and tutorials live at
<https://developer.repebble.com>.
