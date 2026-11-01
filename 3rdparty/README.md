# Vendored third-party sources

Nothing in this directory is committed. `scripts/build-android.sh` clones the
dependencies below at pinned tags before invoking Gradle, so the repository
never carries downloaded toolchains or third-party trees.

| Directory       | Upstream                                          | Pinned tag     | Why                                                                 |
|-----------------|---------------------------------------------------|----------------|---------------------------------------------------------------------|
| `SDL2/`         | https://github.com/libsdl-org/SDL                 | `release-2.32.8`   | Window, input, audio and the Android activity bootstrap          |
| `SDL2_image/`   | https://github.com/libsdl-org/SDL_image           | `release-2.8.8`   | Image loading used by the D3DX compatibility layer                |
| `SDL2_ttf/`     | https://github.com/libsdl-org/SDL_ttf             | `release-2.24.0`  | Text rendering for the GDI emulation                              |
| `freetype/`     | https://gitlab.freedesktop.org/freetype/freetype  | `VER-2-13-3`     | Font metrics measured directly by `linux_compat.cpp`              |

The tags can be overridden for an experiment, for example
`SDL2_REF=release-2.32.10 scripts/build-android.sh`. The desktop builds use
the system packages instead and never look at this directory.

The licenses of these projects are not redistributed by this repository; they
ship inside the fetched trees and inside the built APK's notices as required
by each project.
