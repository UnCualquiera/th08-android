#!/bin/sh
# Builds the Android APK.
#
# The vendored third-party trees are fetched at pinned tags on first run (see
# 3rdparty/README.md), then Gradle drives the CMake build in the repository
# root for both ABIs. The result is android/app/build/outputs/apk/<variant>/.
#
# Requirements:
#   - Android SDK with platform 35 and build-tools (ANDROID_HOME set)
#   - Android NDK 27.x (installed through the SDK manager)
#   - CMake 3.22+ available to the SDK (sdkmanager "cmake;3.22.1")
#   - JDK 17 and Gradle 8.9+ on PATH
#   - Python 3 (the CMake build generates the i18n header with it)
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

SDL2_REF=${SDL2_REF:-release-2.32.8}
SDL2_IMAGE_REF=${SDL2_IMAGE_REF:-release-2.8.8}
SDL2_TTF_REF=${SDL2_TTF_REF:-release-2.24.0}
FREETYPE_REF=${FREETYPE_REF:-VER-2-13-3}

fetch()
{
    name=$1
    url=$2
    tag=$3
    destination="$root/3rdparty/$name"
    if [ -d "$destination/.git" ]; then
        current=$(git -C "$destination" describe --tags --exact-match 2>/dev/null || echo unknown)
        if [ "$current" = "$tag" ]; then
            return 0
        fi
        echo "build-android: $name is at $current, re-fetching $tag"
        rm -rf "$destination"
    fi
    echo "build-android: fetching $name at $tag"
    # SDL2_image and SDL2_ttf keep their codec and font dependencies in
    # submodules, which the vendored CMake builds need.
    git clone --quiet --depth 1 --branch "$tag" --recurse-submodules --shallow-submodules \
        "$url" "$destination"
}

fetch SDL2 https://github.com/libsdl-org/SDL.git "$SDL2_REF"
fetch SDL2_image https://github.com/libsdl-org/SDL_image.git "$SDL2_IMAGE_REF"
fetch SDL2_ttf https://github.com/libsdl-org/SDL_ttf.git "$SDL2_TTF_REF"
fetch freetype https://gitlab.freedesktop.org/freetype/freetype.git "$FREETYPE_REF"

: "${ANDROID_HOME:=${ANDROID_SDK_ROOT:-}}"
if [ -z "${ANDROID_HOME:-}" ]; then
    echo "build-android: set ANDROID_HOME to your Android SDK" >&2
    exit 1
fi
if ! command -v gradle >/dev/null 2>&1; then
    echo "build-android: gradle 8.9+ is required on PATH" >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "build-android: python3 is required to generate the i18n header" >&2
    exit 1
fi

variant=${1:-debug}
case $variant in
    debug) task=assembleDebug ;;
    release) task=assembleRelease ;;
    *)
        echo "build-android: unknown variant '$variant' (use debug or release)" >&2
        exit 1
        ;;
esac
cd "$root/android"
gradle "$task"

echo "build-android: APK in android/app/build/outputs/apk/$variant/"
