package org.th08reconstruction.port;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.pm.PackageInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Bundles every log the port produces into one text file the user can send.
 *
 * What lands in the bundle and why:
 *
 * - Device and app header, so a report is self-describing: model, Android
 *   version, app version, resolved game data directory.
 * - The app's own logcat lines (captured with "logcat -d"; without the
 *   READ_LOGS permission an app only ever sees its own entries, which is
 *   exactly the stderr diagnostics the native side prints).
 * - The game's log.txt from the working directory, decoded from Shift-JIS
 *   because that is the codepage the game writes its messages in.
 * - modern-files.txt (file-open trace) and modern-crash.txt (crash reporter),
 *   plus the stored data directory and touch settings files.
 *
 * The file goes to the public Download collection through MediaStore on
 * API 29+ (no permission needed) and the share sheet is opened on top of it;
 * on API 28 it goes to the app's external files directory, which a file
 * manager or USB connection can still reach there.
 */
final class LogExporter {
    /** Where the bundle landed, and the URI to share when there is one. */
    static final class Result {
        /** Shareable content URI; null when the file is reachable by hand. */
        final Uri shareUri;
        /** Human-readable location for the status line. */
        final String where;

        Result(Uri shareUri, String where) {
            this.shareUri = shareUri;
            this.where = where;
        }
    }

    /** Best effort: the game writes log.txt in Shift-JIS. */
    private static final Charset SHIFT_JIS = loadCharset("Shift_JIS");

    /** Cap for the logcat section so a chatty build cannot produce a monster. */
    private static final int LOGCAT_TAIL_LIMIT = 400000;

    private final Context context;

    LogExporter(Context context) {
        this.context = context;
    }

    /** Runs on a worker thread; performs file and MediaStore I/O. */
    Result export() throws IOException {
        final String stamp =
                new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date());
        final String name = "th08-port-logs-" + stamp + ".txt";
        final byte[] body = buildBundle(stamp);
        if (Build.VERSION.SDK_INT >= 29) {
            final Uri uri = saveToDownloads(name, body);
            return new Result(uri, "Download/" + name);
        }
        final File dir = context.getExternalFilesDir(null);
        if (dir == null) {
            throw new IOException("no external app storage is available");
        }
        final File file = new File(dir, name);
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(body);
        }
        return new Result(null, file.getAbsolutePath());
    }

    private byte[] buildBundle(String stamp) {
        final StringBuilder text = new StringBuilder(64 * 1024);
        text.append("=== th08 Android port diagnostic export ===\n");
        text.append("created: ").append(stamp).append('\n');
        text.append("device: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
                .append(" (Android ").append(Build.VERSION.RELEASE)
                .append(", API ").append(Build.VERSION.SDK_INT).append(")\n");
        text.append("app: ").append(appVersion()).append('\n');
        text.append("game data dir: ").append(GameData.currentDataDir(context)).append('\n');
        text.append("note: the game log section is decoded from Shift-JIS.\n");

        appendSection(text, "logcat (this app)", null, captureLogcat());
        final File gameDir = new File(context.getFilesDir(), "game");
        appendFile(text, "game log.txt", new File(gameDir, "log.txt"), true);
        appendFile(text, "modern-files.txt", new File(gameDir, "modern-files.txt"), false);
        appendFile(text, "modern-crash.txt", new File(gameDir, "modern-crash.txt"), false);
        appendFile(text, "modern-heartbeat.txt", new File(gameDir, "modern-heartbeat.txt"), false);
        appendFile(text, "modern-heartbeat-prev.txt",
                new File(gameDir, "modern-heartbeat-prev.txt"), false);
        appendFile(text, "modern-stall.txt", new File(gameDir, "modern-stall.txt"), false);
        appendFile(text, "th08_data_dir.txt",
                new File(context.getFilesDir(), "th08_data_dir.txt"), false);
        appendFile(text, "th08_touch_settings.ini",
                new File(context.getFilesDir(), "th08_touch_settings.ini"), false);
        return text.toString().getBytes(StandardCharsets.UTF_8);
    }

    private String appVersion() {
        try {
            final PackageInfo info = context.getPackageManager()
                    .getPackageInfo(context.getPackageName(), 0);
            return info.versionName + " (code " + info.getLongVersionCode() + ")";
        } catch (Exception e) {
            return "unknown (" + e + ")";
        }
    }

    /** Dumps this app's own logcat entries; an app cannot see any others. */
    private String captureLogcat() {
        try {
            final Process process = new ProcessBuilder("logcat", "-d", "-v", "time")
                    .redirectErrorStream(true)
                    .start();
            final String dump;
            try (InputStream in = process.getInputStream()) {
                dump = readAllText(in);
            }
            process.waitFor();
            if (dump.length() > LOGCAT_TAIL_LIMIT) {
                return dump.substring(dump.length() - LOGCAT_TAIL_LIMIT);
            }
            return dump;
        } catch (Exception e) {
            return "(logcat unavailable: " + e + ")";
        }
    }

    private static void appendSection(StringBuilder text, String title, File file, String body) {
        text.append("\n=== ").append(title);
        if (file != null) {
            text.append(" (").append(file.getAbsolutePath()).append(')');
        }
        text.append(" ===\n").append(body);
        if (!body.endsWith("\n")) {
            text.append('\n');
        }
    }

    private static void appendFile(StringBuilder text, String title, File file,
                                   boolean decodeShiftJis) {
        if (!file.isFile()) {
            appendSection(text, title, file, "(missing)");
            return;
        }
        final byte[] data = readFile(file);
        final String body;
        if (decodeShiftJis && SHIFT_JIS != null) {
            body = new String(data, SHIFT_JIS);
        } else {
            body = new String(data, StandardCharsets.UTF_8);
        }
        appendSection(text, title, file, body);
    }

    /** MediaStore insert into the public Download collection; no permission. */
    private Uri saveToDownloads(String name, byte[] body) throws IOException {
        final ContentResolver resolver = context.getContentResolver();
        final ContentValues values = new ContentValues();
        values.put(MediaStore.Downloads.DISPLAY_NAME, name);
        values.put(MediaStore.Downloads.MIME_TYPE, "text/plain");
        values.put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS);
        values.put(MediaStore.Downloads.IS_PENDING, 1);
        final Uri item = resolver.insert(
                MediaStore.Downloads.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY), values);
        if (item == null) {
            throw new IOException("the media store refused the export file");
        }
        try (OutputStream out = resolver.openOutputStream(item)) {
            if (out == null) {
                throw new IOException("cannot open " + item);
            }
            out.write(body);
        } catch (IOException e) {
            resolver.delete(item, null, null);
            throw e;
        }
        values.clear();
        values.put(MediaStore.Downloads.IS_PENDING, 0);
        resolver.update(item, values, null, null);
        return item;
    }

    private static byte[] readFile(File file) {
        try (InputStream in = new FileInputStream(file)) {
            return readAll(in);
        } catch (IOException e) {
            return ("(unreadable: " + e + ")").getBytes(StandardCharsets.UTF_8);
        }
    }

    private static String readAllText(InputStream in) throws IOException {
        return new String(readAll(in), StandardCharsets.UTF_8);
    }

    private static byte[] readAll(InputStream in) throws IOException {
        final ByteArrayOutputStream out = new ByteArrayOutputStream(64 * 1024);
        final byte[] buffer = new byte[32 * 1024];
        int read;
        while ((read = in.read(buffer)) > 0) {
            out.write(buffer, 0, read);
        }
        return out.toByteArray();
    }

    private static Charset loadCharset(String name) {
        try {
            return Charset.forName(name);
        } catch (Exception e) {
            return null;
        }
    }
}
