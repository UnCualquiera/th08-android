package org.th08reconstruction.port;

import android.content.Context;
import android.content.SharedPreferences;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;

/**
 * Persistence shared by the setup screen and the game activity.
 *
 * The native runtime reads two files from the app's internal storage, which
 * is the same directory SDL_AndroidGetInternalStoragePath returns:
 *   - th08_data_dir.txt: one line, the absolute path of the game folder
 *   - th08_touch_settings.ini: key=value lines matching the native parser
 * The same values also live in SharedPreferences so the setup screen can show
 * them again on the next launch.
 */
final class GameData {
    static final String PREFS = "th08_port";
    static final String KEY_DATA_DIR = "data_dir";
    static final String DATA_DIR_FILE = "th08_data_dir.txt";
    static final String TOUCH_SETTINGS_FILE = "th08_touch_settings.ini";

    private GameData() {}

    static String currentDataDir(Context context) {
        final String stored = preferences(context).getString(KEY_DATA_DIR, null);
        if (stored != null && !stored.isEmpty()) {
            return stored;
        }
        // The preferences copy is written asynchronously and can be lost when
        // the process dies abruptly (an ART abort, a force stop). The data
        // dir file is written synchronously and is what the native side
        // reads, so it is the fallback of record; finding it there repairs
        // the preferences copy too.
        final String fromFile = readFirstLine(new File(context.getFilesDir(), DATA_DIR_FILE));
        if (fromFile != null && !fromFile.isEmpty()) {
            try {
                storeDataDir(context, fromFile);
            } catch (IOException ignored) {
                // The file itself already holds the value; only the repair
                // of the preferences copy failed.
            }
            return fromFile;
        }
        return stored;
    }

    private static String readFirstLine(File file) {
        if (!file.isFile()) {
            return null;
        }
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                new FileInputStream(file), StandardCharsets.UTF_8))) {
            return reader.readLine();
        } catch (IOException e) {
            return null;
        }
    }

    static void storeDataDir(Context context, String dir) throws IOException {
        preferences(context).edit().putString(KEY_DATA_DIR, dir).apply();
        writeFile(new File(context.getFilesDir(), DATA_DIR_FILE), dir + "\n");
    }

    /** Forgets a stored folder, e.g. one from an older build's layout. */
    static void clearDataDir(Context context) {
        preferences(context).edit().remove(KEY_DATA_DIR).apply();
        //noinspection ResultOfMethodCallIgnored
        new File(context.getFilesDir(), DATA_DIR_FILE).delete();
    }

    static void storeTouchSettings(Context context, TouchSettings settings) throws IOException {
        settings.save(preferences(context).edit());
        writeFile(new File(context.getFilesDir(), TOUCH_SETTINGS_FILE), settings.toIni());
    }

    static TouchSettings loadTouchSettings(Context context) {
        return TouchSettings.load(preferences(context));
    }

    /** True when the folder holds the game data archive. */
    static boolean hasGameData(String dir) {
        return dir != null && !dir.isEmpty() && new File(dir, "th08.dat").isFile();
    }

    /** True when the folder also holds the music archive; the game runs
     *  without it, but stages have no music. */
    static boolean hasMusicData(String dir) {
        return dir != null && !dir.isEmpty() && new File(dir, "thbgm.dat").isFile();
    }

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    private static void writeFile(File file, String content) throws IOException {
        try (Writer writer =
                     new OutputStreamWriter(new FileOutputStream(file), StandardCharsets.UTF_8)) {
            writer.write(content);
        }
    }
}
