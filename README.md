# modplayer — skinnable tracker module player (SDL3 / C++)

`build/modplayer` is a small music player for tracker modules — Amiga
ProTracker **MOD**, FastTracker II **XM**, Scream Tracker 3 **S3M** and
Impulse Tracker **IT** — with eleven switchable skins. It scans a folder of
module files, reads the song name from each header and lets you browse by
**Artists / Albums / Songs / Years** (modules group under their format and
channel count in Albums); the Now Playing screen shows **folder cover art**
when an image sits next to the modules (JPEG/PNG/BMP, decoded with the
vendored `stb_image.h`).

```bash
./build/modplayer [mods-folder]      # default: ~/Music, else assets/sounds
./build/modplayer --skin winamp      # pick a skin; F2 cycles them live
```

## Build & run

Requires **SDL3** and a C++17 compiler. `build.sh` configures (first run) and
builds via CMake:

```bash
./build.sh                # or: cmake -B build && cmake --build build
./build/modplayer
```

### Installing SDL3

- **Ubuntu/Debian:** `sudo apt install libsdl3-dev` (or build from source).
- **macOS:** `brew install sdl3`
- **From source:** https://github.com/libsdl-org/SDL

## Skins

Eleven skins, all cycled live with **F2** or picked with `--skin <name>`:

- **`nano`** (default) — classic Apple iPod nano: portrait body with a
  **working click wheel** (drag the ring to scroll, click MENU / |<< / >>| /
  play-pause / center).
- **`classic`** — Apple iPod Classic: anodized-gray body, the landscape
  **split screen** (menu left, cover art / library stats right) and the big
  chrome-ring click wheel; same wheel interaction as the nano.
- **`winamp`** — Winamp-classic layout: green LCD digits (blink when
  paused), scrolling title marquee, **live spectrum analyzer** fed from the
  rendered PCM, volume + seek sliders, transport buttons, and a playlist pane
  that doubles as the library browser (shows the play queue on Now Playing;
  `<` in its header goes back).
- **`foobar`** — foobar2000: Win32 gray chrome, menu bar, toolbar with
  transport + seekbar + volume, a **columned playlist**
  (Title | Artist | Album, alternating row tint), and a status bar with the
  stream format. Single click selects, **double click plays**; Now Playing
  shows the queue with the current row marked.
- **`zune`** — Zune Metro: flat black, **oversized lowercase type** that
  bleeds off the edge, magenta accent, full-bleed square art and a hairline
  progress bar (tap it to seek, tap the art to pause).
- **`cassette`** — an animated compact cassette: the **reels spin** while
  playing and the tape pack winds from the left hub to the right as the
  track advances; tags go on the paper label. Click the reels for
  prev/next, the shell for play/pause, the counter strip to seek.
- **`vinyl`** — a turntable: the record **spins with the cover art as its
  label**, grooves and sheen included, and the **tonearm tracks progress**
  from lead-in to run-out (it rests off the platter when idle). Click the
  platter for play/pause, the brass bar to seek.
- **`car`** — a 90s CD head unit: **amber VFD** with one big scrolling text
  line, a 7-segment position clock, tick-strip progress and a row of chunky
  faceplate buttons (SRC = back, arrows scroll, SEL selects). Shallow and
  wide, so it suits automotive-ish displays.
- **`term`** — a terminal player in the cmus mould: green-on-black
  monospace, `>` cursor, ASCII `[====----]` progress bar and a blinking
  status line. Tap the bar line to seek.
- **`mini`** — a neutral, dark **320x240 landscape** UI intended for a
  Raspberry Pi or any small/touch display: single-tap rows and a big-button
  toolbar (vol−, prev, play/pause, next, vol+). For the Pi console run:

  ```bash
  ./build/modplayer --skin mini --fullscreen /path/to/mods
  ```

  (Under X11/Wayland it opens a 640x480 window; on the bare console SDL3
  uses KMSDRM and the 320x240 canvas letterboxes to the display.)
- **`tracker`** — the FastTracker II / Scream Tracker 3 mould: gray-blue
  beveled panels, sunken order/pattern/row/speed/BPM readouts, **one
  oscilloscope per module channel** and the **pattern editor** with the
  note rows scrolling under a fixed center bar as the song plays
  (note / instrument / volume / effect columns colored FT2-style). While a
  module plays the right panel lists its instrument or sample names —
  where demoscene greetings traditionally live. Pattern data and
  per-channel signals need **libopenmpt**; on the pocketmod fallback the
  scopes collapse to one mixed-signal scope.

## Modules, tags & art

- Audio: `.mod` / `.xm` / `.s3m` / `.it` modules, rendered once through to
  PCM at load — a module's pattern order can loop forever, so "duration" is
  one full pass of the song. Rendering uses the system **libopenmpt**
  (loaded with `dlopen`, so it is a runtime — not build — dependency);
  without it the vendored single-header **`pocketmod.h`** still plays
  ProTracker MODs and only XM/S3M/IT files fail to load. Everything plays
  through an `SDL_AudioStream`; if no audio device is available the UI
  still works (and headless tests can run).
- Tags: the song name in the module header becomes the title (folded from
  Latin-1-ish tracker charsets to the built-in 8x8 font, so accented names
  render fine); modules with a blank name fall back to the file name.
  Modules carry no artist or year, so those group under "Unknown Artist";
  the Albums view groups modules by format ("4-channel MOD", "16-channel
  XM", "8-channel S3M", "Impulse Tracker IT"…).
- Cover art: a **folder image** next to the module —
  `cover`/`folder`/`front`/`album`/`art`.(png|jpg|jpeg|bmp) in any case, or
  the folder's only image file.

## Click wheel (mouse)

Drag around the ring to scroll (in Now Playing it sets the volume). Click:
center = select, **MENU** = back, `|<<` / `>>|` = previous / next track,
`> ||` = play/pause. The mouse wheel scrolls too.

## Keyboard

| Key                    | Action                                    |
| ---------------------- | ----------------------------------------- |
| Up / Down              | Scroll menus (Now Playing: volume)        |
| Return / Right         | Select                                    |
| Left / Esc / Backspace | Back (Now Playing: Left/Right seek ±5 s)  |
| Space                  | Play / pause                              |
| N / `.` — P / `,`      | Next / previous track                     |
| F2                     | Cycle through all ten skins               |
| Q                      | Quit                                      |

## Headless tests

```bash
./build/modplayer --selftest [dir]   # print the parsed library and exit
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
  ./build/modplayer --skin winamp --shot out [dir]  # screens → out-winamp-*.bmp
```

## Layout

- `src/` — the player: `main.cpp` (modes/CLI), `core.*` (UI framework +
  menus), `skin_*.cpp` (the skins), `library.*` (folder scan + grouping),
  `tags.*` (module header parsing + folder art), `playback.*` (module
  rendering via libopenmpt/pocketmod + SDL audio), plus the vendored
  single-header libraries `pocketmod.h`, `stb_image.h`.
- `assets/sounds/` — small demo module library used when no folder is given
  and `~/Music` doesn't exist.
- `CMakeLists.txt` / `build.sh` — build entry points.
