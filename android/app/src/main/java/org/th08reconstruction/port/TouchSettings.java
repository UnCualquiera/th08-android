package org.th08reconstruction.port;

import android.content.SharedPreferences;

import java.util.Locale;

/**
 * The control options offered by the setup screen.
 *
 * The field names, defaults and the ini serialization mirror
 * src/modern/touch/touch_controls.hpp and the parser in
 * src/modern/software/sdl_present.cpp; the two sides meet in
 * th08_touch_settings.ini. Out-of-range values written here would be clamped
 * by the native parser, but the UI keeps them in range already.
 */
final class TouchSettings {
    static final int STICK_FIXED = 0;
    static final int STICK_FLOATING = 1;
    static final int DISPLAY_FIT = 0;
    static final int DISPLAY_FILL = 1;

    int stickMode = STICK_FLOATING;
    float scale = 1.0f;
    float opacity = 0.45f;
    float deadZone = 0.22f;
    boolean leftHanded = false;
    float stickX = 0.16f;
    float stickY = 0.72f;
    boolean vibrationOnBomb = false;
    // Full-screen stretch by default, as requested for phone play; the setup
    // screen can switch back to the 4:3 letterbox.
    int displayMode = DISPLAY_FILL;

    static TouchSettings load(SharedPreferences preferences) {
        TouchSettings settings = new TouchSettings();
        settings.stickMode = preferences.getInt("stick_mode", settings.stickMode);
        settings.scale = preferences.getFloat("scale", settings.scale);
        settings.opacity = preferences.getFloat("opacity", settings.opacity);
        settings.deadZone = preferences.getFloat("dead_zone", settings.deadZone);
        settings.leftHanded = preferences.getBoolean("left_handed", settings.leftHanded);
        settings.stickX = preferences.getFloat("stick_x", settings.stickX);
        settings.stickY = preferences.getFloat("stick_y", settings.stickY);
        settings.vibrationOnBomb =
                preferences.getBoolean("vibration_on_bomb", settings.vibrationOnBomb);
        settings.displayMode = preferences.getInt("display_mode", settings.displayMode);
        return settings;
    }

    void save(SharedPreferences.Editor editor) {
        editor.putInt("stick_mode", stickMode);
        editor.putFloat("scale", scale);
        editor.putFloat("opacity", opacity);
        editor.putFloat("dead_zone", deadZone);
        editor.putBoolean("left_handed", leftHanded);
        editor.putFloat("stick_x", stickX);
        editor.putFloat("stick_y", stickY);
        editor.putBoolean("vibration_on_bomb", vibrationOnBomb);
        editor.putInt("display_mode", displayMode);
        editor.apply();
    }

    /** key=value lines exactly as the native parser in sdl_present.cpp reads. */
    String toIni() {
        StringBuilder builder = new StringBuilder();
        builder.append("stick_mode=").append(stickMode).append('\n');
        builder.append(String.format(Locale.US, "opacity=%.3f%n", opacity));
        builder.append(String.format(Locale.US, "scale=%.3f%n", scale));
        builder.append(String.format(Locale.US, "dead_zone=%.3f%n", deadZone));
        builder.append("left_handed=").append(leftHanded ? 1 : 0).append('\n');
        builder.append(String.format(Locale.US, "stick_x=%.3f%n", stickX));
        builder.append(String.format(Locale.US, "stick_y=%.3f%n", stickY));
        builder.append("vibration_on_bomb=").append(vibrationOnBomb ? 1 : 0).append('\n');
        builder.append("display_mode=").append(displayMode == DISPLAY_FILL ? "fill" : "fit")
                .append('\n');
        return builder.toString();
    }
}
