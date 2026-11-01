# Android port

This branch (`port/android`) carries the Android port of the playable
reconstruction. It extends the `port/portable-64bit` line and adds no
reconstruction state of its own: the exact-match ledgers, the VC7 build, and
the evidence workflow are untouched by the port.

## Status

| Piece | State | Verified by |
| --- | --- | --- |
| Virtual gamepad (`src/modern/touch`) | Done | `scripts/ci-touch-controls.sh` (57 host checks) |
| CPU Direct3D 8 back end (`src/modern/software`) | Done | `scripts/ci-software-renderer.sh` (51 host checks) |
| Input bridge to the game's keyboard paths | Done | `scripts/ci-touch-input.sh` (316 host checks) |
| SDL presenter, overlay, settings plumbing | Done | `scripts/ci-software-present.sh` (287 headless checks) |
| Whole game links against the CPU back end on host | Done | `scripts/ci-software-build.sh` |
| Gradle/Java scaffolding, CMake `ANDROID` branch | Done | Debug APK builds with NDK r28 for both ABIs; `apksigner verify` passes |
| On-device gameplay | Not started | Needs original game data on a device |

The native side is verified on a Linux host through the same code paths the
phone runs, using SDL's dummy video driver where a display is not needed. The
debug APK itself now builds on a machine with the Android SDK: NDK r28
compiles `arm64-v8a`, `armeabi-v7a` and `x86_64`, and `apksigner verify`
accepts the
result. On-device gameplay has not been tested yet, so runtime behaviour on a
phone remains the open item below the "Building the APK" line.

## Architecture

```
SetupActivity (Java, first run)
  └─ picks the original game folder (Storage Access Framework)
  └─ writes th08_data_dir.txt + th08_touch_settings.ini into internal storage
MainActivity extends SDLActivity (Java)
  └─ loads libSDL2.so + libth08-modern.so, passes --data-dir
th08-modern.so (C++, the reconstructed game)
  ├─ src/modern/linux/linux_runtime.cpp   main/SDL_main, data dir, crash file
  ├─ src/modern/linux/linux_compat.cpp    Win32/DirectInput/DirectSound over SDL
  ├─ src/modern/software/software_d3d8.cpp CPU Direct3D 8 implementation
  ├─ src/modern/software/sdl_present.cpp  frame upload, overlay, touch routing
  └─ src/modern/touch/touch_controls.cpp  virtual gamepad (platform neutral)
```

### Why a CPU renderer

The desktop port presents through `d3d8_compat.cpp`, which is written against
desktop OpenGL (`glBegin`, fixed-function fog, texenv). Android's GLES has
none of that, and rewriting the renderer in GLES would create a second,
device-only rendering path that cannot be checked on this repository's hosts.
The CPU back end in `src/modern/software` renders every supported format and
texture stage in plain C++, so the same rasterizer that passes the host checks
is the one that runs on the phone. Frames are uploaded to an `SDL_Renderer`
streaming texture with nearest-neighbor scaling and letterboxing; the overlay
uses only SDL points and rectangles.

The game creates its Direct3D interface before it creates its window, matching
the original where that call depends on no window, while this back end binds
its presenter together with the window. The device therefore resolves the
presenter at creation time, and the window is created with `SDL_WINDOW_OPENGL`
on Android so the GLES renderer that performs the final blit can attach
(Android's SDL has no window-surface path for a software renderer).

Performance is the open question, not correctness: the game renders 384×448 at
60 Hz, and the adapter sources are compiled at `-O2` while the reconstructed
game translation units keep their original `-O0` boundaries.

### Input

The game reads input through two Win32 paths: DirectInput scan codes and
`GetKeyboardState` virtual keys. The touch layer writes both (see
`src/modern/software/sdl_present.cpp` and `tests/touch-input.cpp`), so the
reconstructed input code needed no change. Finger events are removed from
SDL's queue by range, leaving `SDL_QUIT` and the focus events to the game's
own message pump.

Default layout, landscape: movement stick on the left half (floating by
default), Z/X cluster bottom right, Shift (focus) above it, Enter and Escape
as small buttons in the top right. Every geometry and behaviour option lives
in `touch::Settings` and is mirrored one-to-one by the setup screen.

Keyboard and controller input works alongside the touch layer. The overlay
hides itself as soon as a keyboard key or a controller/joystick button is
pressed, so a connected pad drives the game with a clean screen; the next
touch brings the buttons back, and that revealing touch is swallowed so it
cannot press a control by accident. The cycle repeats for the rest of the
session: touch to show, physical input to hide. Detection compares the live
SDL scancode state against the previous frame and additionally peeks queued
`SDL_KEYDOWN`, `SDL_CONTROLLERBUTTONDOWN` and `SDL_JOYBUTTONDOWN` events
without consuming them, so the game's own message pump still sees them.

## Building the APK

Requirements: Android SDK (platform 35), NDK 28.x, CMake 3.22.1 through the
SDK manager, JDK 17, Gradle 8.9+, Python 3 (for the i18n header).

```sh
scripts/build-android.sh          # debug APK
scripts/build-android.sh release  # release APK (needs a signing config)
```

The script fetches SDL2, SDL2_image, SDL2_ttf and FreeType at pinned tags into
`3rdparty/` (see `3rdparty/README.md`; nothing is committed), then Gradle
drives the `ANDROID` branch of the repository's `CMakeLists.txt` for
`arm64-v8a`, `armeabi-v7a` and `x86_64` (the last one for emulators and
x86_64 devices). The APK lands in `android/app/build/outputs/apk/`.

The launcher icon is generated from `resources/modern-icon.png`, the same
artwork the desktop builds use, scaled by ImageMagick into the density
buckets under `android/app/src/main/res/mipmap-*`.

All three ABIs build with `TH08_PORTABLE_NATIVE_LAYOUT`: the original
fixed-address 32-bit layout needs a non-PIE executable, which Android never
provides, so the symbolic layout that the portable-64bit line validates on
64-bit hosts is used everywhere.

## First-run data flow

1. `SetupActivity` opens `ACTION_OPEN_DOCUMENT_TREE`.
2. `th08.dat`, `thbgm.dat` and any `.ttf`/`.otf` next to them are copied from
   the picked tree into `getFilesDir()/game` — always, never used in place.
   Android's scoped storage lets the document grant read a shared folder while
   rejecting the plain-path writes the game performs for `th08.cfg`, its saves
   and its replays, and a shared folder may carry a foreign `th08.cfg` with
   attributes that break the read/write round trip. The app-private directory
   is the one location that is readable and writable on every device. The copy
   is staged in `getFilesDir()/game.new` and swapped into place, so an
   interrupted copy never destroys a working one. The screen shows the stage
   (listing, then copying) and a byte-accurate progress bar with percentage;
   every failure — a folder without `th08.dat` at its top level, an unreadable
   provider listing, a copy error — keeps its own message on screen instead of
   silently reverting to the idle status.
3. A data directory stored by an older build outside `getFilesDir()/game` is
   cleared on startup and the screen asks for one more pick; that is the
   migration from the former run-in-place layout.
4. The resolved absolute path is stored in `SharedPreferences` and written to
   `getFilesDir()/th08_data_dir.txt`; the native runtime reads the file on
   every launch (`--data-dir` on the command line takes precedence).
5. Natively, `ConfigureDataDirectory` verifies the working directory with a
   create/delete probe right after entering it. A directory that can be
   entered and read but not written logs its reason to stderr (visible in
   `adb logcat`) and falls back to `getFilesDir()/game` before the game opens
   anything; failing both routes aborts with a log line instead of a blank
   screen later.
6. Control settings are written to `getFilesDir()/th08_touch_settings.ini`,
   the same key=value format the host tests exercise.

The original game data is never distributed, never modified, and never leaves
the device; the copy is created from the user's own files.

Both activities are locked to landscape. A sensor-landscape request was
honoured as portrait on at least one Transsion/Unisoc device, rotating the
surface mid-startup, so `MainActivity` also overrides SDL's
`setOrientationBis` to request plain landscape outright, and keeps the
system bars hidden with an immersive-sticky flag so the game uses the whole
panel.

Display mode: the setup screen offers *Full screen* (default; the 4:3 game
picture is stretched over the whole panel) and *Original 4:3* (aspect
preserved, black side bars). The choice is stored as `display_mode` in
`th08_touch_settings.ini` and read by the SDL presenter.

### Window title

The game sets its window title as Shift-JIS text (`東方永夜抄 …`). SDL's
Android backend passes window titles to JNI `NewStringUTF`, which aborts the
whole process on bytes that are not valid Modified UTF-8, so
`CreateWindowExA` folds the title to ASCII — one `?` per Shift-JIS character —
before SDL ever sees it. Android shows no title bar, so nothing visible is
lost; `MessageBoxA` already went to stderr only and needed no treatment.

### Diagnostics

The setup screen has an **Export logs** button. It bundles the app's own
logcat lines (the `th08-modern:` stderr diagnostics; an app can only ever
read its own entries), the game's `log.txt` decoded from Shift-JIS,
`modern-files.txt`, `modern-crash.txt` and the stored data directory and
touch settings into `th08-port-logs-<timestamp>.txt`. The bundle also carries
the heartbeat the presenter rewrites once a second (`modern-heartbeat.txt`:
frame count, observed frame rate, process RSS/peak memory and oom score) plus
`modern-files.txt`, which timestamps every game file open. A session that ends
without `status=closed` leaves its last heartbeat saying `status=running`,
which the next launch preserves as `modern-heartbeat-prev.txt` — together the
two distinguish a low-memory kill, a clean exit and a hang. On API 29+ the file
lands in the public `Download/` collection through MediaStore — no storage
permission — and the share sheet opens on top; on API 28 it goes to the
app's external files directory, reachable through a file manager or USB.

### Text rendering

The desktop build finds a Japanese font through fontconfig, which Android does
not have. On Android the native side looks for `font/vlgothic.ttf` (or
`vlgothic.ttf`) next to the game data first, then for the system CJK fonts
(`NotoSansCJK-Regular.ttc` and friends). Shipping a font next to the archives
is the reliable option on devices without a CJK system font.

## Known limitations

- Two on-device boot failures have been reproduced from user logs and fixed
  (the Direct3D-before-window ordering and the shared-storage config write);
  gameplay, audio latency and CPU renderer performance remain unmeasured.
- Audio uses the same SDL audio path as the desktop ports; device latency and
  the music (`thbgm.dat`) OGG decoding are unmeasured on Android.
- The `vibration_on_bomb` setting is parsed and persisted, but no haptic
  output is wired yet.
- The setup screen offers stick mode, handedness, size, opacity and dead
  zone; per-button remapping is future work.
- The CPU renderer's phone performance is unknown; if 60 Hz is not reachable,
  the fallback is frame skipping in the presenter, not a GLES rewrite.

## Evidence note

Nothing on this page is a reconstruction claim. Exact-match status is
published only from reproducible reccmp comparisons against the canonical
`resources/th08.exe`, and the port changes none of it.
